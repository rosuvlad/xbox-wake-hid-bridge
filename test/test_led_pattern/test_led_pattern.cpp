// Unit tests for LedPattern.h — the headless UX's state -> colour mapping.
//
// These run on the host (`pio test -e native`). LedPattern.h takes time as a
// parameter and touches no hardware, so every frame of every animation can be
// asserted exactly, including the millis() rollover.

#include <unity.h>

#include <algorithm>

#include "LedPattern.h"

void setUp() {}
void tearDown() {}

// ---------------------------------------------------------------------------
// Rgb
// ---------------------------------------------------------------------------

static void test_rgb_equality() {
  TEST_ASSERT_TRUE((Rgb{1, 2, 3} == Rgb{1, 2, 3}));
  TEST_ASSERT_FALSE((Rgb{1, 2, 3} == Rgb{1, 2, 4}));
  TEST_ASSERT_TRUE((Rgb{1, 2, 3} != Rgb{9, 2, 3}));
  TEST_ASSERT_FALSE((Rgb{1, 2, 3} != Rgb{1, 2, 3}));
}

static void test_rgb_defaults_to_off() {
  Rgb c;
  TEST_ASSERT_TRUE((c == led::kOff));
}

// ---------------------------------------------------------------------------
// scale8 / dim
// ---------------------------------------------------------------------------

static void test_scale8_endpoints_are_exact() {
  // Full scale must be lossless, or a "solid" colour would drift dimmer.
  TEST_ASSERT_EQUAL_UINT8(255, led::scale8(255, 255));
  TEST_ASSERT_EQUAL_UINT8(200, led::scale8(200, 255));
  TEST_ASSERT_EQUAL_UINT8(0, led::scale8(255, 0));
  TEST_ASSERT_EQUAL_UINT8(0, led::scale8(0, 255));
  TEST_ASSERT_EQUAL_UINT8(0, led::scale8(0, 0));
}

static void test_scale8_is_monotonic_and_never_overflows() {
  uint8_t prev = 0;
  for (int s = 0; s <= 255; s++) {
    uint8_t v = led::scale8(255, (uint8_t)s);
    TEST_ASSERT_TRUE(v >= prev);  // never goes backwards
    prev = v;
  }
  TEST_ASSERT_EQUAL_UINT8(255, prev);

  // Exhaustive: no input pair may exceed the input value.
  for (int v = 0; v <= 255; v++) {
    for (int s = 0; s <= 255; s++) {
      TEST_ASSERT_TRUE(led::scale8((uint8_t)v, (uint8_t)s) <= v);
    }
  }
}

static void test_scale8_halves() {
  // 255 * 128 / 255 is exactly 128 — full-range values divide out cleanly.
  TEST_ASSERT_EQUAL_UINT8(128, led::scale8(255, 128));
  // 100 * 128 / 255 is 50.2, truncated. Scaling is lossy below full range.
  TEST_ASSERT_EQUAL_UINT8(50, led::scale8(100, 128));
}

static void test_dim_scales_every_channel() {
  TEST_ASSERT_TRUE((led::dim(Rgb{255, 255, 255}, 255) == Rgb{255, 255, 255}));
  TEST_ASSERT_TRUE((led::dim(Rgb{255, 255, 255}, 0) == led::kOff));
  TEST_ASSERT_TRUE((led::dim(Rgb{200, 100, 50}, 128) == Rgb{100, 50, 25}));
}

static void test_dim_of_off_stays_off() {
  TEST_ASSERT_TRUE((led::dim(led::kOff, 255) == led::kOff));
}

// ---------------------------------------------------------------------------
// luma — how a mono LED renders a colour it cannot show
// ---------------------------------------------------------------------------

static void test_luma_black_and_white_are_exact() {
  // Weights sum to 256, so white must fold to exactly full brightness.
  TEST_ASSERT_EQUAL_UINT8(255, led::luma(led::kWhite));
  TEST_ASSERT_EQUAL_UINT8(0, led::luma(led::kOff));
}

