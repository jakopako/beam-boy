#pragma once

#include <stdint.h>

// The parts of the battery policy that are pure decisions, kept deliberately
// free of any I2C or Arduino header.
//
// This split exists so the rules can be tested on the host, following the
// same pattern as net_policy.h. power.h drags in Wire and the Adafruit fuel-
// gauge driver, which cannot be exercised natively -- but the interesting part
// is not the I2C transaction, it is the small set of threshold-and-hysteresis
// questions built on top of it: is this worth warning about, is it worth
// shutting down for, is it worth staying awake for. Getting any of those wrong
// is either a console that cries wolf or one that lets its own flash get
// corrupted mid-write. They deserve tests; the register read around them does
// not.

namespace beamboy {

// Battery state, coarsened from a continuous percentage into the three things
// the UI actually reacts to differently.
enum class PowerLevel : uint8_t {
  kNormal,
  kLow,       // Warn, but keep playing.
  kCritical,  // Save and shut down before the protection circuit does it first.
};

// Percentage thresholds, not voltage. The fuel gauge already linearises the
// LiPo discharge curve (that is the whole reason to use one instead of an ADC
// divider -- see PLAN.md sec 2.1), so re-deriving a curve from raw millivolts
// here would just reintroduce the "sits near 3.7 V for 80% of the discharge,
// then falls off a cliff" problem the chip exists to solve.
//
// Each threshold has a separate, more generous recovery point. Without that,
// a percentage dithering by a fraction of a point under LED load flips the
// low-battery indicator on and off every sample -- which reads as a fault, not
// as a battery near empty.
constexpr float kLowBatteryPercent = 15.0f;
constexpr float kLowBatteryRecoverPercent = 20.0f;
constexpr float kCriticalBatteryPercent = 5.0f;
constexpr float kCriticalBatteryRecoverPercent = 10.0f;

static_assert(kLowBatteryRecoverPercent > kLowBatteryPercent,
              "recovery must sit above the entry threshold, or there is no "
              "hysteresis at all");
static_assert(kCriticalBatteryRecoverPercent > kCriticalBatteryPercent,
              "recovery must sit above the entry threshold, or there is no "
              "hysteresis at all");
static_assert(kCriticalBatteryRecoverPercent <= kLowBatteryPercent,
              "recovering from critical must land in kLow, not jump straight "
              "back to kNormal");

// `previous` carries the hysteresis: the same percentage classifies
// differently depending on which direction it was approached from, which is
// the entire point of having two thresholds per boundary instead of one.
//
// Deliberately not constexpr (unlike net_policy.h's single-expression
// functions): the ESP32 Arduino core builds are still treated as C++11 here
// (see GameEntry's comment in game_registry.h), and a multi-branch body is not
// a valid C++11 constexpr function. There is no need to evaluate this at
// compile time, so a plain inline function costs nothing.
inline PowerLevel classifyPowerLevel(float percent, PowerLevel previous) {
  if (previous == PowerLevel::kCritical) {
    if (percent < kCriticalBatteryRecoverPercent) return PowerLevel::kCritical;
    return percent >= kLowBatteryRecoverPercent ? PowerLevel::kNormal
                                                 : PowerLevel::kLow;
  }
  if (previous == PowerLevel::kLow) {
    if (percent <= kCriticalBatteryPercent) return PowerLevel::kCritical;
    return percent >= kLowBatteryRecoverPercent ? PowerLevel::kNormal
                                                 : PowerLevel::kLow;
  }
  // previous == PowerLevel::kNormal
  if (percent <= kCriticalBatteryPercent) return PowerLevel::kCritical;
  if (percent <= kLowBatteryPercent) return PowerLevel::kLow;
  return PowerLevel::kNormal;
}

// The fuel gauge's %/hr charge-rate register is noisy at rest -- a battery
// doing nothing still reports a few tenths of a percent per hour of jitter --
// so a small positive threshold, not "greater than zero", is what keeps the
// charging indicator from flickering on a battery that is simply sitting
// still on a desk.
//
// Expect this to lag reality by minutes, not seconds. CRATE is derived from
// the ModelGauge algorithm's filtered state-of-charge trend, not from a
// current measurement, so the Feather's own orange CHG LED lights well before
// this reads true. That is the sensor, not a bug -- and lowering the threshold
// to chase it just trades the lag for the resting-jitter false positives this
// threshold exists to prevent. A genuinely prompt answer would need a real
// VBUS-present signal, which this board does not expose.
constexpr float kChargingRateThreshold = 1.0f;  // percent per hour

constexpr bool isChargingRate(float charge_rate_percent_per_hour) {
  return charge_rate_percent_per_hour >= kChargingRateThreshold;
}

// Whether a reading from the gauge can be believed at all.
//
// The MAX17048 needs roughly 250 ms after power-up before its VCELL and SOC
// registers mean anything, and Adafruit's begin() resets the chip -- so the
// very first read after begin() returns 0.00 V / 0.0 %. Fed straight to
// classifyPowerLevel() that is indistinguishable from a battery about to die,
// and the console shuts itself down one second into a boot on a full charge.
//
// Voltage is the discriminator, not percent: 0 % is a legitimate thing for a
// flat battery to report, but 0.00 V is not something a board can read while
// it is executing this code -- below ~2.5 V the LiPo's own protection circuit
// has long since cut power. Anything outside the range is the chip not being
// ready (or not being there), never a battery state worth acting on.
constexpr float kMinPlausibleVoltage = 2.5f;
constexpr float kMaxPlausibleVoltage = 5.0f;
// The gauge reports slightly over 100% on a freshly-charged cell; that is
// normal and must not be mistaken for a bad reading.
constexpr float kMaxPlausiblePercent = 110.0f;

constexpr bool isPlausibleReading(float percent, float voltage) {
  return voltage >= kMinPlausibleVoltage && voltage <= kMaxPlausibleVoltage &&
         percent >= 0.0f && percent <= kMaxPlausiblePercent;
}

// How long the console sits idle before it sleeps.
constexpr uint32_t kIdleSleepMs = 120000;  // 2 minutes

// Whether the idle timer should sleep the device now.
//
// Gated on `charging`: a battery being fed by USB is going to keep charging
// regardless of what the ESP32 does, so a deep sleep saves nothing there and
// only costs the one piece of feedback a plugged-in, otherwise-idle console
// can usefully show -- the charging animation. Staying awake to show it is
// strictly better than sleeping and going dark.
constexpr bool shouldEnterIdleSleep(uint32_t idle_ms, bool charging) {
  return !charging && idle_ms >= kIdleSleepMs;
}

// For logs only. Kept next to the enum so a new level cannot be added without
// this landing in the same diff.
inline const char* powerLevelName(PowerLevel level) {
  switch (level) {
    case PowerLevel::kLow:
      return "LOW";
    case PowerLevel::kCritical:
      return "CRITICAL";
    default:
      return "normal";
  }
}

}  // namespace beamboy
