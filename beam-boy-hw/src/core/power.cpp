#include "power.h"

#include <Wire.h>

namespace beamboy {

bool Power::begin() {
  if (!board::kHasBatteryMonitor) {
    Serial.println("[power] no fuel gauge on this board -- power management "
                   "inert");
    return false;
  }

  Wire.begin();
  if (!gauge_.begin(&Wire)) {
    Serial.println("[power] MAX17048 not found on I2C -- running without a "
                   "battery gauge");
    return false;
  }

  available_ = true;
  // Read once immediately rather than waiting for the first update() poll, so
  // a caller that checks percent() right after begin() does not see the
  // default 100% placeholder for up to a second.
  percent_ = gauge_.cellPercent();
  voltage_ = gauge_.cellVoltage();
  charging_ = isChargingRate(gauge_.chargeRate());
  // Seeded from the first reading too. Leaving this at kNormal until the first
  // update() would mean a console booted on an already-flat battery spends its
  // first second believing it is fine -- and, worse, would classify that first
  // real sample with kNormal as its "previous" level, applying the wrong side
  // of the hysteresis at exactly the moment it matters most.
  level_ = classifyPowerLevel(percent_, PowerLevel::kNormal);
  last_poll_ms_ = millis();

  Serial.print("[power] MAX17048 found: ");
  Serial.print(percent_, 1);
  Serial.print("%  ");
  Serial.print(voltage_, 2);
  Serial.print("V  ");
  Serial.println(powerLevelName(level_));
  return true;
}

void Power::update(uint32_t now_ms) {
  if (!available_) return;
  if (now_ms - last_poll_ms_ < kPollIntervalMs) return;
  last_poll_ms_ = now_ms;

  const PowerLevel previous_level = level_;
  const bool was_charging = charging_;

  percent_ = gauge_.cellPercent();
  voltage_ = gauge_.cellVoltage();
  charging_ = isChargingRate(gauge_.chargeRate());
  level_ = classifyPowerLevel(percent_, level_);

  // Only transitions are logged, not every poll: this runs once a second for
  // the life of the console, and a line a second would bury everything else in
  // the log while telling you nothing a periodic summary does not. The
  // *changes* are the events worth timestamping -- when the warning appeared,
  // when the cable went in.
  if (level_ != previous_level) {
    Serial.print("[power] level ");
    Serial.print(powerLevelName(previous_level));
    Serial.print(" -> ");
    Serial.print(powerLevelName(level_));
    Serial.print("  (");
    Serial.print(percent_, 1);
    Serial.print("%  ");
    Serial.print(voltage_, 2);
    Serial.println("V)");
  }

  if (charging_ != was_charging) {
    Serial.print(charging_ ? "[power] charging detected at "
                           : "[power] charging stopped at ");
    Serial.print(percent_, 1);
    Serial.println("%");
  }
}

}  // namespace beamboy
