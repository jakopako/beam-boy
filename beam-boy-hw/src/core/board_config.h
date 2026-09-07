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

// Provisional: confirm against the Feather ESP32-S3 pinout before wiring.
// Analog inputs (thumbstick, battery sense) must land on ADC1, since ADC2 is
// unusable while WiFi is active.
constexpr uint8_t kPinLedData = 5;
constexpr uint8_t kPinButtonA = 6;
constexpr uint8_t kPinButtonB = 9;
constexpr uint8_t kPinStickSw = 10;
constexpr uint8_t kPinStickX = A2;

constexpr uint16_t kAdcMax = 4095;

// The ESP32 RMT peripheral generates WS2812 timing in hardware, so LED output
// does not contend with the WiFi stack.
using LedMethod = NeoEsp32Rmt0800KbpsMethod;

#else
#error "Unsupported board -- add a section to board_config.h"
#endif

// The tube is WS2812B, which expects colour data in GRB order.
using LedFeature = NeoGrbFeature;
using LedBus = NeoPixelBus<LedFeature, LedMethod>;

}  // namespace board
}  // namespace beamboy