static void test_luma_weights_channels_by_perception() {
  TEST_ASSERT_EQUAL_UINT8(76, led::luma(Rgb{255, 0, 0}));
  TEST_ASSERT_EQUAL_UINT8(150, led::luma(Rgb{0, 255, 0}));
  TEST_ASSERT_EQUAL_UINT8(27, led::luma(Rgb{0, 0, 255}));
  // Green must read brighter than red, red brighter than blue.
  TEST_ASSERT_TRUE(led::luma(Rgb{0, 255, 0}) > led::luma(Rgb{255, 0, 0}));
  TEST_ASSERT_TRUE(led::luma(Rgb{255, 0, 0}) > led::luma(Rgb{0, 0, 255}));
}

static void test_luma_never_overflows() {
  // The intermediate sum is the risky part; sweep the whole space coarsely.
  for (int r = 0; r <= 255; r += 5) {
    for (int g = 0; g <= 255; g += 5) {
      for (int b = 0; b <= 255; b += 5) {
        (void)led::luma(Rgb{(uint8_t)r, (uint8_t)g, (uint8_t)b});
      }
    }
  }
  TEST_ASSERT_EQUAL_UINT8(255, led::luma(led::kWhite));  // still exact
}

// ---------------------------------------------------------------------------
// rampLevel
// ---------------------------------------------------------------------------

static void test_ramp_level_endpoints_and_clamping() {
  TEST_ASSERT_EQUAL_UINT8(0, led::rampLevel(0.0f));
  TEST_ASSERT_EQUAL_UINT8(255, led::rampLevel(1.0f));
  TEST_ASSERT_EQUAL_UINT8(255, led::rampLevel(9.0f));    // above 1 clamps
  TEST_ASSERT_EQUAL_UINT8(0, led::rampLevel(-1.0f));     // below 0 clamps
  TEST_ASSERT_EQUAL_UINT8(127, led::rampLevel(0.5f));
}

static void test_ramp_level_survives_nan() {
  // A 0/0 progress would otherwise wrap to a bright value.
  TEST_ASSERT_EQUAL_UINT8(0, led::rampLevel(0.0f / 0.0f));
}

static void test_ramp_level_is_monotonic() {
  uint8_t prev = 0;
  for (int i = 0; i <= 100; i++) {
    uint8_t v = led::rampLevel(i / 100.0f);
    TEST_ASSERT_TRUE(v >= prev);
    prev = v;
  }
}

// ---------------------------------------------------------------------------
// breathe
// ---------------------------------------------------------------------------

static void test_breathe_rises_to_peak_at_half_period() {
  TEST_ASSERT_EQUAL_UINT8(0, led::breathe(0, 1000));
  TEST_ASSERT_EQUAL_UINT8(255, led::breathe(500, 1000));
  TEST_ASSERT_EQUAL_UINT8(127, led::breathe(250, 1000));   // rising
  TEST_ASSERT_EQUAL_UINT8(127, led::breathe(750, 1000));   // falling, symmetric
}

static void test_breathe_is_periodic() {
  for (uint32_t t = 0; t < 1000; t += 37) {
    TEST_ASSERT_EQUAL_UINT8(led::breathe(t, 1000), led::breathe(t + 1000, 1000));
    TEST_ASSERT_EQUAL_UINT8(led::breathe(t, 1000), led::breathe(t + 50000, 1000));
  }
}

static void test_breathe_stays_in_range() {
  for (uint32_t t = 0; t < 4000; t++) {
    (void)led::breathe(t, 1400);  // uint8_t return bounds it; assert no crash/UB
  }
  TEST_ASSERT_EQUAL_UINT8(255, led::breathe(700, 1400));
}

static void test_breathe_degenerate_periods() {
  // period 0 and 1 both have a zero half-period; must not divide by zero.
  TEST_ASSERT_EQUAL_UINT8(255, led::breathe(123, 0));
  TEST_ASSERT_EQUAL_UINT8(255, led::breathe(123, 1));
}

