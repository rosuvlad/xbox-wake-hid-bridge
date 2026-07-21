// main.cpp — Xbox Controller USB Wake Bridge, milestone 1.
//
// This build delivers the out-of-box experience: a guided pairing wizard on
// the tiny 128x128 panel, persistent bonding, silent auto-reconnect, and a
// two-page live debug view (precise values + controller diagram).
//
// Button scheme (matches the spec):
//   Short LEFT  -> previous page          Short RIGHT -> next page
//   Long  LEFT  -> pair a new controller  Long  RIGHT -> forget controller
//   Long  BOTH  -> diagnostics
//
// Headless builds (UX_LED, see Ux.h) have one LED and one button instead, so
// the scheme collapses to:
//   Hold BOOT 2s -> forget controller and reboot into pairing
// Nothing is lost: pages need a screen, and "pair a new controller" and "forget
// controller" were already the same operation on both long-press paths below.
#include <Arduino.h>

#if !UX_LED
#include <OneButton.h>
#endif

#include "AppState.h"
#include "BridgeState.h"
#include "Config.h"
#include "ControllerLink.h"
#include "UsbGamepad.h"
#include "Ux.h"

#if UX_LED
#include "HoldButton.h"
#endif

static Ux ui;
static ControllerLink padLink;
static UsbGamepad usb;
static BridgeState bridge;

#if UX_LED
static HoldButton btnForget(FORGET_HOLD_MS);
#else
static OneButton btnL(PIN_BTN_LEFT, true);
static OneButton btnR(PIN_BTN_RIGHT, true);
#endif

static Screen screen = Screen::Splash;
static LivePage livePage = LivePage::Values;
static uint32_t tEnter = 0;      // millis() when current screen was entered
static uint32_t lastFrame = 0;   // render throttle
#if !UX_LED
static bool comboActive = false; // a two-button combo is in progress
#endif

// USB forwarding bookkeeping
static unsigned long lastNotifSent = 0;  // BLE notif stamp last forwarded
static uint16_t usbReportCount = 0;      // reports this Hz window
static uint32_t usbHzWindow = 0;
static uint32_t wakingUntil = 0;         // show the "Waking PC" flash until

// ---------------------------------------------------------------------------
// Helpers
// ---------------------------------------------------------------------------
static void enter(Screen s) {
  screen = s;
  tEnter = millis();
}

#if !UX_LED
static bool bothPressed() {
  return digitalRead(PIN_BTN_LEFT) == LOW && digitalRead(PIN_BTN_RIGHT) == LOW;
}

static void cyclePage(int dir) {
  int n = (int)LivePage::_count;
  livePage = (LivePage)(((int)livePage + dir + n) % n);
}
#endif

// Drop the bond and reboot into the out-of-box scan. Both button schemes end
// here; the restart is what makes ControllerLink re-read (empty) NVS and scan.
static void forgetAndRestart() {
  padLink.forgetBond();
  delay(40);
  ESP.restart();
}

// Enter the live view and give the controller a short "connected" buzz — this
// doubles as a self-test that the rumble path to the pad works.
static void enterLive() {
  padLink.pulseRumble(45, 22);  // ~0.22s on all motors
  enter(Screen::Live);
}

// USB-identity switch: hold View + Menu + D-pad Down on the pad for
// IDENTITY_COMBO_MS. A pad chord rather than a board button because it works
// identically on the TFT and headless builds, and the one headless button
// already owns the forget-hold. Gated on "awake and streaming" so it cannot
// tangle with the sleep/wake machinery.
static void serviceIdentityCombo(uint32_t now) {
  static uint32_t comboSince = 0;
  bool held = padLink.isReceiving() && !bridge.usbSuspended &&
              padLink.identityComboPressed();
  if (!held) {
    comboSince = 0;
    return;
  }
  if (!comboSince) {
    comboSince = now ? now : 1;
    return;
  }
  if (now - comboSince < IDENTITY_COMBO_MS) return;

  const bool toXusb = !UsbGamepad::xusbIdentity();
  UsbGamepad::setXusbIdentity(toXusb);
  padLink.pulseRumble(45, 22);  // "heard you" — mirrors the connect buzz
  // Blocking on purpose: the reboot is next, and the confirmation must be
  // seen. Same idiom as the alignTest loop in setup().
  const uint32_t t0 = millis();
  while (millis() - t0 < IDENTITY_FLASH_MS) {
    ui.identitySwitch(toXusb, millis());
    delay(FRAME_MS);
  }
  ESP.restart();
}

#if !UX_LED
// ---------------------------------------------------------------------------
// Button intents (run from *.tick() in loop, so plain function calls are safe)
// ---------------------------------------------------------------------------
static void onLeftClick() {
  if (comboActive) return;
  switch (screen) {
    case Screen::Live: cyclePage(-1); break;
    case Screen::Diagnostics: enter(Screen::Live); break;
    case Screen::ForgetConfirm: enter(Screen::Live); break;  // cancel
    default: break;
  }
}

