// UsbGamepad.cpp
#include "UsbGamepad.h"

#include <string.h>

#include <Preferences.h>

#include "Config.h"
#include "XusbDevice.h"
#include "soc/usb_reg.h"
#include "soc/usb_struct.h"
#include "soc/usb_wrap_reg.h"
#include "soc/usb_wrap_struct.h"

// TinyUSB device-state probe (declared here like tud_remote_wakeup was: the
// Arduino core links TinyUSB but does not expose tusb.h to sketches).
extern "C" bool tud_mounted(void);

// Why this file talks to USB0 registers directly
// ----------------------------------------------
// The Arduino core's TinyUSB driver cannot see a sleeping PC. Its patched
// dcd_esp32sx.c posts DCD_EVENT_UNPLUGGED where stock TinyUSB posts
// DCD_EVENT_SUSPEND (the S3 has no VBUS sensing, so Espressif treats bus-idle
// as cable-gone), and the resume interrupt is never unmasked. The moment the
// host suspends, TinyUSB tears the device down: ARDUINO_USB_SUSPEND/RESUME
// never fire, tud_remote_wakeup() refuses (its state says "unplugged"), and
// when the host wakes the still-unconfigured device stays dead until it is
// physically replugged. Present in arduino-esp32 2.0.17 and current 3.x alike
// (github.com/rosuvlad/xbox-wake-hid-bridge/issues/4).
//
// The unplug lie is not the driver's only blind spot. It reports a bus reset
// to the stack only when the host *completes* one (the ENUMDONE interrupt); a
// reset that starts and never ends — which is what a port powering down into
// SE0 looks like — is handled as internal FIFO cleanup and posts no event at
// all (verified in the shipped dcd_esp32sx: USBRST and RESETDET call only
// bus_reset()). A device that was configured when its bus died that way stays
// tud_mounted() forever, so neither TinyUSB flag can be trusted for liveness.
//
// The DWC-OTG controller underneath is fine, so the bridge reads it directly:
//   asleep   = the SOF heartbeat going silent (GINTSTS.SOF latch), with
//              DSTS.SUSPSTS as a 3 ms fast path — never TinyUSB's flags —
//              backed by a "nothing has worked for 10 s" backstop for the
//              states the heartbeat cannot speak for (kUnusableArmsWakeMs)
//   wake     = drive DCTL.RMTWKUPSIG (resume K-state) ourselves
//   recovery = a physical-line disconnect pulse (DCTL.SFTDISCON plus the
//              ESP32 USB wrapper's pad override) once the bus is active again,
//              so the host re-enumerates the device.
//
// And the fake unplug is stopped at its source: GINTMSK.USBSUSP is cleared
// right after USB.begin() (see blindDcdToSuspend below), so the DCD never
// sees the suspend interrupt whose only effect was that teardown. The device
// now stays configured through every suspend and resume needs no
// re-enumeration. That matters beyond PC sleep: hosts also suspend an *idle*
// device while wide awake (Linux USB autosuspend, Windows selective suspend
// — default-on there), and before this the teardown-plus-recovery-pulse
// cycle made the bridge visibly disconnect/reconnect on an awake desktop and
// kicked a freshly connected pad off BLE (issue #4, closing report). The
// suspend itself is indistinguishable from PC sleep on the bus — the LED may
// breathe amber under an autosuspending awake host — but with the state
// intact the first remote wakeup resumes the port in milliseconds and input
// flows immediately, so the misread is now harmless.

