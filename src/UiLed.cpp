#include "UiLed.h"

#if LED_KIND == LED_MONO
// A plain LED has no colour, so patterns separate by brightness and rhythm
// instead. LEDC gives us the breathe and the hold ramp.
static const uint8_t kPwmChannel = 0;
static const uint32_t kPwmFreqHz = 5000;  // well above flicker, well under LEDC's limit
static const uint8_t kPwmBits = 8;        // matches the 0..255 the patterns speak
#endif

void UiLed::begin() {
#if LED_KIND == LED_MONO
  ledcSetup(kPwmChannel, kPwmFreqHz, kPwmBits);
  ledcAttachPin(LED_PIN, kPwmChannel);
#endif
  // LED_RGB needs no setup: neopixelWrite() brings the RMT channel up lazily on
  // its first call.
  show(led::kOff);
}

// The one place a pixel is actually written.
void UiLed::show(Rgb c) {
  const Rgb out = led::dim(c, LED_BRIGHTNESS);

  // neopixelWrite() ends in rmtWriteBlocking() — it stalls the caller for the
  // length of the transfer. Callers are already frame-gated to ~30 Hz, and
  // skipping unchanged colours makes a steady state cost nothing at all, which
  // is what keeps this clear of the bridge's sub-2ms forwarding budget.
  if (haveLast_ && out == last_) return;
  last_ = out;
  haveLast_ = true;

#if LED_KIND == LED_MONO
  const uint8_t v = led::luma(out);
  ledcWrite(kPwmChannel, LED_ACTIVE_LOW ? 255 - v : v);
#else
  neopixelWrite(LED_PIN, out.r, out.g, out.b);
#endif
}

void UiLed::alignTest() { show(led::alignTest(millis())); }

void UiLed::splash(float progress) { show(led::splash(progress)); }
void UiLed::oobeStep1(uint32_t nowMs) { show(led::oobeStep1(nowMs)); }
void UiLed::oobeStep2(uint32_t nowMs) { show(led::oobeStep2(nowMs)); }
void UiLed::found(uint32_t nowMs) { show(led::found(nowMs)); }

// The MAC has nowhere to go on a single LED; the celebration is the whole point.
void UiLed::paired(const String&) { show(led::paired(millis())); }

// The pair hint is a screen affordance — headless, holding the button is the
// only control there is, so the amber breathe says everything it can.
void UiLed::reconnecting(uint32_t nowMs, bool) { show(led::reconnecting(nowMs)); }

void UiLed::waking() { show(led::waking(millis())); }

void UiLed::identitySwitch(bool xusb, uint32_t nowMs) {
  show(led::identitySwitch(xusb, nowMs));
}

// Unreachable headless: reaching it on the TFT needs both buttons at once, and
// the serial console is the better home for this anyway. Mapped so the surface
// stays identical and a future control can route here.
void UiLed::diagnostics(const PadSnapshot&, uint32_t) { show(led::diagnostics(millis())); }

// Also unreachable headless: the hold *is* the confirmation.
void UiLed::forgetConfirm() { show(led::forgetConfirm(millis())); }

void UiLed::forgetHold(float progress) { show(led::forgetHold(progress)); }

void UiLed::bootHealth(uint32_t elapsedMs, uint8_t brownouts, uint8_t faults) {
  show(led::bootHealth(elapsedMs, brownouts, faults));
}

void UiLed::live() {
  const uint32_t now = millis();

  led::LiveInputs in;
  in.usbReady = bridge_.usbReady;
  in.usbSuspended = bridge_.usbSuspended;
  in.rumbleActive = bridge_.rumbleActive();
  in.rumbleMax = max(max(bridge_.rmbLeft, bridge_.rmbRight),
                     max(bridge_.rmbLT, bridge_.rmbRT));
  in.sinceRumbleMs = now - bridge_.lastRumbleMs;
  show(led::live(in, now));
}

void UiLed::liveValues(const PadSnapshot&) { live(); }
void UiLed::livePad(const PadSnapshot&) { live(); }
void UiLed::liveBridge(const PadSnapshot&) { live(); }
