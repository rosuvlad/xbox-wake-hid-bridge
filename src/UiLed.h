// UiLed.h — the headless backend: the entire UI is one LED.
//
// Mirrors Ui's public surface exactly, so main.cpp's state machine dispatches to
// either backend without knowing which it has (see Ux.h). Screens that need a
// display collapse to a colour; the three live pages collapse to one, because
// there are no pages to cycle without a screen.
//
// This class is only the adapter: it owns the clock and the pin. The actual
// state -> colour mapping lives in LedPattern.h, which is pure and tested.
#pragma once

#include <Arduino.h>

#include "AppState.h"
#include "BridgeState.h"
#include "Config.h"
#include "LedPattern.h"
#include "PadSnapshot.h"

class UiLed {
 public:
  void begin();

  // Headless: cycles red/green/blue to prove LED_PIN and the colour order.
  void alignTest();

  void setBridge(const BridgeState& b) { bridge_ = b; }

  // Boot + OOBE
  void splash(float progress);
  void oobeStep1(uint32_t nowMs);
  void oobeStep2(uint32_t nowMs);
  void found(uint32_t nowMs);
  void paired(const String& addr);

  // Connected — all three collapse to the same pattern.
  void liveValues(const PadSnapshot& s);
  void livePad(const PadSnapshot& s);
  void liveBridge(const PadSnapshot& s);

  void waking();

  // USB-identity switch confirmation, shown until the reboot that applies it.
  void identitySwitch(bool xusb, uint32_t nowMs);

  void reconnecting(uint32_t nowMs, bool showPairHint);
  void diagnostics(const PadSnapshot& s, uint32_t uptimeMs);
  void forgetConfirm();

  // Headless-only: hold-to-forget progress, 0..1. Rendered as an override in
  // main.cpp the same way waking() overrides the current screen.
  void forgetHold(float progress);

 private:
  void show(Rgb c);
  void live();

  BridgeState bridge_;
  Rgb last_;
  bool haveLast_ = false;
};
