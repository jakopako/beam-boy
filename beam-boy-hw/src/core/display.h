#pragma once

// Beam Boy — display abstraction.
//
// Games draw in a normalised coordinate space where 0.0 is the first pixel and
// 1.0 is the last, rather than in pixel indices. Two things fall out of that:
//
//   * A game written on the 10 px development strip runs unchanged on the 50 px
//     tube, which is what makes downloadable cartridges portable across future
//     hardware revisions.
//   * Positions are continuous, so a dot at 0.5 can sit *between* two pixels.
//     Rendering that with anti-aliasing is the single biggest visual difference
//     on a low-resolution display: motion becomes smooth rather than steppy.

#include <Arduino.h>

#include "board_config.h"

namespace beamboy {

// Linear RGB colour, 0-255 per channel, before the brightness cap is applied.
struct Color {
  uint8_t r = 0;
  uint8_t g = 0;
  uint8_t b = 0;

  constexpr Color() = default;
  constexpr Color(uint8_t red, uint8_t green, uint8_t blue)
      : r(red), g(green), b(blue) {}

  // Scale all channels by 0..1, used for anti-aliasing and fading.
  //
  // Deliberately fixed-point: the ESP8266's LX106 core has no FPU, so every
  // float multiply here is a soft-float library call. Converting the factor to
  // a 0..256 integer once turns float multiplies and casts into integer
  // multiplies and shifts.
  //
  // Note this TRUNCATES rather than rounds, unlike the blend path in
  // addToPixel(). That is required, not an oversight: fade() calls this
  // repeatedly on its own output, and with rounding a channel at 1 scaled by
  // 0.9 gives (1*230+128)>>8 == 1, so a faded pixel would never reach black and
  // would stay faintly lit forever.
  Color scaled(float factor) const {
    if (factor <= 0.0f) return Color();
    if (factor >= 1.0f) return *this;

    const uint16_t f = static_cast<uint16_t>(factor * 256.0f);
    return Color(static_cast<uint8_t>((static_cast<uint16_t>(r) * f) >> 8),
                 static_cast<uint8_t>((static_cast<uint16_t>(g) * f) >> 8),
                 static_cast<uint8_t>((static_cast<uint16_t>(b) * f) >> 8));
  }

  static Color hsv(float hue, float saturation, float value);
};

namespace colors {
constexpr Color kBlack(0, 0, 0);
constexpr Color kWhite(255, 255, 255);
constexpr Color kRed(255, 0, 0);
constexpr Color kGreen(0, 255, 0);
constexpr Color kBlue(0, 150, 255);
constexpr Color kAmber(255, 140, 0);
}  // namespace colors

// Sine for animation, safe to call with a phase that grows without bound.
//
// This exists because plain sinf() crashes the ESP8266 once its argument gets
// large. For |x| beyond a few hundred, newlib falls out of its fast path into
// __kernel_rem_pio2f, the "huge argument" argument-reduction routine, which
// allocates a large local array. The cont task's stack is only ~4 KB, so that
// allocation overflows it and the device resets -- after several minutes of
// running perfectly, since the argument has to grow first.
//
// Every animated scene has a phase that increases every frame, so every one of
// them is a latent version of this bug. Wrapping the phase into a single period
// keeps the argument small and the fast path taken.
inline float wrappedSin(float phase) {
  constexpr float kTwoPi = 6.28318531f;
  return sinf(fmodf(phase, kTwoPi));
}

// As wrappedSin, but takes seconds and a rate in cycles per second -- the shape
// most call sites want, and it keeps the multiply inside the wrap.
//
// The wrap matters most here, because the usual argument is millis()/1000.0f,
// which climbs to ~4.3 million before the 32-bit rollover. Multiplied by a rate
// that is well past the point where sinf takes the huge-argument path, and past
// the point where a float has enough precision left to animate smoothly.
//
// That second point is worth stating plainly, because this function cannot fix
// it: past a phase of ~1e5 a float's steps are coarser than the fraction of a
// radian a smooth animation needs, so the wrap keeps the device *alive* but the
// motion still stutters. Scenes must therefore wrap their own accumulating
// phase (every hour is the convention here) rather than treating pulse() as a
// licence to let one grow without bound.
inline float pulse(float seconds, float rate) {
  return wrappedSin(seconds * rate);
}

class Display {
 public:
  Display();

