#include "power.h"

#include <Wire.h>

namespace beamboy {

bool Power::readGauge() {
  const float percent = gauge_.cellPercent();
  const float voltage = gauge_.cellVoltage();
  // Committed together or not at all. Letting a rejected reading through to
  // the members would defeat the point: percent_ is what the shutdown decision
  // is made on, so a single 0.0 landing there is all it takes.
  if (!isPlausibleReading(percent, voltage)) return false;

  percent_ = percent;
  voltage_ = voltage;
  charging_ = isChargingRate(gauge_.chargeRate());
  return true;
}

bool Power::begin() {
  if (!board::kHasBatteryMonitor) {
    Serial.println("[power] no fuel gauge on this board -- power management "
                   "inert");
    return false;
  }

  Wire.begin();
  // The gauge is the only device on this bus and is rated for 400 kHz, so
  // there is no reason to sit at the Arduino default of 100 kHz. Wire is
  // blocking, and update()'s three register reads land inside a frame: at
  // 100 kHz that costs ~3.3 ms of the 16.7 ms budget once a second, which
  // dominates the worst-frame figure even though average frames are unaffected.
  Wire.setClock(400000);
  if (!gauge_.begin(&Wire)) {
    Serial.println("[power] MAX17048 not found on I2C -- running without a "
                   "battery gauge");
    return false;
  }

  gauge_present_ = true;

  // Wait for the chip to warm up rather than trusting its first answer.
  // Adafruit's begin() resets the gauge, and for ~250 ms afterwards it reports
  // 0.00 V / 0.0 % -- which classifies as CRITICAL and shuts the console down
  // on a full battery. Blocking here for a fraction of a second is worth it to
  // make the banner below meaningful; update() applies the same gate anyway,
  // so a gauge that is slower than this budget still recovers on its own.
  for (uint8_t attempt = 0; attempt < kWarmupAttempts; ++attempt) {
    if (readGauge()) {
      ready_ = true;
      break;
    }
    delay(kWarmupDelayMs);
  }
  last_poll_ms_ = millis();

  if (!ready_) {
    // Not fatal: percent_ is still at its safe 100% default, available() stays
    // false, and every consumer behaves as though the board has no gauge.
    Serial.println("[power] MAX17048 found but returned no usable reading -- "
                   "continuing without battery management for now");
    return false;
  }

  // Seeded from the first real reading. Leaving this at kNormal until the
  // first update() would mean a console booted on an already-flat battery
  // spends its first second believing it is fine -- and, worse, would classify
  // that first real sample with kNormal as its "previous" level, applying the
  // wrong side of the hysteresis at exactly the moment it matters most.
  level_ = classifyPowerLevel(percent_, PowerLevel::kNormal);

  Serial.print("[power] MAX17048 ready: ");
  Serial.print(percent_, 1);
  Serial.print("%  ");
  Serial.print(voltage_, 2);
  Serial.print("V  ");
  Serial.println(powerLevelName(level_));
  return true;
}

void Power::update(uint32_t now_ms) {
  if (!gauge_present_) return;
  if (now_ms - last_poll_ms_ < kPollIntervalMs) return;
  last_poll_ms_ = now_ms;

  const PowerLevel previous_level = level_;
  const bool was_charging = charging_;
  const bool was_ready = ready_;

  if (!readGauge()) {
    // Keep the last known good state and say so once, rather than every
    // second. A gauge that stops answering mid-session is a real fault worth
    // seeing in the log, but it must not be allowed to shut the console down.
    if (!last_read_rejected_) {
      Serial.println("[power] implausible gauge reading discarded -- holding "
                     "last known good value");
      last_read_rejected_ = true;
    }
    return;
  }
  last_read_rejected_ = false;

  // A gauge that only became readable after boot still needs its level seeded
  // from kNormal rather than hysteresis-classified against a level that was
  // never based on a real measurement.
  ready_ = true;
  level_ = classifyPowerLevel(percent_,
                              was_ready ? level_ : PowerLevel::kNormal);

  if (!was_ready) {
    Serial.print("[power] gauge now readable: ");
    Serial.print(percent_, 1);
    Serial.print("%  ");
    Serial.print(voltage_, 2);
    Serial.print("V  ");
    Serial.println(powerLevelName(level_));
    return;
  }

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
