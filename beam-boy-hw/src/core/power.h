#pragma once

// Beam Boy — battery power management.
//
// Wraps the Feather's on-board MAX17048 fuel gauge (I2C, address 0x36),
// wired to the STEMMA QT bus. This chip -- not an ADC divider -- is why
// there is no kPinBatterySense in board_config.h: Adafruit's own docs for
// this board are explicit that "there is no pin on the Feather ESP32-S3 that
// returns battery voltage". The gauge already linearises the LiPo discharge
// curve and reports a usable percentage directly, so there is no divider
// ratio to get right and no discharge-curve lookup table to tune here.
//
// All of the actual *decisions* -- is this worth warning about, is it worth
// shutting down for, is it worth staying awake for -- live in
// core/power_policy.h as free functions over plain floats, so they can run on
// the host without an I2C bus. This class is the thin, untested-by-necessity
// layer around the register reads; see power_policy.h's header comment for
// why the split is worth it.
//
// board::kHasBatteryMonitor gates everything here: on a board with no fuel
// gauge (the DevKitC), begin() fails harmlessly and every other call answers
// as if nothing changed -- no charging, no low/critical warning, and the
// engine's idle timer behaves as though this class does not exist.

#include <Adafruit_MAX1704X.h>
#include <Arduino.h>

#include "board_config.h"
#include "power_policy.h"

namespace beamboy {

class Power {
 public:
  // Starts the I2C bus and probes for the gauge. Safe to call even on a board
  // with none: board::kHasBatteryMonitor short-circuits it before any I2C
  // traffic, so this is not "try and fail", it is "do not try".
  bool begin();

  // True once a fuel gauge has been found and read at least once. Every
  // reading below is meaningless while this is false, and callers must treat
  // it that way rather than acting on a stale default.
  bool available() const { return available_; }

  // Polls the gauge at most once every kPollIntervalMs, so callers can invoke
  // this every frame without hammering the I2C bus for a value that changes
  // over minutes, not milliseconds.
  void update(uint32_t now_ms);

  // 0..100. Meaningless (and left at its last-known value) while !available().
  float percent() const { return percent_; }

  // Cell voltage, for diagnostics only -- percent() is what everything else
  // should read, since it is what the gauge has already linearised.
  float voltage() const { return voltage_; }

  bool charging() const { return charging_; }

  PowerLevel level() const { return level_; }

 private:
  static constexpr uint32_t kPollIntervalMs = 1000;

  Adafruit_MAX17048 gauge_;
  bool available_ = false;
  uint32_t last_poll_ms_ = 0;

  float percent_ = 100.0f;
  float voltage_ = 0.0f;
  bool charging_ = false;
  PowerLevel level_ = PowerLevel::kNormal;
};

}  // namespace beamboy