// Resume-signalling hold. The spec window is 1-15 ms; the driver's own
// dcd_remote_wakeup() holds for one FreeRTOS tick, which can round down to
// ~0 ms and be missed by the host.
static const uint32_t kResumeSignalMs = 5;
// A host that resets us itself after resume gets this long before we force
// re-enumeration. Counted from *verified SOFs* (see the probe below), so the
// host controller is demonstrably up when it expires.
static const uint32_t kReenumGraceMs = 1000;
// Physical-line disconnect pulse width. Generous on purpose: a PC mid-resume
// debounces port changes, and a short blip can be coalesced away entirely.
static const uint32_t kDetachPulseMs = 300;
// Re-enumeration is retried at this pace for as long as the pathological
// state persists (SOFs flowing + still unconfigured). Each retry restarts
// host-side enumeration, so pacing only has to outlast one attempt.
static const uint32_t kPulseRetryMs = 2500;
// Bus still idle this long after remote wakeup means the host ignored it
// (wakeup not armed for us). A disconnect pulse also wakes a wake-armed port,
// so fall back to that.
static const uint32_t kWakeFallbackMs = 1000;
// SOF freshness sampling period. A living host emits one SOF per millisecond,
// so a sample window with zero SOFs is unambiguous: the host is asleep or
// gone. This heartbeat — not any line state — is what "host awake" means.
static const uint32_t kSofStaleMs = 250;
// How long the bridge may sit with nothing working before it arms the wake on
// that basis alone. The heartbeat verdict below is the precise one, but it
// speaks only for a host that has already configured us once; two real states
// fall outside it and both used to disarm the wake permanently:
//
//   * the bridge reset while the host slept (brownout, watchdog, a glitch on
//     a port that keeps VBUS through sleep). everMounted_ is false again, and
//     nothing can ever set it: the only host that could enumerate us is the
//     sleeping one we are supposed to wake. Issue #4's >1 h reports look
//     exactly like this — breathing green all night, Guide button inert.
//   * anything that leaves the SOF latch reading alive while the device is
//     unusable, which would make the heartbeat lie in the safe-looking
//     direction.
//
// Ten seconds is far past enumeration (well under a second even on a slow
// host, and re-checked every tick), so a healthy boot never trips it; when it
// does trip, the host is asleep, off, or the cable is dead — arming is right
// in the first case and costs nothing in the others.
static const uint32_t kUnusableArmsWakeMs = 10000;

// Xbox Wireless Controller (Series, model 1914) Bluetooth-LE HID report
// descriptor, 283 bytes, verbatim from a real controller dump
// (DJm00n/ControllersInfo). Report id 1 = input (16B), report id 3 = rumble.
static const uint8_t kReportDescriptor[] = {
    0x05, 0x01, 0x09, 0x05, 0xA1, 0x01, 0x85, 0x01, 0x09, 0x01, 0xA1, 0x00,
    0x09, 0x30, 0x09, 0x31, 0x15, 0x00, 0x27, 0xFF, 0xFF, 0x00, 0x00, 0x95,
    0x02, 0x75, 0x10, 0x81, 0x02, 0xC0, 0x09, 0x01, 0xA1, 0x00, 0x09, 0x32,
    0x09, 0x35, 0x15, 0x00, 0x27, 0xFF, 0xFF, 0x00, 0x00, 0x95, 0x02, 0x75,
    0x10, 0x81, 0x02, 0xC0, 0x05, 0x02, 0x09, 0xC5, 0x15, 0x00, 0x26, 0xFF,
    0x03, 0x95, 0x01, 0x75, 0x0A, 0x81, 0x02, 0x15, 0x00, 0x25, 0x00, 0x75,
    0x06, 0x95, 0x01, 0x81, 0x03, 0x05, 0x02, 0x09, 0xC4, 0x15, 0x00, 0x26,
    0xFF, 0x03, 0x95, 0x01, 0x75, 0x0A, 0x81, 0x02, 0x15, 0x00, 0x25, 0x00,
    0x75, 0x06, 0x95, 0x01, 0x81, 0x03, 0x05, 0x01, 0x09, 0x39, 0x15, 0x01,
    0x25, 0x08, 0x35, 0x00, 0x46, 0x3B, 0x01, 0x66, 0x14, 0x00, 0x75, 0x04,
    0x95, 0x01, 0x81, 0x42, 0x75, 0x04, 0x95, 0x01, 0x15, 0x00, 0x25, 0x00,
    0x35, 0x00, 0x45, 0x00, 0x65, 0x00, 0x81, 0x03, 0x05, 0x09, 0x19, 0x01,
    0x29, 0x0F, 0x15, 0x00, 0x25, 0x01, 0x75, 0x01, 0x95, 0x0F, 0x81, 0x02,
    0x15, 0x00, 0x25, 0x00, 0x75, 0x01, 0x95, 0x01, 0x81, 0x03, 0x05, 0x0C,
    0x0A, 0xB2, 0x00, 0x15, 0x00, 0x25, 0x01, 0x95, 0x01, 0x75, 0x01, 0x81,
    0x02, 0x15, 0x00, 0x25, 0x00, 0x75, 0x07, 0x95, 0x01, 0x81, 0x03, 0x05,
    0x0F, 0x09, 0x21, 0x85, 0x03, 0xA1, 0x02, 0x09, 0x97, 0x15, 0x00, 0x25,
    0x01, 0x75, 0x04, 0x95, 0x01, 0x91, 0x02, 0x15, 0x00, 0x25, 0x00, 0x75,
    0x04, 0x95, 0x01, 0x91, 0x03, 0x09, 0x70, 0x15, 0x00, 0x25, 0x64, 0x75,
    0x08, 0x95, 0x04, 0x91, 0x02, 0x09, 0x50, 0x66, 0x01, 0x10, 0x55, 0x0E,
    0x15, 0x00, 0x26, 0xFF, 0x00, 0x75, 0x08, 0x95, 0x01, 0x91, 0x02, 0x09,
    0xA7, 0x15, 0x00, 0x26, 0xFF, 0x00, 0x75, 0x08, 0x95, 0x01, 0x91, 0x02,
    0x65, 0x00, 0x55, 0x00, 0x09, 0x7C, 0x15, 0x00, 0x26, 0xFF, 0x00, 0x75,
    0x08, 0x95, 0x01, 0x91, 0x02, 0xC0, 0xC0,
};