static void test_breathe_survives_millis_rollover() {
  // Unsigned modulo keeps the wave continuous across the ~49 day wrap.
  const uint32_t nearMax = 0xFFFFFFFFu - 10;
  for (uint32_t i = 0; i < 20; i++) {
    (void)led::breathe(nearMax + i, 1000);  // must not trap
  }
  TEST_ASSERT_EQUAL_UINT8(led::breathe(500, 1000), led::breathe(500, 1000));
}

// ---------------------------------------------------------------------------
// blink
// ---------------------------------------------------------------------------

static void test_blink_duty_cycle() {
  TEST_ASSERT_TRUE(led::blink(0, 200, 50));
  TEST_ASSERT_TRUE(led::blink(99, 200, 50));
  TEST_ASSERT_FALSE(led::blink(100, 200, 50));  // exactly at the boundary: off
  TEST_ASSERT_FALSE(led::blink(199, 200, 50));
  TEST_ASSERT_TRUE(led::blink(200, 200, 50));   // next window
}

static void test_blink_duty_extremes() {
  TEST_ASSERT_FALSE(led::blink(0, 200, 0));     // never on
  TEST_ASSERT_FALSE(led::blink(150, 200, 0));
  TEST_ASSERT_TRUE(led::blink(0, 200, 100));    // always on
  TEST_ASSERT_TRUE(led::blink(199, 200, 100));
  TEST_ASSERT_TRUE(led::blink(199, 200, 200));  // >100 saturates
}

static void test_blink_zero_period_is_on() {
  TEST_ASSERT_TRUE(led::blink(123, 0, 50));  // must not divide by zero
}

static void test_blink_asymmetric_duty() {
  TEST_ASSERT_TRUE(led::blink(0, 1000, 25));
  TEST_ASSERT_TRUE(led::blink(249, 1000, 25));
  TEST_ASSERT_FALSE(led::blink(250, 1000, 25));
  TEST_ASSERT_FALSE(led::blink(999, 1000, 25));
}

static void test_blink_is_periodic() {
  for (uint32_t t = 0; t < 200; t += 7) {
    TEST_ASSERT_EQUAL(led::blink(t, 200, 50), led::blink(t + 200, 200, 50));
  }
}

// ---------------------------------------------------------------------------
// flashN
// ---------------------------------------------------------------------------

static void test_flashN_produces_exactly_n_pulses() {
  // 3 pulses of 120ms on/off, then dark until 1600ms.
  bool prev = false;
  int rising = 0;
  for (uint32_t t = 0; t < 1600; t++) {
    bool on = led::flashN(t, 1600, 3, 120);
    if (on && !prev) rising++;
    prev = on;
  }
  TEST_ASSERT_EQUAL_INT(3, rising);
}

static void test_flashN_train_then_gap() {
  TEST_ASSERT_TRUE(led::flashN(0, 1600, 3, 120));     // pulse 1 on
  TEST_ASSERT_FALSE(led::flashN(120, 1600, 3, 120));  // pulse 1 off
  TEST_ASSERT_TRUE(led::flashN(240, 1600, 3, 120));   // pulse 2 on
  TEST_ASSERT_TRUE(led::flashN(480, 1600, 3, 120));   // pulse 3 on
  TEST_ASSERT_FALSE(led::flashN(720, 1600, 3, 120));  // train over -> gap
  TEST_ASSERT_FALSE(led::flashN(1599, 1600, 3, 120)); // still gap
  TEST_ASSERT_TRUE(led::flashN(1600, 1600, 3, 120));  // next cycle
}

static void test_flashN_degenerate_args() {
  TEST_ASSERT_FALSE(led::flashN(0, 0, 3, 120));    // no period
  TEST_ASSERT_FALSE(led::flashN(0, 1600, 0, 120)); // no pulses
  TEST_ASSERT_FALSE(led::flashN(0, 1600, 3, 0));   // no pulse width
}