  void begin();

  // --- Drawing -------------------------------------------------------------
  // Positions are normalised: 0.0 = first pixel, 1.0 = last pixel. Anything
  // outside that range is clipped rather than wrapped, so a projectile flying
  // off the end simply disappears.

  void clear();

  // Draw an anti-aliased point. A position that falls between two pixels lights
  // both, weighted by proximity, which is what makes movement look smooth on a
  // strip this short.
  void point(float pos, const Color& color, float intensity = 1.0f);

  // Draw a filled span between two normalised positions, with anti-aliased
  // ends. Used for health bars, hazard zones and the battery meter.
  void span(float from, float to, const Color& color, float intensity = 1.0f);

  // Multiply the whole framebuffer toward black. Called once per frame instead
  // of clear() to leave motion trails; 0.0 keeps everything, 1.0 clears fully.
  void fade(float amount);

  // Write a single pixel by index, bypassing the coordinate space. Intended for
  // engine-level UI such as the score readout, not for games.
  void rawPixel(uint16_t index, const Color& color);

  void present();

  // Limits how often present() actually drives the strip, in frames. 1 is every
  // frame (the default); 4 means every fourth frame, and so on.
  //
  // This exists because WS2812 output and WiFi contend for the same hardware.
  // The I2S/DMA method is interrupt-safe, which is why it was chosen, but it is
  // not free: it drives GPIO3 continuously for the length of the strip plus a
  // reset gap, and NeoPixelBus's Update() calls yield() while waiting for the
  // previous transfer to drain. During radio-critical work (a scan, an
  // association, RF calibration) that steady drumbeat of DMA and yields is
  // enough to disturb the PHY, which faults inside the SDK's own timing
  // callbacks -- DefFreqCalTimerCB, ppCheckTxIdle, pp_tx_idle_timeout -- with a
  // perfectly healthy heap.
  //
  // Slowing the refresh rather than stopping it keeps the tube alive so the user
  // can still see what the device is doing, which is the whole point of having a
  // display during provisioning.
  void setRefreshDivider(uint8_t divider) {
    refresh_divider_ = divider < 1 ? 1 : divider;
    refresh_counter_ = 0;
  }

  // --- Effects -------------------------------------------------------------

  // Offset everything drawn afterwards by a normalised amount. Used for screen
  // shake: a few frames of jitter on impact does more for the feel of a hit
  // than any amount of colour work.
  void setShake(float offset) { shake_ = offset; }
  float shake() const { return shake_; }

  // --- Geometry ------------------------------------------------------------

  uint16_t pixelCount() const { return board::kPixelCount; }

  // The normalised width of one pixel -- useful for sizing things that should
  // stay a fixed number of pixels regardless of strip length.
  float pixelWidth() const {
    return board::kPixelCount > 1 ? 1.0f / (board::kPixelCount - 1) : 1.0f;
  }

  // --- Brightness ----------------------------------------------------------

  void setBrightness(uint8_t brightness) { brightness_ = brightness; }
  uint8_t brightness() const { return brightness_; }

  // Physical orientation. If the tube is mounted with pixel 0 at the far end,
  // set this rather than rewiring; all coordinates flip.
  void setReversed(bool reversed) { reversed_ = reversed; }

#if defined(BEAMBOY_NATIVE)
  // Host-only: how many times the strip was actually driven. Used to test the
  // refresh divider, which skips transfers rather than changing their content.
  uint32_t shownCount() const { return strip_.shownCount(); }
#endif

 private:
  void addToPixel(uint16_t index, const Color& color, float weight);

  board::LedBus strip_;
  Color buffer_[board::kPixelCount];
  uint8_t brightness_ = board::kBrightnessCap;
  bool reversed_ = false;

  // See setRefreshDivider(). 1 = drive the strip every frame.
  uint8_t refresh_divider_ = 1;
  uint8_t refresh_counter_ = 0;
  float shake_ = 0.0f;
};

}  // namespace beamboy
