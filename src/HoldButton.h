// HoldButton.h — press-and-hold detector, as pure logic.
//
// Headless builds have exactly one button (BOOT), so "forget the controller and
// re-pair" is a deliberate hold rather than a click. This tracks that hold and
// exposes its progress, which the LED renders as a deepening red ramp.
//
// The click that *isn't* a hold is the other half of the same button: releasing
// early reports a click (takeClick), which the headless build spends on the
// boot-health readout. The two are mutually exclusive by construction — a press
// long enough to fire the hold never also reports a click.
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
  // Shortest press that counts as a click. Comfortably above contact-bounce
  // durations (single-digit ms) and far below a deliberate tap.
  static const uint32_t kClickMinMs = 30;

  explicit HoldButton(uint32_t holdMs) : holdMs_(holdMs) {}

  // Feed the debounced-by-physics pressed state every loop. Returns true exactly
  // once per press: on the tick the hold threshold is crossed.
  bool update(bool pressed, uint32_t nowMs) {
    if (!pressed) {
      // A press that ends before the threshold is a click. `fired_` is what
      // keeps a completed hold from also reporting one on release, and the
      // floor is what keeps a contact bounce from manufacturing one: the same
      // press-edge bounce that legitimately restarts the hold timer would
      // otherwise look like an (extremely fast) click on its way through.
      // Measured from the press stamp rather than heldMs_, which only advances
      // on pressed ticks and is therefore stale by however long the gap to this
      // release was. Unsigned, so the millis() rollover is a non-event.
      if (down_ && !fired_ && nowMs - downAt_ >= kClickMinMs) clicked_ = true;
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

  // True once per completed short press, and consumed by reading it — the same
  // fire-once contract as update(), so a caller that polls both cannot act on
  // the same click twice.
  bool takeClick() {
    const bool c = clicked_;
    clicked_ = false;
    return c;
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
  bool clicked_ = false;
};
