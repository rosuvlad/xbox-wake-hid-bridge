// LedPattern.h — the headless UX's state -> colour mapping, as pure functions.
//
// Deliberately free of Arduino, NimBLE and TinyUSB types: time arrives as a
// parameter rather than from millis(), and nothing here touches a pin. That
// keeps the whole mapping unit-testable on the host (see test/test_led_pattern)
// and leaves UiLed as a thin adapter that supplies the clock and drives the LED.
//
// Colours here are "logical" — full 0..255 range. The single global brightness
// ceiling (LED_BRIGHTNESS) is applied once by UiLed at the write, so patterns
// express *relative* intensity and never have to know how bright the board is.
//
// Header-only and inline, matching ControllerLink.h / BridgeState.h.
#pragma once

#include <stdint.h>

struct Rgb {
  uint8_t r, g, b;
  // An explicit constructor (rather than default member initializers) keeps Rgb
  // brace-initializable under the Arduino core's gnu++11, where a struct with
  // in-class initializers is no longer an aggregate.
  constexpr Rgb(uint8_t r_ = 0, uint8_t g_ = 0, uint8_t b_ = 0) : r(r_), g(g_), b(b_) {}
  bool operator==(const Rgb& o) const { return r == o.r && g == o.g && b == o.b; }
  bool operator!=(const Rgb& o) const { return !(*this == o); }
};

