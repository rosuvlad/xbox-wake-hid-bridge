// AppState.h — the screens the device can show and the live sub-pages.
#pragma once

#include <Arduino.h>

enum class Screen : uint8_t {
  Splash,        // boot logo
  OobeStep1,     // "Turn your controller on"
  OobeStep2,     // "Hold the Pair button"
  Found,         // link established, syncing first report
  Paired,        // first-time success celebration (saves bond)
  Reconnecting,  // known pad, waiting for it to come back
  Live,          // connected — shows a LivePage
  Diagnostics,   // long-both: RSSI / MAC / uptime
  ForgetConfirm, // long-right: confirm before erasing the bond
};

enum class LivePage : uint8_t {
  Values = 0,    // precise numeric readout (default)
  Pad    = 1,    // stylised controller diagram
  Bridge = 2,    // BLE + USB + PC + performance dashboard
  _count = 3,
};
