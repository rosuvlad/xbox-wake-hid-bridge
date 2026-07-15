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
#include <Arduino.h>
#include <OneButton.h>

#include "AppState.h"
#include "BridgeState.h"
#include "Config.h"
#include "ControllerLink.h"
#include "Ui.h"
#include "UsbGamepad.h"

static Ui ui;
static ControllerLink padLink;
static UsbGamepad usb;
static BridgeState bridge;
static OneButton btnL(PIN_BTN_LEFT, true);
static OneButton btnR(PIN_BTN_RIGHT, true);

static Screen screen = Screen::Splash;
static LivePage livePage = LivePage::Values;
static uint32_t tEnter = 0;      // millis() when current screen was entered
static uint32_t lastFrame = 0;   // render throttle
static bool comboActive = false; // a two-button combo is in progress

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

static bool bothPressed() {
  return digitalRead(PIN_BTN_LEFT) == LOW && digitalRead(PIN_BTN_RIGHT) == LOW;
}

static void cyclePage(int dir) {
  int n = (int)LivePage::_count;
  livePage = (LivePage)(((int)livePage + dir + n) % n);
}

// Enter the live view and give the controller a short "connected" buzz — this
// doubles as a self-test that the rumble path to the pad works.
static void enterLive() {
  padLink.pulseRumble(45, 22);  // ~0.22s on all motors
  enter(Screen::Live);
}

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
  padLink.forgetBond();
  delay(40);
  ESP.restart();
}

static void onRightLong() {
  if (bothPressed()) {         // long-both -> diagnostics
    comboActive = true;
    enter(Screen::Diagnostics);
    return;
  }
  if (comboActive) return;
  if (screen == Screen::ForgetConfirm) {
    padLink.forgetBond();
    delay(40);
    ESP.restart();
  } else {
    enter(Screen::ForgetConfirm);
  }
}

// ---------------------------------------------------------------------------
// Setup
// ---------------------------------------------------------------------------
void setup() {
  Serial.begin(115200);
  ui.begin();

#if UI_ALIGN_TEST
  ui.alignTest();
  while (true) delay(1000);  // hold the pattern; nothing else runs
#endif

  // Bring up the native-USB Xbox controller device before BLE so the PC can
  // enumerate it immediately.
  usb.begin();

  btnL.attachClick(onLeftClick);
  btnR.attachClick(onRightClick);
  btnL.attachLongPressStart(onLeftLong);
  btnR.attachLongPressStart(onRightLong);

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
      if (!padLink.isConnected()) {
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
  bridge.usbReady = usb.ready();
  bridge.usbSuspended = usb.suspended();

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
  btnL.tick();
  btnR.tick();

  // Clear the combo latch once both buttons are released.
  if (comboActive && digitalRead(PIN_BTN_LEFT) == HIGH &&
      digitalRead(PIN_BTN_RIGHT) == HIGH) {
    comboActive = false;
  }

  if (now - lastFrame >= FRAME_MS) {
    lastFrame = now;
    frame(now);
  }
}
