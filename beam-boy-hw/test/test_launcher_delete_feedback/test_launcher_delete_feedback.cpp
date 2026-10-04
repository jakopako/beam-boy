#include <unity.h>

#include "scenes/launcher_delete_feedback.h"

using namespace beamboy;

void setUp(void) {}
void tearDown(void) {}

void test_color_fades_from_accent_to_solid_bright_red(void) {
  const Color accent(40, 180, 255);
  const DeleteFeedback start = launcherDeleteFeedback(accent, 0);
  TEST_ASSERT_EQUAL_UINT8(accent.r, start.color.r);
  TEST_ASSERT_EQUAL_UINT8(accent.g, start.color.g);
  TEST_ASSERT_EQUAL_UINT8(accent.b, start.color.b);
  const DeleteFeedback middle = launcherDeleteFeedback(accent, 1500);
  TEST_ASSERT_EQUAL_UINT8(147, middle.color.r);
  TEST_ASSERT_EQUAL_UINT8(90, middle.color.g);
  TEST_ASSERT_EQUAL_UINT8(127, middle.color.b);
  for (uint32_t t : {uint32_t{3000}, uint32_t{4000}}) {
    const DeleteFeedback end = launcherDeleteFeedback(accent, t);
    TEST_ASSERT_EQUAL_UINT8(255, end.color.r);
    TEST_ASSERT_EQUAL_UINT8(0, end.color.g);
    TEST_ASSERT_EQUAL_UINT8(0, end.color.b);
    TEST_ASSERT_EQUAL_FLOAT(1.0f, end.intensity);
  }
}

void test_feedback_starts_immediately_and_never_disappears(void) {
  const Color accent(0, 255, 255);
  const DeleteFeedback first = launcherDeleteFeedback(accent, 16);
  TEST_ASSERT_TRUE(first.color.r > 0);
  TEST_ASSERT_TRUE(first.color.g < accent.g);
  for (uint32_t t = 0; t < kDeleteHoldMs; ++t) {
    const DeleteFeedback feedback = launcherDeleteFeedback(accent, t);
    const float minimum = 0.5f + 0.5f * t / kDeleteHoldMs;
    TEST_ASSERT_TRUE(feedback.intensity >= minimum - 0.00001f);
    TEST_ASSERT_TRUE(feedback.intensity <= 1.0f);
  }
}

void test_pulses_accelerate_from_slow_to_five_cycles_per_second(void) {
  TEST_ASSERT_FLOAT_WITHIN(0.001f, 0.64f, deletePulseRate(0.0f));
  TEST_ASSERT_FLOAT_WITHIN(0.001f, 5.0f, deletePulseRate(1.0f));
  int early_peaks = 0;
  int late_peaks = 0;
  for (uint32_t t = 1; t < 2999; ++t) {
    const Color accent(0, 150, 255);
    const float previous = launcherDeleteFeedback(accent, t - 1).intensity;
    const float current = launcherDeleteFeedback(accent, t).intensity;
    const float next = launcherDeleteFeedback(accent, t + 1).intensity;
    if (current > previous && current >= next) {
      if (t < 1000) ++early_peaks;
      if (t >= 2000) ++late_peaks;
    }
  }
  TEST_ASSERT_TRUE(early_peaks >= 1);
  TEST_ASSERT_TRUE(late_peaks > early_peaks);
}

int main(int, char**) {
  UNITY_BEGIN();
  RUN_TEST(test_color_fades_from_accent_to_solid_bright_red);
  RUN_TEST(test_feedback_starts_immediately_and_never_disappears);
  RUN_TEST(test_pulses_accelerate_from_slow_to_five_cycles_per_second);
  return UNITY_END();
}
