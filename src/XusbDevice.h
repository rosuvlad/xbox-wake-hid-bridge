// XusbDevice.h — the bridge's second USB face: a wired Xbox 360 controller
// (XUSB / XInput), for Windows.
//
// Windows classifies Xbox controllers per transport+PID, and the bridge's
// native identity — the Series pad's *Bluetooth* PID carried over USB — is in
// no Windows-side database: the inbox INFs bind it as a generic DirectInput
// gamepad, Chromium's mapper for 045e:0b13 is Bluetooth-gated, and Steam
// hands anything with Microsoft's VID to its XInput-only Xbox path, which
// finds nothing and deliberately ignores the raw HID node (issue #6).
// Enumerating as the wired 360 pad (045E:028E) instead binds Windows' inbox
// XInput driver by VID/PID alone and makes the bridge a first-class
// controller in Steam and every game.
//
// XUSB is a vendor protocol, not HID, so this is a TinyUSB application class
// driver (usbd_app_driver_get_cb) speaking the wire format directly: one
// interface FF/5D/01 with interrupt IN 0x81 (20-byte input reports) and OUT
// 0x02 (rumble/LED commands), descriptors byte-identical to ArduinoXInput's
// field-proven set.
#pragma once

#include <Arduino.h>

namespace xusb {

// Register the XUSB interface with the Arduino USB stack. Call after the
// USB identity setters and before USB.begin().
void begin();

// Host configured us and the IN endpoint is free for the next report.
bool ready();

// Translate the pad's 16-byte BLE input report into a 20-byte XUSB report
// and send it. Returns false when not ready or the report is malformed.
bool sendReport16(const uint8_t* report16);

// If the host sent a rumble command since the last call, write it into out8
// in the pad's own BLE rumble format (report id 3 payload) and return true.
bool takeRumble(uint8_t out8[8]);

}  // namespace xusb