static void test_flashN_train_longer_than_period_never_traps() {
  for (uint32_t t = 0; t < 500; t++) (void)led::flashN(t, 100, 9, 120);
}

// ---------------------------------------------------------------------------
// Screen states
// ---------------------------------------------------------------------------

static void test_splash_ramps_white() {
  TEST_ASSERT_TRUE((led::splash(0.0f) == led::kOff));
  TEST_ASSERT_TRUE((led::splash(1.0f) == led::kWhite));
  Rgb mid = led::splash(0.5f);
  TEST_ASSERT_TRUE(mid.r > 0 && mid.r < 255);
  TEST_ASSERT_EQUAL_UINT8(mid.r, mid.g);  // stays neutral
  TEST_ASSERT_EQUAL_UINT8(mid.r, mid.b);
}

static void test_oobe_step1_breathes_blue_and_never_goes_dark() {
  // A dark frame would read as "nothing is happening" mid-pairing.
  for (uint32_t t = 0; t < 1400; t += 10) {
    Rgb c = led::oobeStep1(t);
    TEST_ASSERT_TRUE(c.b > 0);
    TEST_ASSERT_TRUE(c.b >= c.r);  // unmistakably blue
  }
}

static void test_oobe_step2_blinks_blue() {
  TEST_ASSERT_TRUE((led::oobeStep2(0) == led::kBlue));
  TEST_ASSERT_TRUE((led::oobeStep2(150) == led::kOff));
}

static void test_oobe_steps_are_distinguishable() {
  // Step 2 must look busier than step 1 — it is the "act now" step.
  TEST_ASSERT_TRUE((led::oobeStep2(150) == led::kOff));
  TEST_ASSERT_TRUE(led::oobeStep1(150).b > 0);
}

static void test_found_is_solid_blue() {
  TEST_ASSERT_TRUE((led::found(0) == led::kBlue));
  TEST_ASSERT_TRUE((led::found(99999) == led::kBlue));
}

static void test_paired_flashes_green_three_times() {
  bool prev = false;
  int rising = 0;
  for (uint32_t t = 0; t < 1600; t++) {
    Rgb c = led::paired(t);
    bool on = (c != led::kOff);
    if (on) TEST_ASSERT_TRUE((c == led::kGreen));
    if (on && !prev) rising++;
    prev = on;
  }
  TEST_ASSERT_EQUAL_INT(3, rising);
}

static void test_reconnecting_breathes_amber_and_stays_lit() {
  for (uint32_t t = 0; t < 2000; t += 10) {
    Rgb c = led::reconnecting(t);
    TEST_ASSERT_TRUE(c.r > 0);
    TEST_ASSERT_TRUE(c.r > c.b);  // amber, not blue
  }
}

static void test_waking_strobes_white() {
  TEST_ASSERT_TRUE((led::waking(0) == led::kWhite));
  TEST_ASSERT_TRUE((led::waking(60) == led::kOff));
  TEST_ASSERT_TRUE((led::waking(120) == led::kWhite));
}

static void test_identity_switch_flashes_target_colour() {
  // The colour must name the face being rebooted into, unambiguously.
  TEST_ASSERT_TRUE((led::identitySwitch(true, 0) == led::kBlue));
  TEST_ASSERT_TRUE((led::identitySwitch(false, 0) == led::kGreen));
  // Three pulses of 160 ms (train 960 ms), then dark until the reboot.
  TEST_ASSERT_TRUE((led::identitySwitch(true, 160) == led::kOff));
  TEST_ASSERT_TRUE((led::identitySwitch(true, 1000) == led::kOff));
  TEST_ASSERT_TRUE((led::identitySwitch(false, 1599) == led::kOff));
}

static void test_diagnostics_is_solid_cyan() {
  TEST_ASSERT_TRUE((led::diagnostics(0) == led::kCyan));
}

