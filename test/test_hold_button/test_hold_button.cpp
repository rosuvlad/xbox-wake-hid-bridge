// Unit tests for HoldButton.h — the headless build's only control.
//
// This is the one input that can destroy user state (it erases the controller
// bond), so the cases that matter most are the ones where it must *not* fire.

#include <unity.h>

#include "HoldButton.h"

static const uint32_t kHold = 2000;  // mirrors FORGET_HOLD_MS

void setUp() {}
void tearDown() {}

// Feed a continuous press from `from` to `to` in `step` increments, counting
// how many times the hold fires.
static int pressFrom(HoldButton& b, uint32_t from, uint32_t to, uint32_t step) {
  int fired = 0;
  for (uint32_t t = from; t <= to; t += step) {
    if (b.update(true, t)) fired++;
  }
  return fired;
}

// ---------------------------------------------------------------------------
// Not firing
// ---------------------------------------------------------------------------

static void test_idle_never_fires() {
  HoldButton b(kHold);
  for (uint32_t t = 0; t < 10000; t += 10) {
    TEST_ASSERT_FALSE(b.update(false, t));
  }
  TEST_ASSERT_FALSE(b.isDown());
  TEST_ASSERT_EQUAL_UINT32(0, b.heldMs());
  TEST_ASSERT_EQUAL_FLOAT(0.0f, b.progress());
}

static void test_short_press_does_not_fire() {
  HoldButton b(kHold);
  TEST_ASSERT_EQUAL_INT(0, pressFrom(b, 0, kHold - 1, 10));
  TEST_ASSERT_FALSE(b.update(false, kHold + 500));  // released just shy
}

static void test_release_one_tick_before_threshold_does_not_fire() {
  HoldButton b(kHold);
  b.update(true, 0);
  TEST_ASSERT_FALSE(b.update(true, kHold - 1));
  TEST_ASSERT_FALSE(b.update(false, kHold));  // let go in time
}

static void test_repeated_taps_never_accumulate_into_a_hold() {
  // Each tap must start from zero, or impatient tapping would erase the bond.
  HoldButton b(kHold);
  for (int i = 0; i < 50; i++) {
    uint32_t base = i * 1000;
    TEST_ASSERT_FALSE(b.update(true, base));
    TEST_ASSERT_FALSE(b.update(true, base + 200));
    TEST_ASSERT_FALSE(b.update(false, base + 300));
  }
}

static void test_bounce_at_press_edge_restarts_the_timer() {
  // A contact bounce must not shorten the hold.
  HoldButton b(kHold);
  b.update(true, 0);
  b.update(false, 5);   // bounce
  b.update(true, 10);   // settles here; threshold is now 2010
  TEST_ASSERT_FALSE(b.update(true, kHold));
  TEST_ASSERT_FALSE(b.update(true, kHold + 9));
  TEST_ASSERT_TRUE(b.update(true, kHold + 10));
}

// ---------------------------------------------------------------------------
// Firing
// ---------------------------------------------------------------------------

static void test_fires_exactly_at_threshold() {
  HoldButton b(kHold);
  b.update(true, 0);
  TEST_ASSERT_FALSE(b.update(true, kHold - 1));
  TEST_ASSERT_TRUE(b.update(true, kHold));
}

static void test_fires_exactly_once_while_held() {
  HoldButton b(kHold);
  b.update(true, 0);
  TEST_ASSERT_EQUAL_INT(1, pressFrom(b, 10, 30000, 10));
}

static void test_does_not_refire_after_a_long_hold() {
  HoldButton b(kHold);
  b.update(true, 0);
  TEST_ASSERT_TRUE(b.update(true, kHold));
  for (uint32_t t = kHold + 10; t < kHold + 60000; t += 10) {
    TEST_ASSERT_FALSE(b.update(true, t));
  }
}

static void test_can_fire_again_after_release() {
  HoldButton b(kHold);
  b.update(true, 0);
  TEST_ASSERT_TRUE(b.update(true, kHold));
  b.update(false, kHold + 100);
  b.update(true, kHold + 200);
  TEST_ASSERT_TRUE(b.update(true, kHold + 200 + kHold));
}

static void test_first_tick_already_past_threshold_fires() {
  // A button held through boot: the first sample starts the timer, it does not
  // instantly commit.
  HoldButton b(kHold);
  TEST_ASSERT_FALSE(b.update(true, 500000));
  TEST_ASSERT_TRUE(b.update(true, 500000 + kHold));
}

// ---------------------------------------------------------------------------
// heldMs / progress
// ---------------------------------------------------------------------------

static void test_held_ms_tracks_the_press() {
  HoldButton b(kHold);
  b.update(true, 1000);
  TEST_ASSERT_EQUAL_UINT32(0, b.heldMs());
  b.update(true, 1500);
  TEST_ASSERT_EQUAL_UINT32(500, b.heldMs());
  TEST_ASSERT_TRUE(b.isDown());
}

