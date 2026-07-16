// HoldButton.h — press-and-hold detector, as pure logic.
//
// Headless builds have exactly one button (BOOT), so "forget the controller and
// re-pair" is a deliberate hold rather than a click. This tracks that hold and
// exposes its progress, which the LED renders as a deepening red ramp.
//
// Why not OneButton (already vendored, used by the TFT build)? Its 2.0.3 API has
// setPressTicks() but no way to read how long the current press has lasted, so
// it cannot drive a 0..1 progress ramp — it can only tell you a long press
// already happened. This is ~20 lines and gives the ramp for free.
//
// Debouncing is implicit: a bounce merely restarts the timer, and a commit needs
// FORGET_HOLD_MS of *continuous* contact. Bounces occur at the press and release
// edges, never mid-hold, so they cannot manufacture a hold that did not happen.
//
// No Arduino types: the caller feeds it the pin state and the clock, which keeps
// it unit-testable on the host (see test/test_hold_button).
#pragma once

#include <stdint.h>

class HoldButton {
 public:
  explicit HoldButton(uint32_t holdMs) : holdMs_(holdMs) {}

  // Feed the debounced-by-physics pressed state every loop. Returns true exactly
  // once per press: on the tick the hold threshold is crossed.
  bool update(bool pressed, uint32_t nowMs) {
    if (!pressed) {
      down_ = false;
      fired_ = false;
      heldMs_ = 0;
      return false;
    }
    if (!down_) {
      down_ = true;
      fired_ = false;
      downAt_ = nowMs;
    }
    // Unsigned subtraction, so this stays correct across the millis() rollover
    // at ~49 days rather than firing spuriously.
    heldMs_ = nowMs - downAt_;
    if (!fired_ && heldMs_ >= holdMs_) {
      fired_ = true;
      return true;
    }
    return false;
  }

  bool isDown() const { return down_; }
  uint32_t heldMs() const { return heldMs_; }

  // 0..1, clamped. Drives the LED ramp.
  float progress() const {
    if (holdMs_ == 0) return 1.0f;
    if (heldMs_ >= holdMs_) return 1.0f;
    return (float)heldMs_ / (float)holdMs_;
  }

 private:
  uint32_t holdMs_;
  uint32_t downAt_ = 0;
  uint32_t heldMs_ = 0;
  bool down_ = false;
  bool fired_ = false;
};
