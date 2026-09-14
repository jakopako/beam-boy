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

  // True once a fuel gauge has been found *and* has returned a reading that
  // can be believed -- see isPlausibleReading(). Every reading below is
  // meaningless while this is false, and callers must treat it that way rather
  // than acting on a stale default.
  //
  // Deliberately conflates "no gauge" with "gauge not ready yet": for every
  // consumer the correct response to both is identical -- show nothing, decide
  // nothing, and above all do not shut the console down. Keeping them apart
  // here would mean every call site had to remember to check two things, and
  // the one that forgot would be the one that sleeps a healthy device.
  bool available() const { return gauge_present_ && ready_; }

  // Whether the hardware is there at all, regardless of whether it has warmed
  // up. Only for telling "this board has no fuel gauge" apart from "it has one
  // that is not talking" in diagnostics -- never for gating behaviour.
  bool gaugePresent() const { return gauge_present_; }

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

  // Bounded warm-up in begin(). The gauge needs ~250 ms from reset before its
  // registers are valid; this gives it a little over that, in small steps, so
  // a gauge that is ready early costs almost nothing and one that never comes
  // up cannot stall the boot indefinitely.
  static constexpr uint8_t kWarmupAttempts = 8;
  static constexpr uint32_t kWarmupDelayMs = 50;

  // Reads all three registers and commits them to the members only if the
  // result is plausible. Returns false on a reading that must be discarded,
  // leaving the last known good values untouched.
  bool readGauge();

  Adafruit_MAX17048 gauge_;
  bool gauge_present_ = false;
  bool ready_ = false;
  bool last_read_rejected_ = false;
  uint32_t last_poll_ms_ = 0;

  // 100% until proven otherwise: if anything goes wrong, the safe default is
  // the one that never triggers a warning or a shutdown.
  float percent_ = 100.0f;
  float voltage_ = 0.0f;
  bool charging_ = false;
  PowerLevel level_ = PowerLevel::kNormal;
};

}  // namespace beamboy
