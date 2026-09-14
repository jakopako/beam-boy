// Tests for the battery-level and idle-sleep policy.
//
// These thresholds are the whole reason a low battery reads as a clear
// warning rather than a flickering indicator that looks broken: hysteresis
// means the direction the percentage is moving matters, not just where it
// currently sits. That is exactly the kind of rule that is easy to get subtly
// wrong and tedious to reproduce on hardware -- it needs a battery genuinely
// discharging across a boundary. Here the whole state machine runs in
// microseconds.

#include <unity.h>

#include "core/power_policy.h"

using namespace beamboy;

namespace {

// --- classifyPowerLevel ----------------------------------------------------

// Comfortably full, starting from Normal: stays Normal.
void test_a_healthy_battery_starting_normal_stays_normal() {
  TEST_ASSERT_TRUE(classifyPowerLevel(80.0f, PowerLevel::kNormal) ==
                    PowerLevel::kNormal);
}

// Discharging from Normal, crossing the low threshold for the first time.
void test_discharging_from_normal_enters_low_at_the_threshold() {
  TEST_ASSERT_TRUE(classifyPowerLevel(15.0f, PowerLevel::kNormal) ==
                    PowerLevel::kLow);
  TEST_ASSERT_TRUE(classifyPowerLevel(15.01f, PowerLevel::kNormal) ==
                    PowerLevel::kNormal);
}

// Discharging further, crossing the critical threshold.
void test_discharging_from_normal_can_reach_critical_directly() {
  // A single sample landing below the critical threshold must not be
  // softened into "low" just because the previous reading was miles above
  // it -- a stalled read followed by a sudden true value must still trip
  // the safe-shutdown path immediately.
  TEST_ASSERT_TRUE(classifyPowerLevel(5.0f, PowerLevel::kNormal) ==
                    PowerLevel::kCritical);
}

// Once low, small upward jitter under the recovery threshold must not clear
// the warning -- that is the entire point of a separate recovery threshold.
void test_low_does_not_clear_until_past_its_own_recovery_threshold() {
  TEST_ASSERT_TRUE(classifyPowerLevel(16.0f, PowerLevel::kLow) ==
                    PowerLevel::kLow);
  TEST_ASSERT_TRUE(classifyPowerLevel(19.99f, PowerLevel::kLow) ==
                    PowerLevel::kLow);
  TEST_ASSERT_TRUE(classifyPowerLevel(20.0f, PowerLevel::kLow) ==
                    PowerLevel::kNormal);
}

// Once critical, recovering must land in Low, never jump straight back to
// Normal -- a battery that just tripped the safe-shutdown path has not earned
// "everything is fine" the instant it ticks up a percent.
void test_critical_recovers_into_low_not_normal() {
  TEST_ASSERT_TRUE(classifyPowerLevel(10.0f, PowerLevel::kCritical) ==
                    PowerLevel::kLow);
  TEST_ASSERT_TRUE(classifyPowerLevel(9.99f, PowerLevel::kCritical) ==
                    PowerLevel::kCritical);
}

// Recovering all the way from critical to normal is possible, but only past
// the low threshold's own, higher recovery point -- both hysteresis gaps
// apply in sequence, not just the first one.
void test_critical_can_recover_all_the_way_to_normal() {
  TEST_ASSERT_TRUE(classifyPowerLevel(20.0f, PowerLevel::kCritical) ==
                    PowerLevel::kNormal);
  TEST_ASSERT_TRUE(classifyPowerLevel(19.99f, PowerLevel::kCritical) ==
                    PowerLevel::kLow);
}

// The dithering case the hysteresis exists for: a percentage bouncing by a
// fraction of a point either side of the low threshold must not flap the
// level back and forth once it has entered Low.
void test_low_survives_dithering_around_the_entry_threshold() {
  PowerLevel level = PowerLevel::kNormal;
  level = classifyPowerLevel(15.0f, level);  // enters Low
  TEST_ASSERT_TRUE(level == PowerLevel::kLow);
  level = classifyPowerLevel(15.3f, level);  // dithers back up slightly
  TEST_ASSERT_TRUE(level == PowerLevel::kLow);
  level = classifyPowerLevel(14.7f, level);  // dithers back down
  TEST_ASSERT_TRUE(level == PowerLevel::kLow);
}

// --- isChargingRate ----------------------------------------------------

// A battery at rest still reports a small non-zero rate; that must not read
// as charging.
void test_resting_jitter_does_not_read_as_charging() {
  TEST_ASSERT_FALSE(isChargingRate(0.0f));
  TEST_ASSERT_FALSE(isChargingRate(0.4f));
  TEST_ASSERT_FALSE(isChargingRate(-0.6f));
}

// A real charge current clears the threshold plainly.
void test_a_real_charge_current_reads_as_charging() {
  TEST_ASSERT_TRUE(isChargingRate(1.0f));
  TEST_ASSERT_TRUE(isChargingRate(12.0f));
}

// --- shouldEnterIdleSleep ------------------------------------------------

void test_idle_sleep_fires_after_the_timeout_when_not_charging() {
  TEST_ASSERT_FALSE(shouldEnterIdleSleep(kIdleSleepMs - 1, false));
  TEST_ASSERT_TRUE(shouldEnterIdleSleep(kIdleSleepMs, false));
}

// The one case the whole function exists for: plugged in and idle should
// never sleep, because sleeping saves nothing while USB is doing the powering
// anyway, and it trades the charging animation for a dark tube.
void test_idle_sleep_never_fires_while_charging() {
  TEST_ASSERT_FALSE(shouldEnterIdleSleep(kIdleSleepMs, true));
  TEST_ASSERT_FALSE(shouldEnterIdleSleep(kIdleSleepMs * 10, true));
}

// --- isPlausibleReading --------------------------------------------------

// The bug this function exists for, reproduced exactly. Adafruit's begin()
// resets the MAX17048, and for ~250 ms afterwards it reports zeros. Before
// this gate existed those zeros classified as kCritical and put a console on
// a full battery straight into deep sleep, one second into boot.
void test_the_gauges_cold_reading_is_rejected() {
  TEST_ASSERT_FALSE(isPlausibleReading(0.0f, 0.00f));
}

void test_a_normal_reading_is_accepted() {
  TEST_ASSERT_TRUE(isPlausibleReading(88.5f, 4.09f));
  TEST_ASSERT_TRUE(isPlausibleReading(50.0f, 3.70f));
}

// A genuinely flat battery must still be believed -- this gate is about the
// chip not being ready, and it must never swallow the one reading the
// critical-shutdown path exists to act on.
void test_a_genuinely_empty_battery_is_still_believed() {
  TEST_ASSERT_TRUE(isPlausibleReading(2.0f, 3.20f));
  TEST_ASSERT_TRUE(isPlausibleReading(0.0f, 3.00f));
}

// A freshly-charged cell reads slightly over 100%; that is the gauge being
// normal, not the gauge being broken.
void test_slightly_over_full_is_accepted() {
  TEST_ASSERT_TRUE(isPlausibleReading(102.0f, 4.20f));
}

void test_readings_outside_the_physical_range_are_rejected() {
  TEST_ASSERT_FALSE(isPlausibleReading(50.0f, 1.5f));   // below protection cut
  TEST_ASSERT_FALSE(isPlausibleReading(50.0f, 6.0f));   // above any LiPo
  TEST_ASSERT_FALSE(isPlausibleReading(-1.0f, 3.7f));   // nonsense percent
  TEST_ASSERT_FALSE(isPlausibleReading(500.0f, 3.7f));  // nonsense percent
}

// Belt and braces on the failure mode itself: whatever else changes, a
// rejected reading must never be the one that reaches classifyPowerLevel().
void test_the_cold_reading_would_have_been_critical_if_let_through() {
  TEST_ASSERT_EQUAL(static_cast<int>(PowerLevel::kCritical),
                    static_cast<int>(classifyPowerLevel(0.0f,
                                                        PowerLevel::kNormal)));
  TEST_ASSERT_FALSE(isPlausibleReading(0.0f, 0.0f));
}

}  // namespace