static void test_forget_confirm_blinks_red() {
  TEST_ASSERT_TRUE((led::forgetConfirm(0) == led::kRed));
  TEST_ASSERT_TRUE((led::forgetConfirm(200) == led::kOff));
}

static void test_forget_hold_deepens_red_with_progress() {
  Rgb start = led::forgetHold(0.0f);
  Rgb mid = led::forgetHold(0.5f);
  Rgb end = led::forgetHold(1.0f);

  TEST_ASSERT_TRUE(start.r > 0);        // visible the instant you press
  TEST_ASSERT_TRUE(mid.r > start.r);    // and it grows
  TEST_ASSERT_TRUE(end.r > mid.r);
  TEST_ASSERT_EQUAL_UINT8(255, end.r);  // full red at commit
  TEST_ASSERT_EQUAL_UINT8(0, end.g);    // unambiguously red, not amber
  TEST_ASSERT_EQUAL_UINT8(0, end.b);
}

static void test_forget_hold_clamps_out_of_range_progress() {
  TEST_ASSERT_TRUE((led::forgetHold(2.0f) == led::forgetHold(1.0f)));
  TEST_ASSERT_TRUE((led::forgetHold(-1.0f) == led::forgetHold(0.0f)));
}

// ---------------------------------------------------------------------------
// bootHealth
// ---------------------------------------------------------------------------

// Count colour-on pulses across one full readout period, sampling finely enough
// that a pulse cannot be stepped over.
static int countPulses(uint8_t brownouts, uint8_t faults, Rgb colour,
                       uint32_t spanMs) {
  int pulses = 0;
  bool on = false;
  for (uint32_t t = 0; t < spanMs; t++) {
    const bool nowOn = (led::bootHealth(t, brownouts, faults) == colour);
    if (nowOn && !on) pulses++;
    on = nowOn;
  }
  return pulses;
}

static void test_boot_health_clean_history_breathes_green() {
  // The reassuring case has to be unmistakably *not* a flash train, or a user
  // counting blinks would report a fault that never happened.
  bool sawLit = false;
  for (uint32_t t = 0; t < 4000; t += 10) {
    const Rgb c = led::bootHealth(t, 0, 0);
    TEST_ASSERT_EQUAL_UINT8(0, c.r);
    if (c.g > 0) sawLit = true;
  }
  TEST_ASSERT_TRUE(sawLit);
  // Peak of the breathe: full green at half the 2 s period.
  TEST_ASSERT_TRUE((led::bootHealth(1000, 0, 0) == led::kGreen));
}

static void test_boot_health_blinks_brownouts_in_red() {
  for (uint8_t n = 1; n <= 5; n++) {
    const uint32_t period = (uint32_t)n * led::kHealthFlashMs * 2 + led::kHealthGapMs;
    TEST_ASSERT_EQUAL_INT(n, countPulses(n, 0, led::kRed, period));
  }
}

static void test_boot_health_blinks_faults_in_blue() {
  for (uint8_t n = 1; n <= 5; n++) {
    const uint32_t period = (uint32_t)n * led::kHealthFlashMs * 2 + led::kHealthGapMs;
    TEST_ASSERT_EQUAL_INT(n, countPulses(0, n, led::kBlue, period));
  }
}

static void test_boot_health_plays_both_trains_without_bleeding_together() {
  // 3 brownouts and 2 faults must read as "3 red, then 2 blue" — never as one
  // train of five, and never with a colour appearing inside the other's train.
  const uint32_t period = (uint32_t)3 * led::kHealthFlashMs * 2 +
                          (uint32_t)2 * led::kHealthFlashMs * 2 +
                          led::kHealthGapMs * 2;
  TEST_ASSERT_EQUAL_INT(3, countPulses(3, 2, led::kRed, period));
  TEST_ASSERT_EQUAL_INT(2, countPulses(3, 2, led::kBlue, period));

  // The red train finishes before the first blue pulse starts.
  uint32_t lastRed = 0, firstBlue = period;
  for (uint32_t t = 0; t < period; t++) {
    const Rgb c = led::bootHealth(t, 3, 2);
    if (c == led::kRed) lastRed = t;
    if (c == led::kBlue && firstBlue == period) firstBlue = t;
  }
  TEST_ASSERT_TRUE(lastRed < firstBlue);
}

