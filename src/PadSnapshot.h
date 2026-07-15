// PadSnapshot.h — a decoded, display-ready frame of controller state.
//
// The UI layer consumes only this struct, so it never has to touch NimBLE or
// the raw Xbox report format.
#pragma once

#include <Arduino.h>

struct PadSnapshot {
  bool connected = false;
  bool receiving = false;   // connected AND first input report has arrived

  // Face + shoulder + stick-click + centre buttons
  bool a = false, b = false, x = false, y = false;
  bool lb = false, rb = false;
  bool ls = false, rs = false;
  bool view = false, menu = false, xbox = false, share = false;

  // D-pad
  bool up = false, down = false, left = false, right = false;

  // Analog, normalised for display
  float lx = 0, ly = 0;     // left stick,  -1..+1 (up = +1)
  float rx = 0, ry = 0;     // right stick, -1..+1 (up = +1)
  float lt = 0, rt = 0;     // triggers,     0..+1

  // Telemetry
  uint8_t battery = 255;    // 0..100 percent, 255 = unknown
  bool batteryKnown = false;
  uint16_t rateHz = 0;      // input reports per second
  int8_t rssi = 0;          // link RSSI in dBm, 0 = unknown
  uint16_t connItvlUnits = 0; // BLE connection interval, units of 1.25 ms (0 = unknown)
  String addr;              // controller MAC, e.g. "AA:BB:CC:DD:EE:FF"

  // Convenience: any button currently held (for the "held" list on values page)
  bool anyButton() const {
    return a || b || x || y || lb || rb || ls || rs ||
           view || menu || xbox || share || up || down || left || right;
  }
};
