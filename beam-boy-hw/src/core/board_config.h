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

#if defined(ARDUINO_ARCH_ESP8266)

// Prototype wiring. The ESP8266 has only one ADC (A0), which is enough because
// a 1D display only needs the joystick's X axis -- VRy is left unconnected.
constexpr uint8_t kPinButtonA = 5;   // D1
constexpr uint8_t kPinButtonB = 4;   // D2
constexpr uint8_t kPinStickSw = 14;  // D5
constexpr uint8_t kPinStickX = A0;

// A0 on a NodeMCU reads 0-1023 over 0-3.3V thanks to an onboard divider.
constexpr uint16_t kAdcMax = 1023;

// The DMA method drives the strip from the I2S peripheral, so interrupts are
// never disabled. The bit-banged default (as used by Adafruit_NeoPixel) starves
// the WiFi stack and causes flicker and watchdog resets, so it must not be used
// here even though WiFi only arrives later in the project.
//
// Note: this method ignores any pin argument -- output is fixed to GPIO3 (RX).
// That shares the pin with serial receive, so Serial.print() and uploads work
// but Serial.read() does not. Nothing in the firmware needs it.
//
// If GPIO3 is ever needed for something else, NeoEsp8266Uart1800KbpsMethod
// outputs on GPIO2 instead and is equally interrupt-safe.
constexpr uint8_t kPinLedData = 3;
using LedMethod = NeoEsp8266Dma800KbpsMethod;

#elif defined(ARDUINO_ARCH_ESP32)

#if defined(BEAMBOY_BOARD_S3_DEVKIT)

// Espressif ESP32-S3-DevKitC-1 N16R8, used to get off the ESP8266 before the
// Feather arrives. Pins differ from the Feather because this board has real
// constraints the Feather does not:
//
//   * GPIO35-37 are wired to the octal PSRAM and must never be used, even
//     though the headers expose them and nothing stops you.
//   * GPIO0, 45 and 46 are strapping pins; a button or a pull-up on one can
//     stop the board booting.
//   * GPIO19/20 are USB D-/D+, needed for the CDC serial console.
//   * GPIO38 (or 48 on some revisions) drives the onboard WS2812.
//
// Everything below lands in the 4-18 range, clear of all of the above, with the
// analog input on ADC1 -- ADC2 is unusable while WiFi is on, which is exactly
// when the stick still has to work.
constexpr uint8_t kPinLedData = 17;
constexpr uint8_t kPinButtonA = 15;
constexpr uint8_t kPinButtonB = 16;
constexpr uint8_t kPinStickSw = 18;
constexpr uint8_t kPinStickX = 4;  // ADC1_CH3

// ⚠️ No battery on this board. The DevKitC has no LiPo charger and no
// battery-sense divider, so it runs from USB only and there is deliberately no
// kPinBatterySense here. Power management is Feather-only work; if a build
// error ever points at a missing battery pin, the fix is to guard that feature,
// not to invent a pin number for this board.

#else

// Provisional: confirm against the Feather ESP32-S3 pinout before wiring.
// Analog inputs (thumbstick, battery sense) must land on ADC1, since ADC2 is
// unusable while WiFi is active.
constexpr uint8_t kPinLedData = 5;
constexpr uint8_t kPinButtonA = 6;
constexpr uint8_t kPinButtonB = 9;
constexpr uint8_t kPinStickSw = 10;
constexpr uint8_t kPinStickX = A2;

#endif  // BEAMBOY_BOARD_S3_DEVKIT

constexpr uint16_t kAdcMax = 4095;

// The ESP32 RMT peripheral generates WS2812 timing in hardware, so LED output
// does not contend with the WiFi stack. This is the main reason to move off the
// ESP8266: see docs/phase-4-wifi.md for the crash that motivated it.
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