static void onRightClick() {
  if (comboActive) return;
  switch (screen) {
    case Screen::Live: cyclePage(+1); break;
    case Screen::Diagnostics: enter(Screen::Live); break;
    case Screen::ForgetConfirm: enter(Screen::Live); break;  // cancel
    default: break;
  }
}

static void onLeftLong() {
  if (bothPressed()) {         // long-both -> diagnostics
    comboActive = true;
    enter(Screen::Diagnostics);
    return;
  }
  if (comboActive) return;
  // Pair a new controller: drop the bond and reboot into the OOBE scan.
  forgetAndRestart();
}

static void onRightLong() {
  if (bothPressed()) {         // long-both -> diagnostics
    comboActive = true;
    enter(Screen::Diagnostics);
    return;
  }
  if (comboActive) return;
  if (screen == Screen::ForgetConfirm) {
    forgetAndRestart();
  } else {
    enter(Screen::ForgetConfirm);
  }
}
#endif  // !UX_LED

// ---------------------------------------------------------------------------
// Setup
// ---------------------------------------------------------------------------
void setup() {
  Serial.begin(115200);
  ui.begin();

#if UI_ALIGN_TEST
  // Nothing else runs. Re-called rather than drawn once because the headless
  // backend animates an R/G/B cycle here; the TFT pattern is static and simply
  // redraws itself.
  while (true) {
    ui.alignTest();
    delay(FRAME_MS);
  }
#endif

  // Bring up the native-USB Xbox controller device before BLE so the PC can
  // enumerate it immediately.
  usb.begin();

#if UX_LED
  pinMode(BTN_PIN, INPUT_PULLUP);
#else
  btnL.attachClick(onLeftClick);
  btnR.attachClick(onRightClick);
  btnL.attachLongPressStart(onLeftLong);
  btnR.attachLongPressStart(onRightLong);
#endif

  padLink.begin();

  // Known pad -> reconnect silently. New device -> run the OOBE.
  enter(padLink.hasBond() ? Screen::Reconnecting : Screen::Splash);
}

// ---------------------------------------------------------------------------
// One render + transition tick (called at ~30 fps)
// ---------------------------------------------------------------------------
static void frame(uint32_t now) {
  uint32_t age = now - tEnter;
  ui.setBridge(bridge);

  // Remote-wake flash overrides whatever screen we are on.
  if (now < wakingUntil) {
    ui.waking();
    return;
  }

#if UX_LED
  // Same idea for the forget hold: an override rather than a Screen, so the
  // state machine below is untouched and releasing the button simply resumes
  // whatever was on screen.
  if (btnForget.isDown()) {
    ui.forgetHold(btnForget.progress());
    return;
  }
#endif

  switch (screen) {
    case Screen::Splash:
      ui.splash((float)age / SPLASH_MS);
      if (age >= SPLASH_MS) enter(Screen::OobeStep1);
      break;

    case Screen::OobeStep1:
      ui.oobeStep1(now);
      if (padLink.isConnected()) enter(Screen::Found);
      else if (age >= OOBE_STEP1_MS) enter(Screen::OobeStep2);
      break;

    case Screen::OobeStep2:
      ui.oobeStep2(now);
      if (padLink.isConnected()) enter(Screen::Found);
      break;

    case Screen::Found:
      ui.found(now);
      if (padLink.isReceiving()) {
        padLink.saveBondFromCurrent();
        enter(Screen::Paired);
      } else if (!padLink.isConnected()) {
        enter(Screen::OobeStep2);  // dropped before first report; keep scanning
      }
      break;

    case Screen::Paired:
      ui.paired(padLink.snapshot().addr);
      if (age >= PAIRED_CELEBRATE_MS) {
        livePage = LivePage::Values;
        enterLive();
      }
      break;

    case Screen::Reconnecting:
      ui.reconnecting(now, age >= RECONNECT_HINT_MS);
      if (padLink.isReceiving()) enterLive();
      break;

    case Screen::Live: {
      // While the PC sleeps the pad is *meant* to be gone (released so it can
      // power down) — stay here so the LED tells the "PC asleep" story, not a
      // spurious "reconnecting" one.
      if (!padLink.isConnected() && !bridge.usbSuspended) {
        enter(Screen::Reconnecting);
        break;
      }
      PadSnapshot snap = padLink.snapshot();
      switch (livePage) {
        case LivePage::Values: ui.liveValues(snap); break;
        case LivePage::Pad: ui.livePad(snap); break;
        case LivePage::Bridge: ui.liveBridge(snap); break;
        default: ui.liveValues(snap); break;
      }
      break;
    }

    case Screen::Diagnostics:
      ui.diagnostics(padLink.snapshot(), now);
      break;

    case Screen::ForgetConfirm:
      ui.forgetConfirm();
      if (age >= 10000) enter(Screen::Live);  // auto-cancel
      break;
  }
}