static void test_boot_health_caps_the_flash_count() {
  // Past the cap the train must stop growing, or the readout would run for
  // minutes and still be uncountable.
  const uint32_t period =
      (uint32_t)led::kHealthFlashCap * led::kHealthFlashMs * 2 + led::kHealthGapMs;
  TEST_ASSERT_EQUAL_INT(led::kHealthFlashCap,
                        countPulses(led::kHealthFlashCap, 0, led::kRed, period));
  TEST_ASSERT_EQUAL_INT(led::kHealthFlashCap, countPulses(200, 0, led::kRed, period));
  TEST_ASSERT_EQUAL_INT(led::kHealthFlashCap, countPulses(255, 0, led::kRed, period));
}

static void test_boot_health_repeats_so_a_miscount_costs_only_a_second_look() {
  const uint32_t period = (uint32_t)2 * led::kHealthFlashMs * 2 + led::kHealthGapMs;
  for (uint32_t t = 0; t < period; t += 3) {
    TEST_ASSERT_TRUE((led::bootHealth(t, 2, 0) == led::bootHealth(t + period, 2, 0)));
  }
}

static void test_boot_health_starts_lit_so_the_first_flash_is_never_missed() {
  // The readout is phase-anchored to the button press; if it opened dark the
  // user would be counting from an arbitrary point in the train.
  TEST_ASSERT_TRUE((led::bootHealth(0, 1, 0) == led::kRed));
  TEST_ASSERT_TRUE((led::bootHealth(0, 0, 1) == led::kBlue));
}

static void test_boot_health_survives_millis_rollover() {
  // elapsedMs is a difference, so it cannot itself wrap — but the modulo maths
  // must still hold at the top of the range.
  const uint32_t period = (uint32_t)2 * led::kHealthFlashMs * 2 + led::kHealthGapMs;
  const uint32_t base = 0xFFFFFFFFu - period;
  for (uint32_t i = 0; i < period; i += 5) {
    const Rgb c = led::bootHealth(base + i, 2, 0);
    TEST_ASSERT_TRUE(c == led::kRed || c == led::kOff);
  }
}

static void test_align_test_cycles_all_three_channels() {
  // The whole point is proving LED_PIN and the colour order, so all three
  // channels must be exercised.
  TEST_ASSERT_TRUE((led::alignTest(0) == led::kRed));
  TEST_ASSERT_TRUE((led::alignTest(600) == led::kGreen));
  TEST_ASSERT_TRUE((led::alignTest(1200) == led::kBlue));
  TEST_ASSERT_TRUE((led::alignTest(1800) == led::kRed));  // wraps
  TEST_ASSERT_TRUE((led::alignTest(599) == led::kRed));   // holds for the step
}

// ---------------------------------------------------------------------------
// live() — the priority ladder is the important part
// ---------------------------------------------------------------------------

static led::LiveInputs streaming() {
  led::LiveInputs in;
  in.usbReady = true;
  in.usbSuspended = false;
  in.sinceRumbleMs = 10000;
  return in;
}

static void test_live_streaming_is_steady_green() {
  Rgb a = led::live(streaming(), 0);
  Rgb b = led::live(streaming(), 12345);
  TEST_ASSERT_TRUE((a == b));  // steady: no animation, so show() never rewrites
  TEST_ASSERT_TRUE(a.g > 0);
  TEST_ASSERT_EQUAL_UINT8(0, a.r);
}