// Which USB face is active this boot. Chosen from NVS before enumeration and
// fixed until the next reboot — see xusbIdentity()/setXusbIdentity().
static bool s_xusb = false;

// ESP32-S2/S3 need both layers for a disconnect the host can see. DWC2's
// SFTDISCON updates the controller state, but does not reliably force the
// internal PHY pads to the disconnected SE0 line state, so upstream TinyUSB
// drives both D+/D- pulls low "in addition to dwc2's dctrl" (hathach/tinyusb
// 0bb7b99). The values below are that commit's, bit for bit.
//
// Nothing in this build competes for these bits: the Arduino core never runs
// ESP-IDF's usb_phy device init, and the shipped dcd_esp32sx references
// USB_WRAP only at .date (the usb-persist magic), so otg_conf sits at its
// reset default of pad_pull_override=0 (both verified by disassembling
// libhal.a and libarduino_tinyusb.a). Two consequences: SFTDISCON alone was
// only *releasing* D+ and leaving SE0 to the host's own pulldowns, where the
// override actively drives both lines low; and clearing the whole mask on
// reconnect restores exactly the state the device has enumerated in all
// along, so the pulse cannot strand us disconnected.
//
// Whether that stronger disconnect is what finally reaches a root port with
// its receiver powered down — where resume signalling has nobody listening —
// is the untested premise of this change, not a verified one.
static const uint32_t kUsbPadPullMask =
    USB_WRAP_PAD_PULL_OVERRIDE_M | USB_WRAP_DP_PULLUP_M |
    USB_WRAP_DP_PULLDOWN_M | USB_WRAP_DM_PULLUP_M |
    USB_WRAP_DM_PULLDOWN_M;

static void forceUsbDisconnect() {
  uint32_t conf = USB_WRAP.otg_conf.val;
  conf &= ~kUsbPadPullMask;
  conf |= USB_WRAP_PAD_PULL_OVERRIDE_M | USB_WRAP_DP_PULLDOWN_M |
          USB_WRAP_DM_PULLDOWN_M;
  USB_WRAP.otg_conf.val = conf;
  USB0.dctl |= USB_SFTDISCON_M;
}

static void forceUsbConnect() {
  USB_WRAP.otg_conf.val &= ~kUsbPadPullMask;
  USB0.dctl &= ~USB_SFTDISCON_M;
}

// Mask the bus-suspend interrupt so the DCD's suspend-as-unplug teardown can
// never run (header note above). Safe to do once: dcd_init() — which runs
// synchronously inside USB.begin() — is the only code that sets this bit,
// and bus_reset() only ORs the endpoint-interrupt bits back into GINTMSK
// (both verified in the shipped libarduino_tinyusb.a). The suspend *status*
// keeps latching in GINTSTS/DSTS regardless of the mask, which is all the
// detection below reads.
static void blindDcdToSuspend() { USB0.gintmsk &= ~USB_USBSUSPMSK_M; }

bool UsbGamepad::xusbIdentity() { return s_xusb; }

void UsbGamepad::setXusbIdentity(bool on) {
  Preferences p;
  p.begin("xbridge", false);
  p.putUChar("usbIdent", on ? 1 : 0);
  p.end();
}

