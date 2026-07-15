// BridgeState.h — the PC-facing (USB) half of the bridge, for the UI + logic.
//
// Mirrors PadSnapshot (the controller-facing half). The UI consumes only this;
// it never touches TinyUSB.
#pragma once

#include <Arduino.h>

struct BridgeState {
  bool usbReady = false;      // host has configured our HID interface
  bool usbSuspended = false;  // bus suspended by host (PC likely asleep)

  uint16_t usbHz = 0;         // input reports sent to the PC per second
  uint16_t latencyMs = 0;     // last BLE-receive -> USB-send delay (coarse, ms)

  // Last rumble the PC asked for (0..255 per motor). lastRumbleMs = when.
  uint8_t rmbLeft = 0, rmbRight = 0, rmbLT = 0, rmbRT = 0;
  uint32_t lastRumbleMs = 0;

  bool rumbleActive() const {
    return (rmbLeft || rmbRight || rmbLT || rmbRT);
  }
};