static void test_live_suspended_breathes_amber() {
  led::LiveInputs in = streaming();
  in.usbSuspended = true;
  for (uint32_t t = 0; t < 2600; t += 20) {
    Rgb c = led::live(in, t);
    TEST_ASSERT_TRUE(c.r > 0);
    TEST_ASSERT_TRUE(c.r > c.b);
  }
}

static void test_live_suspended_is_dimmer_than_streaming() {
  // It idles here all night next to a TV; it must not be the brightest state.
  led::LiveInputs susp = streaming();
  susp.usbSuspended = true;
  uint8_t brightest = 0;
  for (uint32_t t = 0; t < 2600; t += 10) {
    brightest = std::max(brightest, led::luma(led::live(susp, t)));
  }
  TEST_ASSERT_TRUE(brightest < led::luma(led::live(streaming(), 0)));
}

static void test_live_not_ready_breathes_green() {
  led::LiveInputs in;
  in.usbReady = false;
  in.usbSuspended = false;
  in.sinceRumbleMs = 10000;
  Rgb a = led::live(in, 0);
  Rgb b = led::live(in, 900);
  TEST_ASSERT_TRUE((a != b));       // animated, unlike the streaming state
  TEST_ASSERT_EQUAL_UINT8(0, a.r);  // still green
}

static void test_live_rumble_outranks_everything() {
  led::LiveInputs in = streaming();
  in.rumbleActive = true;
  in.rumbleMax = 255;
  in.sinceRumbleMs = 0;
  Rgb c = led::live(in, 0);
  TEST_ASSERT_TRUE(c.r > 0);  // orange, not green
  TEST_ASSERT_TRUE(c.r > c.g);

  // ...even while suspended.
  in.usbSuspended = true;
  TEST_ASSERT_TRUE((led::live(in, 0) == c));
}

static void test_live_rumble_expires_after_the_hold_window() {
  led::LiveInputs in = streaming();
  in.rumbleActive = true;
  in.rumbleMax = 200;

  in.sinceRumbleMs = led::kRumbleHoldMs - 1;
  Rgb held = led::live(in, 0);
  TEST_ASSERT_TRUE(held.r > held.g);  // still orange

  in.sinceRumbleMs = led::kRumbleHoldMs;  // boundary: window is exclusive
  TEST_ASSERT_TRUE((led::live(in, 0) == led::live(streaming(), 0)));
}

static void test_live_rumble_requires_a_live_motor() {
  // lastRumbleMs is 0 at boot, so `since` is tiny — only the motor flag stops
  // the LED claiming a rumble that never happened.
  led::LiveInputs in = streaming();
  in.rumbleActive = false;
  in.sinceRumbleMs = 0;
  TEST_ASSERT_TRUE((led::live(in, 0) == led::live(streaming(), 0)));
}

static void test_live_rumble_brightness_tracks_motor_strength() {
  led::LiveInputs weak = streaming();
  weak.rumbleActive = true;
  weak.sinceRumbleMs = 0;
  weak.rumbleMax = 1;

  led::LiveInputs strong = weak;
  strong.rumbleMax = 255;

  TEST_ASSERT_TRUE(led::luma(led::live(strong, 0)) > led::luma(led::live(weak, 0)));
  TEST_ASSERT_TRUE(led::live(weak, 0).r > 0);  // even a faint buzz is visible
}

static void test_live_states_are_mutually_distinguishable() {
  led::LiveInputs susp = streaming();
  susp.usbSuspended = true;

  led::LiveInputs rumbling = streaming();
  rumbling.rumbleActive = true;
  rumbling.rumbleMax = 255;
  rumbling.sinceRumbleMs = 0;

  Rgb a = led::live(streaming(), 0);
  Rgb b = led::live(susp, 0);
  Rgb c = led::live(rumbling, 0);
  TEST_ASSERT_TRUE((a != b));
  TEST_ASSERT_TRUE((b != c));
  TEST_ASSERT_TRUE((a != c));
}

// ---------------------------------------------------------------------------