void UsbGamepad::begin() {
  {
    Preferences p;
    p.begin("xbridge", false);
    s_xusb = p.getUChar("usbIdent", USB_XUSB_DEFAULT) != 0;
    p.end();
  }

  if (s_xusb) {
    // Windows face: wired Xbox 360 controller. The VID/PID alone binds the
    // inbox XInput driver, which is what makes Steam and games see us —
    // see XusbDevice.h and issue #6.
    USB.VID(0x045E);
    USB.PID(0x028E);
    USB.manufacturerName("Microsoft");
    USB.productName("Xbox 360 Controller");
    USB.firmwareVersion(0x0114);
    // The real pad is vendor-specific at the device level too.
    USB.usbClass(0xFF);
    USB.usbSubClass(0xFF);
    USB.usbProtocol(0xFF);
    USB.usbAttributes(0x20);  // TUSB_DESC_CONFIG_ATT_REMOTE_WAKEUP
    xusb::begin();
    USB.begin();
    blindDcdToSuspend();
    return;
  }

  hid_.addDevice(this, sizeof(kReportDescriptor));
  // Present as a genuine Xbox Wireless Controller so Linux/Steam bind the
  // proper driver (rumble, correct button map).
  USB.VID(0x045E);
  USB.PID(0x0B13);
  USB.manufacturerName("Microsoft");
  USB.productName("Xbox Wireless Controller");
  // Advertise remote-wakeup in the config descriptor (0x20) so the host lets
  // us resume it from suspend; without this the OS never arms wake-up.
  USB.usbAttributes(0x20);  // TUSB_DESC_CONFIG_ATT_REMOTE_WAKEUP
  hid_.begin();
  USB.begin();
  blindDcdToSuspend();
}

bool UsbGamepad::unusableTooLong(uint32_t now) const {
  // Zero means service() has not run yet, so there is nothing to measure from.
  return lastAliveAt_ && now - lastAliveAt_ >= kUnusableArmsWakeMs;
}

void UsbGamepad::service(uint32_t now) {
  // Start the liveness clock on the first tick. Everything below measures how
  // long we have been unusable, and boot is where that window opens — a board
  // that came up into a sleeping host has been unusable since power-on.
  if (!lastAliveAt_) lastAliveAt_ = now ? now : 1;

  // Finish a running detach pulse: reattach and let the host enumerate us
  // from scratch, exactly as a physical replug would.
  if (reattachAt_) {
    if (now - reattachAt_ >= kDetachPulseMs) {
      forceUsbConnect();
      reattachAt_ = 0;
    }
    return;
  }

  // SOF freshness monitor — the shared heartbeat detector. GINTSTS.SOF
  // latches even while masked; sample and re-arm it every kSofStaleMs
  // (write-1-to-clear touches only this bit). This is the ONE consumer of
  // the latch: recovery and the asleep verdict both read sofFresh_, because
  // two independent clear-and-watch cycles would destroy each other's
  // evidence.
  if (!sofCheckAt_ || now - sofCheckAt_ >= kSofStaleMs) {
    sofCheckAt_ = now ? now : 1;
    sofFresh_ = (USB0.gintsts & USB_SOF_M) != 0;
    USB0.gintsts = USB_SOF_M;
    // Cheap insurance against a future core re-arming the bit (today nothing
    // does after dcd_init — see blindDcdToSuspend).
    blindDcdToSuspend();
  }

  // "Configured" alone is not proof of life: a bus that dies into a held
  // reset leaves tud_mounted() true with no event ever posted (see the header
  // note). Only a configured device on a bus with a heartbeat is healthy —
  // on a heartbeatless one we must fall through, or the asleep bookkeeping
  // below (sawSuspend_, the wake-fallback timer) would be reset every loop.
  if (tud_mounted() && sofFresh_) {  // configured and talking: nothing to recover
    everMounted_ = true;
    lastAliveAt_ = now ? now : 1;
    sawSuspend_ = false;
    resumedAt_ = 0;
    wakeSignaledAt_ = 0;
    lastPulseAt_ = 0;
    return;
  }
  // Never enumerated, and still inside the window where that is just a host
  // which has not got to us yet: not our gap to fix. Past the window we fall
  // through instead of returning, which is what lets a bridge that reset
  // mid-sleep reach the wake-fallback below (see kUnusableArmsWakeMs).
  if (!everMounted_ && !unusableTooLong(now)) return;

  if (sofFresh_) {
    // Host demonstrably awake but we are still unconfigured. With the
    // suspend interrupt masked this is rare — a reset the host started but
    // never followed with configuration — but the host may still think we
    // never left, so nobody will re-enumerate us unless we make them. Keep
    // pulsing until it takes — a resuming PC can debounce away the first
    // attempt, and this state only exists while recovery is needed.
    if (!sawSuspend_) return;
    if (!resumedAt_) {
      resumedAt_ = now ? now : 1;
    } else if (now - resumedAt_ >= kReenumGraceMs &&
               (!lastPulseAt_ || now - lastPulseAt_ >= kPulseRetryMs)) {
      lastPulseAt_ = now ? now : 1;
      detach(now);
    }
    return;
  }

  // Bus quiet after we were mounted: the host is asleep. Either in proper L2
  // suspend (SUSPSTS set — the first phase), or with its controller powered
  // down outright (deeper platform states some boards enter ~30 min in,
  // where the idle-J line condition SUSPSTS watches for disappears), or the
  // cable is gone — without VBUS sensing those are one state, and everything
  // done here is harmless on a dead cable.
  sawSuspend_ = true;
  resumedAt_ = 0;
  if (wakeSignaledAt_ && now - wakeSignaledAt_ >= kWakeFallbackMs) {
    detach(now);
  }
}