// ---------------------------------------------------------------------------
// Loop
// ---------------------------------------------------------------------------
// Forward controller input to the PC and PC rumble to the controller. Runs
// every loop iteration (not frame-gated) to keep bridge latency minimal.
static void serviceUsb(uint32_t now) {
  usb.service(now);  // suspend tracking + post-resume recovery
  bridge.usbReady = usb.ready();
  bridge.usbSuspended = usb.suspended();

#if SLEEP_RELEASES_PAD
  // Console-style sleep: let go of the pad so it powers itself down (Guide
  // light off). The grace window must outlive the pad's own link-loss search
  // — resume scanning earlier and the still-searching pad slips back in,
  // which the reconnect trigger below would read as a wake request.
  static uint32_t suspendedSince = 0;
  static uint32_t padReleasedAt = 0;
  if (bridge.usbSuspended) {
    if (!suspendedSince) suspendedSince = now ? now : 1;
    if (!padReleasedAt) {
      // Debounced: a wake attempt in flight flickers the suspend flag and
      // must not read as a fresh sleep (see PAD_RELEASE_DEBOUNCE_MS).
      if (now - suspendedSince >= PAD_RELEASE_DEBOUNCE_MS) {
        padLink.setReleased(true);
        padReleasedAt = now ? now : 1;
      }
    } else if (padLink.isReleased() &&
               now - padReleasedAt >= PAD_RELEASE_GRACE_MS) {
      padLink.setReleased(false);  // start listening for the wake reconnect
    }
  } else {
    suspendedSince = 0;
    padReleasedAt = 0;
    padLink.setReleased(false);
  }
#endif

  // The pad (re)connecting while the host sleeps IS a wake request: the user
  // pressed Guide to power it on. This holds with or without the release
  // above — a pad that idled itself off overnight wakes the PC the same way
  // when it comes back.
  static bool prevReceiving = false;
  bool nowReceiving = padLink.isReceiving();
  if (bridge.usbSuspended && nowReceiving && !prevReceiving &&
      usb.remoteWakeup()) {
    wakingUntil = now + 1500;
    padLink.pulseRumble(45, 22);  // "wake accepted" buzz, console-style
  }
  prevReceiving = nowReceiving;

  // The Xbox (Guide) button is the wake button; edge-detect its press.
  static bool prevXbox = false;
  if (!bridge.usbSuspended) prevXbox = false;

  if (padLink.isReceiving()) {
    unsigned long nAt = padLink.receiveNotificationAt();
    bool fresh = (nAt != lastNotifSent);

    if (bridge.usbSuspended) {
      // PC is asleep: only a fresh Xbox-button press wakes it, like a console.
      if (fresh) {
        bool xbox = padLink.xboxPressed();
        if (xbox && !prevXbox && usb.remoteWakeup()) wakingUntil = now + 1500;
        prevXbox = xbox;
        lastNotifSent = nAt;
      }
    } else if (bridge.usbReady && fresh) {
      // Awake: forward the input report with minimal latency.
      uint8_t rep[UsbGamepad::INPUT_LEN];
      if (padLink.buildUsbReport(rep) && usb.sendInput(rep)) {
        usbReportCount++;
        bridge.latencyMs = (uint16_t)(now - nAt);
      }
      lastNotifSent = nAt;
    }
  }

  // PC rumble -> controller (raw 8-byte Xbox rumble report).
  uint8_t rmb[UsbGamepad::RUMBLE_LEN];
  if (usb.takeRumble(rmb)) {
    padLink.sendRawRumble(rmb, sizeof(rmb));
    bridge.rmbLeft = rmb[1];
    bridge.rmbRight = rmb[2];
    bridge.rmbLT = rmb[3];
    bridge.rmbRT = rmb[4];
    bridge.lastRumbleMs = now;
  }

  if (now - usbHzWindow >= 1000) {
    bridge.usbHz = usbReportCount;
    usbReportCount = 0;
    usbHzWindow = now;
  }
}

void loop() {
  uint32_t now = millis();

  padLink.loop();       // service NimBLE + rate meter every iteration
  serviceUsb(now);      // forward inputs/rumble with minimal latency
  serviceIdentityCombo(now);  // View+Menu+Down chord -> switch USB face

#if UX_LED
  // BOOT is active-low. update() fires once, the moment the hold completes.
  if (btnForget.update(digitalRead(BTN_PIN) == LOW, now)) forgetAndRestart();
#else
  btnL.tick();
  btnR.tick();

  // Clear the combo latch once both buttons are released.
  if (comboActive && digitalRead(PIN_BTN_LEFT) == HIGH &&
      digitalRead(PIN_BTN_RIGHT) == HIGH) {
    comboActive = false;
  }
#endif

  if (now - lastFrame >= FRAME_MS) {
    lastFrame = now;
    frame(now);
  }
}