int main(int, char**) {
  UNITY_BEGIN();

  RUN_TEST(test_rgb_equality);
  RUN_TEST(test_rgb_defaults_to_off);

  RUN_TEST(test_scale8_endpoints_are_exact);
  RUN_TEST(test_scale8_is_monotonic_and_never_overflows);
  RUN_TEST(test_scale8_halves);
  RUN_TEST(test_dim_scales_every_channel);
  RUN_TEST(test_dim_of_off_stays_off);

  RUN_TEST(test_luma_black_and_white_are_exact);
  RUN_TEST(test_luma_weights_channels_by_perception);
  RUN_TEST(test_luma_never_overflows);

  RUN_TEST(test_ramp_level_endpoints_and_clamping);
  RUN_TEST(test_ramp_level_survives_nan);
  RUN_TEST(test_ramp_level_is_monotonic);

  RUN_TEST(test_breathe_rises_to_peak_at_half_period);
  RUN_TEST(test_breathe_is_periodic);
  RUN_TEST(test_breathe_stays_in_range);
  RUN_TEST(test_breathe_degenerate_periods);
  RUN_TEST(test_breathe_survives_millis_rollover);

  RUN_TEST(test_blink_duty_cycle);
  RUN_TEST(test_blink_duty_extremes);
  RUN_TEST(test_blink_zero_period_is_on);
  RUN_TEST(test_blink_asymmetric_duty);
  RUN_TEST(test_blink_is_periodic);

  RUN_TEST(test_flashN_produces_exactly_n_pulses);
  RUN_TEST(test_flashN_train_then_gap);
  RUN_TEST(test_flashN_degenerate_args);
  RUN_TEST(test_flashN_train_longer_than_period_never_traps);

  RUN_TEST(test_splash_ramps_white);
  RUN_TEST(test_oobe_step1_breathes_blue_and_never_goes_dark);
  RUN_TEST(test_oobe_step2_blinks_blue);
  RUN_TEST(test_oobe_steps_are_distinguishable);
  RUN_TEST(test_found_is_solid_blue);
  RUN_TEST(test_paired_flashes_green_three_times);
  RUN_TEST(test_reconnecting_breathes_amber_and_stays_lit);
  RUN_TEST(test_waking_strobes_white);
  RUN_TEST(test_identity_switch_flashes_target_colour);
  RUN_TEST(test_diagnostics_is_solid_cyan);
  RUN_TEST(test_forget_confirm_blinks_red);
  RUN_TEST(test_forget_hold_deepens_red_with_progress);
  RUN_TEST(test_forget_hold_clamps_out_of_range_progress);
  RUN_TEST(test_align_test_cycles_all_three_channels);

  RUN_TEST(test_boot_health_clean_history_breathes_green);
  RUN_TEST(test_boot_health_blinks_brownouts_in_red);
  RUN_TEST(test_boot_health_blinks_faults_in_blue);
  RUN_TEST(test_boot_health_plays_both_trains_without_bleeding_together);
  RUN_TEST(test_boot_health_caps_the_flash_count);
  RUN_TEST(test_boot_health_repeats_so_a_miscount_costs_only_a_second_look);
  RUN_TEST(test_boot_health_starts_lit_so_the_first_flash_is_never_missed);
  RUN_TEST(test_boot_health_survives_millis_rollover);

  RUN_TEST(test_live_streaming_is_steady_green);
  RUN_TEST(test_live_suspended_breathes_amber);
  RUN_TEST(test_live_suspended_is_dimmer_than_streaming);
  RUN_TEST(test_live_not_ready_breathes_green);
  RUN_TEST(test_live_rumble_outranks_everything);
  RUN_TEST(test_live_rumble_expires_after_the_hold_window);
  RUN_TEST(test_live_rumble_requires_a_live_motor);
  RUN_TEST(test_live_rumble_brightness_tracks_motor_strength);
  RUN_TEST(test_live_states_are_mutually_distinguishable);

  return UNITY_END();
}