int main(int, char**) {
  UNITY_BEGIN();
  RUN_TEST(test_a_healthy_battery_starting_normal_stays_normal);
  RUN_TEST(test_discharging_from_normal_enters_low_at_the_threshold);
  RUN_TEST(test_discharging_from_normal_can_reach_critical_directly);
  RUN_TEST(test_low_does_not_clear_until_past_its_own_recovery_threshold);
  RUN_TEST(test_critical_recovers_into_low_not_normal);
  RUN_TEST(test_critical_can_recover_all_the_way_to_normal);
  RUN_TEST(test_low_survives_dithering_around_the_entry_threshold);
  RUN_TEST(test_resting_jitter_does_not_read_as_charging);
  RUN_TEST(test_a_real_charge_current_reads_as_charging);
  RUN_TEST(test_idle_sleep_fires_after_the_timeout_when_not_charging);
  RUN_TEST(test_idle_sleep_never_fires_while_charging);
  RUN_TEST(test_the_gauges_cold_reading_is_rejected);
  RUN_TEST(test_a_normal_reading_is_accepted);
  RUN_TEST(test_a_genuinely_empty_battery_is_still_believed);
  RUN_TEST(test_slightly_over_full_is_accepted);
  RUN_TEST(test_readings_outside_the_physical_range_are_rejected);
  RUN_TEST(test_the_cold_reading_would_have_been_critical_if_let_through);
  return UNITY_END();
}
