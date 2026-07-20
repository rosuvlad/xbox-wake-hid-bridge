// UsbGamepad.h — native-USB HID device that emulates an Xbox Wireless
// Controller (Series, model 1914) so the PC sees a real Xbox pad.
//
// The emulation is nearly transparent:
//   * INPUT  report id 1 (16 bytes) == the controller's own BLE HID report,
//     which we get back from the parser via xboxNotif.toArr().
//   * OUTPUT report id 3 (8 bytes)  == the Xbox rumble report, which we can
//     forward straight to the controller with Core::writeHIDReport().
#pragma once

#include <Arduino.h>
#include "USB.h"
#include "USBHID.h"

class UsbGamepad : public USBHIDDevice {
 public:
  static const uint8_t REPORT_ID_INPUT = 0x01;
  static const uint8_t REPORT_ID_RUMBLE = 0x03;
  static const size_t INPUT_LEN = 16;
  static const size_t RUMBLE_LEN = 8;

  void begin();               // register descriptor, set identity, start USB
  void service(uint32_t nowMs);  // suspend tracking + post-resume recovery
  bool ready();               // host configured + endpoint free
  bool suspended();           // bus suspended by host (PC asleep)
  bool sendInput(const uint8_t* report16);
  bool remoteWakeup();        // ask a suspended host to resume (wake the PC)

  // If the host sent a new rumble report since last call, copy its 8 bytes
  // into out and return true.
  bool takeRumble(uint8_t out[RUMBLE_LEN]);

  // USBHIDDevice overrides
  uint16_t _onGetDescriptor(uint8_t* buffer) override;
  void _onOutput(uint8_t report_id, const uint8_t* buffer, uint16_t len) override;

 private:
  void detach(uint32_t nowMs);  // start a soft-disconnect (re-enumeration) pulse

  USBHID hid_;
  volatile bool rumbleNew_ = false;
  volatile uint8_t rumble_[RUMBLE_LEN] = {0};

  // Suspend/wake recovery state (see the register-level rationale in the .cpp).
  bool everMounted_ = false;    // a host has configured us since boot
  bool sawSuspend_ = false;     // bus went quiet after that (host asleep/unplug)
  bool sofFresh_ = false;       // SOFs seen in the last sampling window
  uint32_t sofCheckAt_ = 0;     // last SOF latch sample-and-rearm
  uint32_t resumedAt_ = 0;      // host verified awake (fresh SOFs seen)
  uint32_t wakeSignaledAt_ = 0; // remote wakeup sent; awaiting bus resume
  uint32_t lastPulseAt_ = 0;    // last re-enumeration pulse (retry pacing)
  uint32_t reattachAt_ = 0;     // soft-disconnect pulse start (0 = idle)
};
