// UsbGamepad.cpp
#include "UsbGamepad.h"

#include <string.h>

#include "soc/usb_reg.h"
#include "soc/usb_struct.h"

// TinyUSB device-state probes (declared here like tud_remote_wakeup was: the
// Arduino core links TinyUSB but does not expose tusb.h to sketches).
extern "C" bool tud_mounted(void);
extern "C" bool tud_suspended(void);

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
// The DWC-OTG controller underneath is fine, so the bridge reads it directly:
//   suspend  = DSTS.SUSPSTS (bus idle >3 ms) after we were once configured
//   wake     = drive DCTL.RMTWKUPSIG (resume K-state) ourselves
//   recovery = a soft-disconnect pulse (DCTL.SFTDISCON) once the bus is active
//              again, so the host re-enumerates the torn-down device — the
//              same thing a physical replug does.

// Resume-signalling hold. The spec window is 1-15 ms; the driver's own
// dcd_remote_wakeup() holds for one FreeRTOS tick, which can round down to
// ~0 ms and be missed by the host.
static const uint32_t kResumeSignalMs = 5;
// A host that resets us itself after resume gets this long before we force
// re-enumeration. Counted from *verified SOFs* (see the probe below), so the
// host controller is demonstrably up when it expires.
static const uint32_t kReenumGraceMs = 1000;
// Soft-disconnect pulse width. Generous on purpose: a PC mid-resume debounces
// port changes, and a short blip can be coalesced away entirely.
static const uint32_t kDetachPulseMs = 300;
// Re-enumeration is retried at this pace for as long as the pathological
// state persists (SOFs flowing + still unconfigured). Each retry restarts
// host-side enumeration, so pacing only has to outlast one attempt.
static const uint32_t kPulseRetryMs = 2500;
// Bus still idle this long after remote wakeup means the host ignored it
// (wakeup not armed for us). A disconnect pulse also wakes a wake-armed port,
// so fall back to that.
static const uint32_t kWakeFallbackMs = 1000;

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

void UsbGamepad::begin() {
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
}

void UsbGamepad::service(uint32_t now) {
  // Finish a running detach pulse: reattach and let the host enumerate us
  // from scratch, exactly as a physical replug would.
  if (reattachAt_) {
    if (now - reattachAt_ >= kDetachPulseMs) {
      USB0.dctl &= ~USB_SFTDISCON_M;
      reattachAt_ = 0;
    }
    return;
  }

  if (tud_mounted()) {  // configured and talking: nothing to recover
    everMounted_ = true;
    sawSuspend_ = false;
    resumedAt_ = 0;
    wakeSignaledAt_ = 0;
    lastPulseAt_ = 0;
    sofArmed_ = false;
    return;
  }
  if (!everMounted_) return;  // never enumerated yet: not our gap to fix

  if (USB0.dsts & USB_SUSPSTS_M) {
    // Bus idle after we were mounted: the host fell asleep, or the cable is
    // gone — without VBUS sensing the S3 cannot tell, and everything done
    // here is harmless on a dead cable, so both read as "host asleep".
    sawSuspend_ = true;
    resumedAt_ = 0;
    sofArmed_ = false;
    if (wakeSignaledAt_ && now - wakeSignaledAt_ >= kWakeFallbackMs) {
      detach(now);
    }
    return;
  }
  if (!sawSuspend_) return;

  // SUSPSTS dropping is not proof the host is awake — our own resume
  // signalling clears it too. SOFs are proof: a running host emits one every
  // millisecond, and GINTSTS.SOF latches even while masked. Clear the latch
  // once (write-1-to-clear touches only this bit), and only a re-assert
  // counts as a live host.
  if (!sofArmed_) {
    USB0.gintsts = USB_SOF_M;
    sofArmed_ = true;
    return;
  }
  if (!(USB0.gintsts & USB_SOF_M)) return;  // no fresh SOF yet
  if (!resumedAt_) resumedAt_ = now ? now : 1;

  // Host verified awake but we are still unconfigured: the fake unplug
  // destroyed our device state, and the host thinks we never left, so nobody
  // will re-enumerate us unless we make them. Keep pulsing until it takes —
  // a resuming PC can debounce away the first attempt, and this state only
  // exists while recovery is needed.
  if (now - resumedAt_ >= kReenumGraceMs &&
      (!lastPulseAt_ || now - lastPulseAt_ >= kPulseRetryMs)) {
    lastPulseAt_ = now ? now : 1;
    detach(now);
  }
}

void UsbGamepad::detach(uint32_t now) {
  USB0.dctl |= USB_SFTDISCON_M;  // drop the D+ pull-up: host sees a disconnect
  reattachAt_ = now ? now : 1;
  // A pulse supersedes any pending wake fallback: the bus idles again during
  // the host's attach debounce, and a stale stamp would fire a second pulse
  // right into the enumeration it is waiting on.
  wakeSignaledAt_ = 0;
}

bool UsbGamepad::remoteWakeup() {
  if (reattachAt_ || !suspended()) return false;
  // tud_remote_wakeup() verifies state the fake unplug already destroyed, so
  // drive the resume signalling at the register level: the controller is
  // still physically attached and the bus is in L2 suspend.
  USB0.dctl |= USB_RMTWKUPSIG_M;
  delay(kResumeSignalMs);
  USB0.dctl &= ~USB_RMTWKUPSIG_M;
  // Keep the *first* press's stamp: a held or mashed Guide button re-enters
  // here every few ms (SUSPSTS flickers as the bus re-idles), and restamping
  // would push the detach fallback out forever.
  if (!wakeSignaledAt_) {
    uint32_t now = millis();
    wakeSignaledAt_ = now ? now : 1;
  }
  return true;
}

bool UsbGamepad::ready() { return hid_.ready(); }

bool UsbGamepad::suspended() {
  if (!everMounted_ || reattachAt_) return false;
  if (!(USB0.dsts & USB_SUSPSTS_M)) return false;
  // Second arm keeps this correct on a core whose driver reports suspend
  // properly (device stays mounted, tud_suspended() goes true).
  return !tud_mounted() || tud_suspended();
}

bool UsbGamepad::sendInput(const uint8_t* report16) {
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
  if (!rumbleNew_) return false;
  rumbleNew_ = false;
  for (size_t i = 0; i < RUMBLE_LEN; ++i) out[i] = rumble_[i];
  return true;
}