namespace led {

// Palette. Green/amber/red carry the same meanings as the TFT status bar.
constexpr Rgb kOff{0, 0, 0};
constexpr Rgb kWhite{255, 255, 255};
constexpr Rgb kBlue{0, 80, 255};
constexpr Rgb kGreen{0, 255, 40};
constexpr Rgb kAmber{255, 140, 0};
constexpr Rgb kOrange{255, 60, 0};
constexpr Rgb kRed{255, 0, 0};
constexpr Rgb kCyan{0, 255, 255};

// How long a rumble report keeps the LED orange. Matches the TFT's own window
// (Ui.cpp livePad/liveBridge) so both backends feel the same.
constexpr uint32_t kRumbleHoldMs = 400;

// Boot-health readout timing. Flashes are long enough to count by eye without
// the train dragging on, and the gap is unmistakably longer than the flash so
// "where does the count restart" never needs guessing.
constexpr uint32_t kHealthFlashMs = 200;
constexpr uint32_t kHealthGapMs = 1100;

// Counts above this render as this many flashes. Nobody counts eleven blinks
// correctly, and past a handful the exact number stops changing the answer:
// the board is browning out, full stop.
constexpr uint8_t kHealthFlashCap = 9;

// ---------------------------------------------------------------------------
// Scalar helpers
// ---------------------------------------------------------------------------

// v scaled by s/255. scale8(v, 255) == v, scale8(v, 0) == 0.
inline uint8_t scale8(uint8_t v, uint8_t s) {
  return (uint8_t)(((uint16_t)v * (uint16_t)s) / 255);
}

// Every channel scaled by level/255.
inline Rgb dim(Rgb c, uint8_t level) {
  return Rgb{scale8(c.r, level), scale8(c.g, level), scale8(c.b, level)};
}

// Rec.601 luma. Weights sum to 256 so pure white folds to exactly 255 — this is
// how a mono LED renders a colour it cannot show.
inline uint8_t luma(Rgb c) {
  return (uint8_t)(((uint32_t)c.r * 77 + (uint32_t)c.g * 151 + (uint32_t)c.b * 28) >> 8);
}

// 0..1 -> 0..255, clamped. Tolerates NaN by treating it as 0.
inline uint8_t rampLevel(float frac) {
  if (!(frac > 0.0f)) return 0;
  if (frac >= 1.0f) return 255;
  return (uint8_t)(frac * 255.0f);
}

// ---------------------------------------------------------------------------
// Waveforms — pure functions of time, so any frame can be rendered in isolation
// ---------------------------------------------------------------------------

// Triangle 0 -> 255 -> 0 across periodMs. Unsigned modulo means this stays
// correct across the millis() rollover.
inline uint8_t breathe(uint32_t nowMs, uint32_t periodMs) {
  const uint32_t half = periodMs / 2;
  if (half == 0) return 255;
  const uint32_t p = nowMs % periodMs;
  const uint32_t up = (p < half) ? p : (periodMs - p);
  const uint32_t level = (up * 255) / half;
  return (uint8_t)(level > 255 ? 255 : level);
}

// True for the first dutyPct of each periodMs window.
inline bool blink(uint32_t nowMs, uint32_t periodMs, uint8_t dutyPct) {
  if (periodMs == 0 || dutyPct >= 100) return true;
  if (dutyPct == 0) return false;
  return (nowMs % periodMs) < (uint32_t)(((uint64_t)periodMs * dutyPct) / 100);
}

// `count` on/off pulses of flashMs each, then dark for the rest of periodMs.
inline bool flashN(uint32_t nowMs, uint32_t periodMs, uint8_t count, uint32_t flashMs) {
  if (periodMs == 0 || flashMs == 0 || count == 0) return false;
  const uint32_t p = nowMs % periodMs;
  const uint32_t train = (uint32_t)count * flashMs * 2;  // each pulse is on+off
  if (p >= train) return false;
  return ((p / flashMs) % 2) == 0;
}

// ---------------------------------------------------------------------------
// State -> colour
// ---------------------------------------------------------------------------

// The live view's inputs, lifted out of BridgeState so this header stays pure.
struct LiveInputs {
  bool usbReady = false;
  bool usbSuspended = false;
  bool rumbleActive = false;      // any motor non-zero in the last report
  uint8_t rumbleMax = 0;          // strongest motor, 0..255
  uint32_t sinceRumbleMs = 0;     // ms since that report landed
};

inline Rgb splash(float progress) { return dim(kWhite, rampLevel(progress)); }

// "Turn your controller on" — a slow, patient breathe.
inline Rgb oobeStep1(uint32_t nowMs) {
  return dim(kBlue, 40 + scale8(breathe(nowMs, 1400), 215));
}

// "Hold the pair button" — busier, because we are actively scanning.
inline Rgb oobeStep2(uint32_t nowMs) { return blink(nowMs, 200, 50) ? kBlue : kOff; }

inline Rgb found(uint32_t) { return kBlue; }

// First-time success. Free-running so it needs no phase reference.
inline Rgb paired(uint32_t nowMs) { return flashN(nowMs, 1600, 3, 120) ? kGreen : kOff; }

inline Rgb reconnecting(uint32_t nowMs) {
  return dim(kAmber, 25 + scale8(breathe(nowMs, 2000), 230));
}

inline Rgb waking(uint32_t nowMs) { return blink(nowMs, 120, 50) ? kWhite : kOff; }

// USB-identity switch confirmation: triple-flash the face the bridge is
// rebooting into. Blue = Xbox 360 / XInput (Windows), green = Series (Linux).
inline Rgb identitySwitch(bool xusb, uint32_t nowMs) {
  return flashN(nowMs, 1600, 3, 160) ? (xusb ? kBlue : kGreen) : kOff;
}

inline Rgb diagnostics(uint32_t) { return kCyan; }

inline Rgb forgetConfirm(uint32_t nowMs) { return blink(nowMs, 300, 50) ? kRed : kOff; }

// Hold-to-forget: red deepening as the commit approaches.
inline Rgb forgetHold(float progress) {
  return dim(kRed, 20 + scale8(rampLevel(progress), 235));
}

// Boot-health readout (headless): how many times this board has reset badly.
//
// The counter it renders exists because a bridge that resets while the PC
// sleeps leaves no trace anywhere else (see recordBootHealth in main.cpp), and
// the serial line that prints it is unreachable on the boards most likely to
// need it — the SuperMini has a single native-USB port and no UART at all, and
// on every headless board that port is busy being an Xbox pad. So the LED has
// to be able to say it.
//
// `elapsedMs` is time since the readout was *asked for*, not free-running
// millis(): a train you join halfway through cannot be counted, so the phase
// has to be anchored to the button press.
//
//   one slow green breath  = nothing has ever gone wrong here
//   N red flashes          = N brownouts (power delivery)
//   N blue flashes         = N faults (crash or watchdog)
//
// Both trains play in that order when both are non-zero, separated by a gap.
inline Rgb bootHealth(uint32_t elapsedMs, uint8_t brownouts, uint8_t faults) {
  const uint8_t nb = brownouts < kHealthFlashCap ? brownouts : kHealthFlashCap;
  const uint8_t nf = faults < kHealthFlashCap ? faults : kHealthFlashCap;
  if (nb == 0 && nf == 0) return dim(kGreen, breathe(elapsedMs, 2000));

  const uint32_t redTrain = (uint32_t)nb * kHealthFlashMs * 2;   // each pulse is on+off
  const uint32_t blueTrain = (uint32_t)nf * kHealthFlashMs * 2;
  // One trailing gap always; a second one only when both trains are present
  // and need separating.
  const uint32_t period =
      redTrain + blueTrain + kHealthGapMs * ((nb && nf) ? 2 : 1);

  uint32_t p = elapsedMs % period;
  if (p < redTrain) return ((p / kHealthFlashMs) % 2) == 0 ? kRed : kOff;
  p -= redTrain;
  if (nb) {  // the gap that closes the red train
    if (p < kHealthGapMs) return kOff;
    p -= kHealthGapMs;
  }
  if (p < blueTrain) return ((p / kHealthFlashMs) % 2) == 0 ? kBlue : kOff;
  return kOff;  // trailing gap, then the whole readout repeats
}

// LED wiring check (UI_ALIGN_TEST). Cycling all three channels proves both the
// pin and the colour order; a dead LED means LED_PIN is wrong.
inline Rgb alignTest(uint32_t nowMs) {
  switch ((nowMs / 600) % 3) {
    case 0: return kRed;
    case 1: return kGreen;
    default: return kBlue;
  }
}

// The connected view. There are no pages without a screen, so all three TFT
// live pages collapse here. Ordered by what a glance most needs to know.
inline Rgb live(const LiveInputs& in, uint32_t nowMs) {
  // 1. The PC is actively driving the motors — brightness tracks the strongest.
  if (in.rumbleActive && in.sinceRumbleMs < kRumbleHoldMs) {
    return dim(kOrange, 100 + scale8(in.rumbleMax, 155));
  }
  // 2. PC asleep, bridge armed and holding the BLE link. This is the state the
  //    device spends all night in, so its *peak* stays below the streaming
  //    level — amber reads brighter than green at equal scale, so this ceiling
  //    is lower than it looks (test_live_suspended_is_dimmer_than_streaming).
  if (in.usbSuspended) {
    return dim(kAmber, 15 + scale8(breathe(nowMs, 2600), 75));
  }
  // 3. Streaming to an awake PC — the normal, boring, good state.
  if (in.usbReady) return dim(kGreen, 140);
  // 4. Pad is linked but the host has not enumerated us (unplugged, or a port
  //    that does not power in sleep — the classic wake failure).
  return dim(kGreen, 20 + scale8(breathe(nowMs, 1800), 120));
}

}  // namespace led
