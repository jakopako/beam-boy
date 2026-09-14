#pragma once

// Beam Boy — board configuration.
//
// All hardware-specific details live here. Porting between boards should be a
// change to this file only; nothing above the Display/Input layer references a
// GPIO number or an LED driver type.

#include <NeoPixelBus.h>

namespace beamboy {
namespace board {

// ---------------------------------------------------------------------------
// Display
// ---------------------------------------------------------------------------

// The production tube is 50 px. Override from platformio.ini (or the command
// line) to develop against a different strip, e.g. -DBEAMBOY_PIXEL_COUNT=10.
//
// Nothing above the Display layer should depend on this value: game and scene
// code works in normalised 0..1 coordinates, so a game written on a 10 px strip
// runs unchanged on the 50 px tube.
#ifndef BEAMBOY_PIXEL_COUNT
#define BEAMBOY_PIXEL_COUNT 50
#endif

constexpr uint16_t kPixelCount = BEAMBOY_PIXEL_COUNT;
static_assert(kPixelCount > 0, "BEAMBOY_PIXEL_COUNT must be positive");

// Global brightness cap (0-255), applied to everything the engine draws.
//
// Measured on a 10 px WS2812 strip at a cap of 25/255:
//   full white  39.0 mA  -> 3.9 mA/px
//   all dark     5.7 mA  -> 0.57 mA/px quiescent
//   game frame   9.5 mA  (a few lit pixels -- the realistic case)
//
// Projected to the 50 px tube: ~195 mA full white, ~33 mA for a game frame.
// With the MCU at ~35 mA that is ~70 mA in normal play, or 30+ hours from a
// 2500 mAh cell -- far more headroom than the original 25 estimate assumed.
//
// The cap therefore exists mainly to bound the worst case (a full-white frame
// at 255 would pull ~2 A from the tube, beyond what the LiPo and its protection
// circuit should supply) rather than to conserve battery. Raise it as far as
// looks good; re-measure the full-white figure after any change.
constexpr uint8_t kBrightnessCap = 64;

#if defined(ARDUINO_ARCH_ESP32)

#if defined(BEAMBOY_BOARD_S3_DEVKIT)

// Espressif ESP32-S3-DevKitC-1 N16R8, the bring-up/dev board. Pins differ from
// the Feather because this board has real constraints the Feather does not:
//
//   * GPIO35-37 are wired to the octal PSRAM and must never be used, even
//     though the headers expose them and nothing stops you.
//   * GPIO0, 45 and 46 are strapping pins; a button or a pull-up on one can
//     stop the board booting.
//   * GPIO19/20 are USB D-/D+, needed for the CDC serial console.
//   * GPIO38 (or 48 on some revisions) drives the onboard WS2812.
//
// Everything below lands in the 4-18 range, clear of all of the above, with the
// analog inputs on ADC1 -- ADC2 is unusable while WiFi is on, which is exactly
// when the stick still has to work.
constexpr uint8_t kPinLedData = 17;
constexpr uint8_t kPinButtonA = 15;
constexpr uint8_t kPinButtonB = 16;
constexpr uint8_t kPinStickSw = 18;
constexpr uint8_t kPinStickX = 4;  // ADC1_CH3
constexpr uint8_t kPinStickY = 5;  // ADC1_CH4

// ⚠️ No battery on this board. The DevKitC has no LiPo charger and no fuel
// gauge, so it runs from USB only and there is deliberately no battery-sense
// wiring here. Power management (core/power.h) checks this flag and stays
// inert rather than inventing a chip that isn't there; if a build error ever
// points at a missing battery pin or peripheral, the fix is to guard that
// feature, not to invent one for this board.
constexpr bool kHasBatteryMonitor = false;

#else

// Provisional: confirm against the Feather ESP32-S3 pinout before wiring.
// Analog inputs (thumbstick, battery sense) must land on ADC1, since ADC2 is
// unusable while WiFi is active.
constexpr uint8_t kPinLedData = 5;
constexpr uint8_t kPinButtonA = 6;
constexpr uint8_t kPinButtonB = 9;
constexpr uint8_t kPinStickSw = 10;
constexpr uint8_t kPinStickX = A2;  // ADC1 (GPIO1)
constexpr uint8_t kPinStickY = A3;  // ADC1 (GPIO2)

// The Feather has no battery-sense *pin* at all -- Adafruit's own docs are
// explicit about this ("There is no pin on the Feather ESP32-S3 that returns
// battery voltage"). Instead there is a MAX17048 fuel gauge chip on the same
// I2C bus as the STEMMA QT connector (SDA/SCL, address 0x36), which reports
// voltage and state-of-charge directly rather than requiring a divider and a
// hand-rolled discharge-curve lookup. See core/power.h.
//
// No SDA/SCL constants are declared here: they are not needed. Wire.begin()
// with no arguments already resolves to this board's SDA/SCL (GPIO3/GPIO4,
// shared with A6/A7) via the Arduino core's own pins_arduino.h, and neither
// pin is used by anything else in kPinLedData/kPinButtonA/kPinButtonB/
// kPinStickSw/kPinStickX/kPinStickY above.
constexpr bool kHasBatteryMonitor = true;

#endif  // BEAMBOY_BOARD_S3_DEVKIT

constexpr uint16_t kAdcMax = 4095;

// The ESP32 RMT peripheral generates WS2812 timing in hardware, so LED output
// does not contend with the WiFi stack -- see docs/phase-4-wifi.md for the
// contention crash this avoids.
using LedMethod = NeoEsp32Rmt0800KbpsMethod;

#elif defined(BEAMBOY_NATIVE)

// Host build, used only by the unit tests under test/. The pin numbers are
// arbitrary but must be distinct, since the fake digitalRead() in the Arduino
// shim keys its per-pin state off them.
constexpr uint8_t kPinLedData = 0;
constexpr uint8_t kPinButtonA = 1;
constexpr uint8_t kPinButtonB = 2;
constexpr uint8_t kPinStickSw = 3;
constexpr uint8_t kPinStickX = 4;
constexpr uint8_t kPinStickY = 5;

// No fuel gauge to shim over I2C on the host; power.h/power.cpp are ESP32-only
// and never compiled into a native test binary, so this exists only for
// symmetry with the two ESP32 branches above.
constexpr bool kHasBatteryMonitor = false;

constexpr uint16_t kAdcMax = 1023;

using LedMethod = NeoNativeMethod;

#else
#error "Unsupported board -- add a section to board_config.h"
#endif

// The tube is WS2812B, which expects colour data in GRB order.
using LedFeature = NeoGrbFeature;
using LedBus = NeoPixelBus<LedFeature, LedMethod>;

}  // namespace board
}  // namespace beamboy