void UsbGamepad::detach(uint32_t now) {
  forceUsbDisconnect();
  reattachAt_ = now ? now : 1;
  // A pulse supersedes any pending wake fallback: the bus idles again during
  // the host's attach debounce, and a stale stamp would fire a second pulse
  // right into the enumeration it is waiting on.
  wakeSignaledAt_ = 0;
}

bool UsbGamepad::remoteWakeup() {
  if (reattachAt_ || !suspended()) return false;
  if (USB0.dsts & USB_SUSPSTS_M) {
    // Bus in proper L2 suspend: resume signalling has a listener upstream.
    // tud_remote_wakeup() verifies state the fake unplug already destroyed,
    // so drive it at the register level.
    USB0.dctl |= USB_RMTWKUPSIG_M;
    delay(kResumeSignalMs);
    USB0.dctl &= ~USB_RMTWKUPSIG_M;
    // Keep the *first* press's stamp: a held or mashed Guide button re-enters
    // here every few ms (SUSPSTS flickers as the bus re-idles), and
    // restamping would push the detach fallback out forever.
    if (!wakeSignaledAt_) {
      uint32_t now = millis();
      wakeSignaledAt_ = now ? now : 1;
    }
  } else {
    // Bus quiet but not in L2: the host powered its controller down (deeper
    // platform sleep) and resume signalling has no receiver. A connect
    // change is the one event port hardware still watches in every
    // wake-armed state — go straight to the detach pulse.
    detach(millis());
  }
  return true;
}

bool UsbGamepad::ready() { return s_xusb ? xusb::ready() : hid_.ready(); }

bool UsbGamepad::suspended() {
  if (reattachAt_) return false;
  // The heartbeat is the whole verdict; TinyUSB's flags are deliberately not
  // consulted. Its view goes stale in both directions on this core: suspend
  // arrives as "unplugged" (mounted lost while the host merely sleeps), and a
  // configured device whose bus dies into a never-completed reset gets no
  // event at all, staying mounted forever (see the header note). The old
  // mounted-first check here hit that second case after a deep platform
  // transition briefly revived the bus and re-enumerated us ~30 min into
  // sleep: it then called the sleeping PC awake indefinitely — green LED,
  // wake disarmed (issue #4, the >30 min reports).
  //
  // SUSPSTS still earns its keep as the fast path: it reacts 3 ms after the
  // bus idles, where the SOF window takes up to two sampling periods.
  if (everMounted_ && (!sofFresh_ || (USB0.dsts & USB_SUSPSTS_M))) return true;
  // Backstop for the two states the heartbeat cannot speak for — a bridge
  // that reset while the host slept (everMounted_ false, and no host awake to
  // ever set it) and a latch that reads alive while nothing works. Costs
  // nothing on a healthy bus: the clock above is restamped every tick.
  return unusableTooLong(millis());
}

bool UsbGamepad::sendInput(const uint8_t* report16) {
  if (s_xusb) return xusb::sendReport16(report16);
  return hid_.SendReport(REPORT_ID_INPUT, report16, INPUT_LEN, 20);
}

uint16_t UsbGamepad::_onGetDescriptor(uint8_t* buffer) {
  memcpy(buffer, kReportDescriptor, sizeof(kReportDescriptor));
  return sizeof(kReportDescriptor);
}

void UsbGamepad::_onOutput(uint8_t report_id, const uint8_t* buffer,
                           uint16_t len) {
  if (report_id != REPORT_ID_RUMBLE) return;
  size_t n = len < RUMBLE_LEN ? len : RUMBLE_LEN;
  for (size_t i = 0; i < RUMBLE_LEN; ++i)
    rumble_[i] = i < n ? buffer[i] : 0;
  rumbleNew_ = true;
}

bool UsbGamepad::takeRumble(uint8_t out[RUMBLE_LEN]) {
  if (s_xusb) return xusb::takeRumble(out);
  if (!rumbleNew_) return false;
  rumbleNew_ = false;
  for (size_t i = 0; i < RUMBLE_LEN; ++i) out[i] = rumble_[i];
  return true;
}
