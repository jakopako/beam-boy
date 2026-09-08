// Beam Boy — host tests for the display layer.
//
// Two of the things checked here are regression tests for bugs that actually
// happened, not hypotheticals:
//
//   * wrappedSin() must stay accurate at phases large enough to have crashed the
//     device. The crash itself cannot be reproduced off-target -- it was a stack
//     overflow in newlib's huge-argument path, and the host's libm has no such
//     limit -- so what is tested is the property the fix relies on: that
//     wrapping does not change the answer. Note this test would still pass if
//     someone replaced wrappedSin() with a bare sinf(), which is exactly why the
//     soak test also asserts on the *argument* the scenes pass in.
//
//   * Color::scaled() must truncate while present() rounds. The asymmetry looks
//     like an inconsistency and is a standing temptation to "tidy"; truncation
//     is what lets fade() reach true black.

#include <unity.h>

#include <cmath>

#include "core/display.h"

using namespace beamboy;

void setUp(void) {}
void tearDown(void) {}

void test_wrapped_sin_matches_sinf_at_small_phase(void) {
  for (float phase = -10.0f; phase < 10.0f; phase += 0.37f) {
    TEST_ASSERT_FLOAT_WITHIN(1e-5f, sinf(phase), wrappedSin(phase));
  }
}

void test_wrapped_sin_stays_accurate_at_crash_scale_phase(void) {
  // Phases in the region where the on-target sinf() blew the cont stack. What
  // is asserted here is accuracy at the phases a scene realistically reaches:
  // a rate of a few cycles per second over hours of uptime lands in the tens of
  // thousands, not the millions.
  //
  // The upper bound is deliberate. Beyond ~1e5 a float's absolute precision is
  // coarser than the ~1e-3 radians that a smooth animation needs, so fmodf
  // cannot give a meaningful answer no matter how it is implemented -- the
  // information is already gone from the input. wrappedSin() still returns a
  // bounded, finite value there (see the range test), which is all that is
  // required to keep the device alive; it just cannot animate smoothly. That is
  // why scenes wrap their own phase every hour rather than relying on this
  // function to rescue an unbounded accumulator.
  const double kTwoPi = 6.283185307179586;
  for (float phase = 1.0e3f; phase < 1.0e5f; phase *= 2.7f) {
    const double exact = sin(fmod(static_cast<double>(phase), kTwoPi));
    TEST_ASSERT_FLOAT_WITHIN(1e-2f, static_cast<float>(exact),
                             wrappedSin(phase));
  }
}

void test_wrapped_sin_stays_in_range_everywhere(void) {
  for (float phase = 0.0f; phase < 1.0e7f; phase += 9973.0f) {
    const float v = wrappedSin(phase);
    TEST_ASSERT_TRUE(v >= -1.0001f && v <= 1.0001f);
    TEST_ASSERT_FALSE(std::isnan(v));
  }
}

void test_pulse_is_periodic(void) {
  // Two moments one whole cycle apart must look the same, or animations would
  // visibly jump when a scene wraps its phase.
  const float rate = 2.0f;
  const float period = 6.28318531f / rate;
  TEST_ASSERT_FLOAT_WITHIN(1e-3f, pulse(3.0f, rate), pulse(3.0f + period, rate));
}

void test_scaled_truncates_so_fade_reaches_black(void) {
  // The real scenario: fade() applied repeatedly to its own output. With
  // rounding, a channel at 1 scaled by 0.9 stays at 1 forever and the tube keeps
  // a faint permanent glow.
  Color c(255, 128, 3);
  for (int i = 0; i < 500; i++) c = c.scaled(0.9f);
  TEST_ASSERT_EQUAL_UINT8(0, c.r);
  TEST_ASSERT_EQUAL_UINT8(0, c.g);
  TEST_ASSERT_EQUAL_UINT8(0, c.b);
}

void test_scaled_endpoints(void) {
  const Color c(200, 100, 50);
  TEST_ASSERT_EQUAL_UINT8(0, c.scaled(0.0f).r);
  TEST_ASSERT_EQUAL_UINT8(200, c.scaled(1.0f).r);
  // Out-of-range factors must clamp, not wrap around.
  TEST_ASSERT_EQUAL_UINT8(0, c.scaled(-3.0f).r);
  TEST_ASSERT_EQUAL_UINT8(200, c.scaled(9.0f).r);
}

void test_hsv_primaries(void) {
  const Color red = Color::hsv(0.0f, 1.0f, 1.0f);
  TEST_ASSERT_EQUAL_UINT8(255, red.r);
  TEST_ASSERT_EQUAL_UINT8(0, red.g);

  const Color green = Color::hsv(1.0f / 3.0f, 1.0f, 1.0f);
  TEST_ASSERT_TRUE(green.g > 250);
  TEST_ASSERT_TRUE(green.r < 5);

  // Hue must wrap rather than clamp, since animations sweep it continuously.
  TEST_ASSERT_EQUAL_UINT8(255, Color::hsv(2.0f, 1.0f, 1.0f).r);
}

void test_hsv_zero_saturation_is_grey(void) {
  const Color grey = Color::hsv(0.7f, 0.0f, 1.0f);
  TEST_ASSERT_EQUAL_UINT8(grey.r, grey.g);
  TEST_ASSERT_EQUAL_UINT8(grey.g, grey.b);
}

void test_present_shows_every_frame(void) {
  Display d;
  for (int i = 0; i < 10; ++i) {
    d.rawPixel(0, colors::kWhite);
    d.present();
  }
  // Games rely on every frame reaching the strip.
  TEST_ASSERT_EQUAL_UINT32(10, d.shownCount());
}

int main(int, char**) {
  UNITY_BEGIN();
  RUN_TEST(test_wrapped_sin_matches_sinf_at_small_phase);
  RUN_TEST(test_wrapped_sin_stays_accurate_at_crash_scale_phase);
  RUN_TEST(test_wrapped_sin_stays_in_range_everywhere);
  RUN_TEST(test_pulse_is_periodic);
  RUN_TEST(test_scaled_truncates_so_fade_reaches_black);
  RUN_TEST(test_scaled_endpoints);
  RUN_TEST(test_hsv_primaries);
  RUN_TEST(test_hsv_zero_saturation_is_grey);
  RUN_TEST(test_present_shows_every_frame);
  return UNITY_END();
}