static void test_release_clears_held_ms_and_down() {
  HoldButton b(kHold);
  b.update(true, 0);
  b.update(true, 500);
  b.update(false, 600);
  TEST_ASSERT_EQUAL_UINT32(0, b.heldMs());
  TEST_ASSERT_FALSE(b.isDown());
  TEST_ASSERT_EQUAL_FLOAT(0.0f, b.progress());
}

static void test_progress_spans_zero_to_one() {
  HoldButton b(kHold);
  b.update(true, 0);
  TEST_ASSERT_EQUAL_FLOAT(0.0f, b.progress());
  b.update(true, 1000);
  TEST_ASSERT_EQUAL_FLOAT(0.5f, b.progress());
  b.update(true, 1999);
  TEST_ASSERT_TRUE(b.progress() > 0.99f && b.progress() < 1.0f);
}

static void test_progress_clamps_at_one_past_threshold() {
  HoldButton b(kHold);
  b.update(true, 0);
  b.update(true, kHold * 10);
  TEST_ASSERT_EQUAL_FLOAT(1.0f, b.progress());
}

static void test_progress_is_monotonic_during_a_hold() {
  HoldButton b(kHold);
  b.update(true, 0);
  float prev = 0.0f;
  for (uint32_t t = 0; t <= kHold; t += 10) {
    b.update(true, t);
    TEST_ASSERT_TRUE(b.progress() >= prev);
    prev = b.progress();
  }
  TEST_ASSERT_EQUAL_FLOAT(1.0f, prev);
}

// ---------------------------------------------------------------------------
// Edge cases
// ---------------------------------------------------------------------------

static void test_survives_millis_rollover_mid_hold() {
  // millis() wraps every ~49 days. Unsigned subtraction must keep the elapsed
  // time correct rather than producing a huge value and firing early.
  const uint32_t start = 0xFFFFFFFFu - 1000;  // 1000ms before the wrap
  HoldButton b(kHold);
  b.update(true, start);
  TEST_ASSERT_EQUAL_UINT32(0, b.heldMs());

  // 500ms in, still before the wrap.
  TEST_ASSERT_FALSE(b.update(true, start + 500));
  TEST_ASSERT_EQUAL_UINT32(500, b.heldMs());

  // 1500ms in — the clock has now wrapped past zero.
  TEST_ASSERT_FALSE(b.update(true, start + 1500));
  TEST_ASSERT_EQUAL_UINT32(1500, b.heldMs());

  // And it commits at exactly the right moment, not early.
  TEST_ASSERT_TRUE(b.update(true, start + kHold));
  TEST_ASSERT_EQUAL_UINT32(kHold, b.heldMs());
}

static void test_zero_threshold_fires_immediately() {
  HoldButton b(0);
  TEST_ASSERT_TRUE(b.update(true, 0));
  TEST_ASSERT_EQUAL_FLOAT(1.0f, b.progress());
  TEST_ASSERT_FALSE(b.update(true, 100));  // still only once
}

static void test_same_timestamp_repeated_does_not_double_fire() {
  // A loop fast enough to sample twice inside one millisecond.
  HoldButton b(kHold);
  b.update(true, 0);
  TEST_ASSERT_TRUE(b.update(true, kHold));
  TEST_ASSERT_FALSE(b.update(true, kHold));
  TEST_ASSERT_FALSE(b.update(true, kHold));
}

// ---------------------------------------------------------------------------

int main(int, char**) {
  UNITY_BEGIN();

  RUN_TEST(test_idle_never_fires);
  RUN_TEST(test_short_press_does_not_fire);
  RUN_TEST(test_release_one_tick_before_threshold_does_not_fire);
  RUN_TEST(test_repeated_taps_never_accumulate_into_a_hold);
  RUN_TEST(test_bounce_at_press_edge_restarts_the_timer);

  RUN_TEST(test_fires_exactly_at_threshold);
  RUN_TEST(test_fires_exactly_once_while_held);
  RUN_TEST(test_does_not_refire_after_a_long_hold);
  RUN_TEST(test_can_fire_again_after_release);
  RUN_TEST(test_first_tick_already_past_threshold_fires);

  RUN_TEST(test_held_ms_tracks_the_press);
  RUN_TEST(test_release_clears_held_ms_and_down);
  RUN_TEST(test_progress_spans_zero_to_one);
  RUN_TEST(test_progress_clamps_at_one_past_threshold);
  RUN_TEST(test_progress_is_monotonic_during_a_hold);

  RUN_TEST(test_survives_millis_rollover_mid_hold);
  RUN_TEST(test_zero_threshold_fires_immediately);
  RUN_TEST(test_same_timestamp_repeated_does_not_double_fire);

  return UNITY_END();
}
