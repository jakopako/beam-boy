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

// --- effectivePowerLevel -------------------------------------------------

// On battery the warnings act on the battery's own level, unchanged.
void test_on_battery_the_alert_level_is_the_battery_level() {
  TEST_ASSERT_TRUE(effectivePowerLevel(PowerLevel::kNormal, false) ==
                   PowerLevel::kNormal);
  TEST_ASSERT_TRUE(effectivePowerLevel(PowerLevel::kLow, false) ==
                   PowerLevel::kLow);
  TEST_ASSERT_TRUE(effectivePowerLevel(PowerLevel::kCritical, false) ==
                   PowerLevel::kCritical);
}

// On USB neither the low-battery pixel nor the safe shutdown applies: the
// cable is running the console.
void test_on_usb_low_and_critical_are_suppressed() {
  TEST_ASSERT_TRUE(effectivePowerLevel(PowerLevel::kLow, true) ==
                   PowerLevel::kNormal);
  TEST_ASSERT_TRUE(effectivePowerLevel(PowerLevel::kCritical, true) ==
                   PowerLevel::kNormal);
}

// Pulling the cable on a near-empty battery: the battery level was tracked
// all along, so the shutdown comes straight back rather than waiting for the
// percentage to cross a threshold again.
void test_unplugging_a_critical_battery_brings_the_shutdown_back() {
  const PowerLevel battery = classifyPowerLevel(3.0f, PowerLevel::kNormal);
  TEST_ASSERT_TRUE(effectivePowerLevel(battery, true) == PowerLevel::kNormal);
  TEST_ASSERT_TRUE(effectivePowerLevel(battery, false) ==
                   PowerLevel::kCritical);
}

// --- shouldEnterIdleSleep ------------------------------------------------

