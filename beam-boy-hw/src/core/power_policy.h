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
// shutting down for, is it charging or full. Getting any of those wrong
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

// The level every warning and shutdown decision should act on, as opposed to
// the battery's own classification.
//
// On USB the Feather's load-sharing charger runs the console from VBUS, so a
// flat cell is no longer a threat: there is nothing to warn about and no
// cutoff to get ahead of. Shutting down then would only interrupt someone who
// has just done the right thing by plugging in.
//
// The battery's own level keeps being classified underneath (with its
// hysteresis intact), so pulling the cable on a near-empty cell brings the
// warning -- or the safe shutdown -- straight back on the next frame.
constexpr PowerLevel effectivePowerLevel(PowerLevel battery_level,
                                         bool usb_present) {
  return usb_present ? PowerLevel::kNormal : battery_level;
}

// Where the energy is coming from and where it is going, as far as the UI
// cares.
enum class ChargeState : uint8_t {
  kOnBattery,  // No USB power: the battery is running the console.
  kCharging,   // USB present, charger still pushing current into the cell.
  kFull,       // USB present, charge complete or in its final taper.
};

// The LiPo charger works in two phases: constant current until the cell
// reaches ~4.2 V, then constant voltage while the current tapers off. Cell
// voltage therefore stops being informative the moment CV begins -- it sits at
// ~4.2 V for the last 20-30% of the charge, and reads high while it does,
// because the gauge is measuring a cell with current being forced into it.
// Voltage alone would declare "full" at ~75%.
//
// So "full" needs both: the voltage proving CV has been reached, and the
// gauge's state-of-charge agreeing that the taper is mostly done. Not 100%:
// the charger terminates (and the cell then relaxes) while the gauge's model
// often still reads in the high 90s, so a 100% rule might never be met.
constexpr float kFullVoltage = 4.15f;
constexpr float kFullPercent = 98.0f;

// Once full, the charger stops and the cell relaxes from ~4.2 V towards its
// resting voltage. Re-applying kFullVoltage at that point would immediately
// flip back to "charging" even though no current is flowing -- and then sit
// there for hours, because the charger does not restart until the cell has
// sagged to its recharge threshold (~3.95-4.05 V, depending on the charger).
// So kFull is latched and only released once the cell has genuinely dropped
// to where a recharge cycle will begin.
constexpr float kRechargeVoltage = 4.0f;

static_assert(kRechargeVoltage < kFullVoltage,
              "the latch release must sit below the entry voltage, or kFull "
              "has no hysteresis and flickers as the cell relaxes");

// `previous` carries the full-latch, exactly the way classifyPowerLevel()'s
// `previous` carries its hysteresis.
//
// `voltage` and `percent` must come from a reading isPlausibleReading()
// accepted; callers with no believable reading should not call this at all.
inline ChargeState classifyChargeState(bool usb_present, float voltage,
                                       float percent, ChargeState previous) {
  if (!usb_present) return ChargeState::kOnBattery;
  if (previous == ChargeState::kFull) {
    return voltage < kRechargeVoltage ? ChargeState::kCharging
                                      : ChargeState::kFull;
  }
  // Coming from kOnBattery covers "plugged in with an already-full cell": it
  // goes straight to kFull rather than claiming to charge a battery the
  // charger will not touch.
  return voltage >= kFullVoltage && percent >= kFullPercent
             ? ChargeState::kFull
             : ChargeState::kCharging;
}

// How long the USB-sense pin has to hold a new level before it counts. A
// connector being pushed in makes and breaks contact for a few milliseconds;
// without this, every plug-in would log (and animate) a burst of transitions.
constexpr uint32_t kUsbDebounceMs = 50;

// For logs only.
inline const char* chargeStateName(ChargeState state) {
  switch (state) {
    case ChargeState::kCharging:
      return "charging";
    case ChargeState::kFull:
      return "full";
    default:
      return "on battery";
  }
}

// Whether a reading from the gauge can be believed at all.
//
// The MAX17048 needs roughly 250 ms after power-up before its VCELL and SOC
// registers mean anything, and Adafruit's begin() resets the chip -- so the
// very first read after begin() returns 0.00 V / 0.0 %. Fed straight to
// classifyPowerLevel() that is indistinguishable from a battery about to die,
// and the console shuts itself down one second into a boot on a full charge.
//
// Voltage is the main discriminator: 0.00 V is not something a board can read
// while it is executing this code -- below ~2.5 V the LiPo's own protection
// circuit has long since cut power. Anything outside the range is the chip not
// being ready (or not being there), never a battery state worth acting on.
//
// An exact 0 % is rejected too, even at a believable voltage: on hardware the
// gauge was seen reporting 0 % alongside a normal VCELL shortly after boot
// (VCELL settles before SOC does), which still shut a charged console down.
// Rejecting it costs nothing for a genuinely draining battery: it passes
// kCriticalBatteryPercent (5 %) long before reaching 0 %, so the critical
// shutdown has already happened.
constexpr float kMinPlausibleVoltage = 2.5f;
constexpr float kMaxPlausibleVoltage = 5.0f;
constexpr float kMinPlausiblePercent = 0.01f;
// The gauge reports slightly over 100% on a freshly-charged cell; that is
// normal and must not be mistaken for a bad reading.
constexpr float kMaxPlausiblePercent = 110.0f;

constexpr bool isPlausibleReading(float percent, float voltage) {
  return voltage >= kMinPlausibleVoltage && voltage <= kMaxPlausibleVoltage &&
         percent >= kMinPlausiblePercent && percent <= kMaxPlausiblePercent;
}

// How long the console sits idle before it sleeps.
constexpr uint32_t kIdleSleepMs = 120000;  // 2 minutes

// Whether the idle timer should sleep the device now. Deliberately ignores
// USB and charging: a plugged-in console sleeps exactly like one on battery,
// and the charge state is only shown on demand (A+B in the launcher).
constexpr bool shouldEnterIdleSleep(uint32_t idle_ms) {
  return idle_ms >= kIdleSleepMs;
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
