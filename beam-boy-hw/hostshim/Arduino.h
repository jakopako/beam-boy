#pragma once

// Beam Boy — minimal Arduino shim for host builds.
//
// This exists so that engine and game logic can be compiled and tested on a PC,
// with no board attached. It is deliberately *small*: it provides only what the
// code under test actually calls, and it makes no attempt to emulate a
// microcontroller. Anything that needs real hardware behaviour -- WiFi, LED
// timing, the cont task's stack -- is out of scope by construction and has to
// be tested on the device.
//
// The important property is that time and input are *driven by the test* rather
// than by a clock. millis() returns whatever the test last set, so a soak test
// can advance a simulated hour in a few milliseconds of real time, and a button
// test can place an edge exactly where it wants it.

#include <cmath>
#include <cstdarg>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>

// --- Arduino constants -----------------------------------------------------

#define HIGH 1
#define LOW 0
#define INPUT 0
#define OUTPUT 1
#define INPUT_PULLUP 2

// F() and PROGMEM are no-ops off-target; there is no separate flash address
// space to move string literals into.
#define PROGMEM
#define F(str) (str)
#define PSTR(str) (str)

using boolean = bool;
using byte = uint8_t;

namespace beamboy_host {

// The simulated clock and pin state. Defined inline so the shim stays
// header-only and needs no entry in build_src_filter.
struct HostState {
  uint32_t now_us = 0;
  int pin_digital[64];
  int pin_analog[64];
  int pin_mode[64];

  HostState() { reset(); }

  void reset() {
    now_us = 0;
    for (int i = 0; i < 64; i++) {
      // Buttons are wired to ground with pull-ups, so "not pressed" is HIGH.
      pin_digital[i] = HIGH;
      pin_analog[i] = 0;
      pin_mode[i] = INPUT;
    }
  }
};

inline HostState& state() {
  static HostState s;
  return s;
}

}  // namespace beamboy_host

// --- Time ------------------------------------------------------------------

inline uint32_t micros() { return beamboy_host::state().now_us; }
inline uint32_t millis() { return beamboy_host::state().now_us / 1000UL; }

// delay() advances the simulated clock instead of sleeping. A test that calls
// into code containing a delay() therefore runs at full speed, and the delay is
// still visible to anything that reads millis().
inline void delay(uint32_t ms) { beamboy_host::state().now_us += ms * 1000UL; }
inline void delayMicroseconds(uint32_t us) {
  beamboy_host::state().now_us += us;
}
inline void yield() {}

// --- GPIO ------------------------------------------------------------------

inline void pinMode(uint8_t pin, uint8_t mode) {
  beamboy_host::state().pin_mode[pin] = mode;
}
inline int digitalRead(uint8_t pin) {
  return beamboy_host::state().pin_digital[pin];
}
inline void digitalWrite(uint8_t pin, uint8_t value) {
  beamboy_host::state().pin_digital[pin] = value;
}
inline int analogRead(uint8_t pin) {
  return beamboy_host::state().pin_analog[pin];
}

// --- Maths -----------------------------------------------------------------

inline long random(long high) { return high > 0 ? rand() % high : 0; }
inline long random(long low, long high) {
  return high > low ? low + rand() % (high - low) : low;
}
inline void randomSeed(unsigned long seed) {
  srand(static_cast<unsigned>(seed));
}

template <typename T>
inline T constrain(T value, T low, T high) {
  return value < low ? low : (value > high ? high : value);
}

// --- Serial ----------------------------------------------------------------
//
// Prints to stdout, so a failing test still shows whatever the code logged on
// its way to failing.

class HostSerial {
 public:
  void begin(unsigned long) {}
  void print(const char* s) { fputs(s, stdout); }
  void print(int v) { printf("%d", v); }
  void print(unsigned int v) { printf("%u", v); }
  void print(long v) { printf("%ld", v); }
  void print(unsigned long v) { printf("%lu", v); }
  void print(float v) { printf("%f", static_cast<double>(v)); }
  void println() { fputs("\n", stdout); }
  template <typename T>
  void println(T v) {
    print(v);
    println();
  }
  void flush() { fflush(stdout); }
};

inline HostSerial& serialInstance() {
  static HostSerial s;
  return s;
}
#define Serial (serialInstance())