void test_idle_sleep_fires_after_the_timeout() {
  TEST_ASSERT_FALSE(shouldEnterIdleSleep(kIdleSleepMs - 1));
  TEST_ASSERT_TRUE(shouldEnterIdleSleep(kIdleSleepMs));
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

// A nearly flat battery must still be believed -- this gate is about the
// chip not being ready, and it must never swallow the readings the
// critical-shutdown path exists to act on.
void test_a_nearly_empty_battery_is_still_believed() {
  TEST_ASSERT_TRUE(isPlausibleReading(kCriticalBatteryPercent, 3.40f));
  TEST_ASSERT_TRUE(isPlausibleReading(2.0f, 3.20f));
  TEST_ASSERT_TRUE(isPlausibleReading(0.5f, 3.00f));
}

// Seen on hardware: shortly after boot the gauge reports 0 % while VCELL
// already looks normal. Believing it would shut down a charged console, so an
// exact 0 % is rejected whatever the voltage says.
void test_a_zero_percent_reading_is_rejected_even_at_a_normal_voltage() {
  TEST_ASSERT_FALSE(isPlausibleReading(0.0f, 3.90f));
  TEST_ASSERT_FALSE(isPlausibleReading(0.0f, 3.00f));
}

// Rejecting 0 % is only safe because the critical shutdown fires well before
// a draining battery could get there.
void test_critical_fires_before_a_reading_would_be_rejected() {
  TEST_ASSERT_TRUE(kCriticalBatteryPercent > kMinPlausiblePercent);
  TEST_ASSERT_TRUE(isPlausibleReading(kCriticalBatteryPercent, 3.40f));
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

// --- classifyChargeState ---------------------------------------------------

void assertChargeState(ChargeState expected, ChargeState actual) {
  TEST_ASSERT_EQUAL_STRING(chargeStateName(expected), chargeStateName(actual));
}

// No cable means no charging, whatever the cell says and whatever came before.
void test_without_usb_it_is_always_on_battery() {
  assertChargeState(ChargeState::kOnBattery,
                    classifyChargeState(false, 3.7f, 50.0f,
                                        ChargeState::kOnBattery));
  assertChargeState(ChargeState::kOnBattery,
                    classifyChargeState(false, 3.7f, 50.0f,
                                        ChargeState::kCharging));
  assertChargeState(ChargeState::kOnBattery,
                    classifyChargeState(false, 4.2f, 100.0f,
                                        ChargeState::kFull));
}

// The whole point of the USB-sense pin: charging the instant the cable goes
// in.
void test_plugging_in_a_part_charged_battery_is_charging_immediately() {
  assertChargeState(ChargeState::kCharging,
                    classifyChargeState(true, 3.8f, 40.0f,
                                        ChargeState::kOnBattery));
}

// Constant-voltage phase: the cell is already at ~4.2 V but the gauge says
// there is still a good chunk to go. Voltage alone would call this full.
void test_reaching_cv_voltage_alone_is_not_full() {
  assertChargeState(ChargeState::kCharging,
                    classifyChargeState(true, 4.19f, 80.0f,
                                        ChargeState::kCharging));
}

void test_high_voltage_and_high_percentage_is_full() {
  assertChargeState(ChargeState::kFull,
                    classifyChargeState(true, kFullVoltage, kFullPercent,
                                        ChargeState::kCharging));
  assertChargeState(ChargeState::kCharging,
                    classifyChargeState(true, kFullVoltage - 0.01f, 99.0f,
                                        ChargeState::kCharging));
  assertChargeState(ChargeState::kCharging,
                    classifyChargeState(true, kFullVoltage, kFullPercent - 0.5f,
                                        ChargeState::kCharging));
}

// After termination the cell relaxes below kFullVoltage. Without the latch
// that flips straight back to "charging" while no current flows at all.
void test_full_is_latched_while_the_cell_relaxes() {
  assertChargeState(ChargeState::kFull,
                    classifyChargeState(true, 4.10f, 99.0f,
                                        ChargeState::kFull));
  assertChargeState(ChargeState::kFull,
                    classifyChargeState(true, kRechargeVoltage, 96.0f,
                                        ChargeState::kFull));
}

// Left on the cable long enough (or played on hard enough) that the charger
// starts a new cycle: that genuinely is charging again.
void test_full_releases_once_the_cell_reaches_recharge_voltage() {
  assertChargeState(ChargeState::kCharging,
                    classifyChargeState(true, kRechargeVoltage - 0.01f, 94.0f,
                                        ChargeState::kFull));
}

// Plugging in an already-topped-up battery must not claim to be charging it.
void test_plugging_in_a_full_battery_reads_full_straight_away() {
  assertChargeState(ChargeState::kFull,
                    classifyChargeState(true, 4.18f, 100.0f,
                                        ChargeState::kOnBattery));
}

// Unplug while full, plug back in after a short game: the latch must not
// survive the unplug, or a half-drained battery would be reported as full.
void test_the_full_latch_does_not_survive_an_unplug() {
  ChargeState state = ChargeState::kFull;
  state = classifyChargeState(false, 4.05f, 92.0f, state);
  state = classifyChargeState(true, 4.05f, 92.0f, state);
  assertChargeState(ChargeState::kCharging, state);
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
  RUN_TEST(test_on_battery_the_alert_level_is_the_battery_level);
  RUN_TEST(test_on_usb_low_and_critical_are_suppressed);
  RUN_TEST(test_unplugging_a_critical_battery_brings_the_shutdown_back);
  RUN_TEST(test_idle_sleep_fires_after_the_timeout);
  RUN_TEST(test_the_gauges_cold_reading_is_rejected);
  RUN_TEST(test_a_normal_reading_is_accepted);
  RUN_TEST(test_a_nearly_empty_battery_is_still_believed);
  RUN_TEST(test_a_zero_percent_reading_is_rejected_even_at_a_normal_voltage);
  RUN_TEST(test_critical_fires_before_a_reading_would_be_rejected);
  RUN_TEST(test_slightly_over_full_is_accepted);
  RUN_TEST(test_readings_outside_the_physical_range_are_rejected);
  RUN_TEST(test_the_cold_reading_would_have_been_critical_if_let_through);
  RUN_TEST(test_without_usb_it_is_always_on_battery);
  RUN_TEST(test_plugging_in_a_part_charged_battery_is_charging_immediately);
  RUN_TEST(test_reaching_cv_voltage_alone_is_not_full);
  RUN_TEST(test_high_voltage_and_high_percentage_is_full);
  RUN_TEST(test_full_is_latched_while_the_cell_relaxes);
  RUN_TEST(test_full_releases_once_the_cell_reaches_recharge_voltage);
  RUN_TEST(test_plugging_in_a_full_battery_reads_full_straight_away);
  RUN_TEST(test_the_full_latch_does_not_survive_an_unplug);
  return UNITY_END();
}
