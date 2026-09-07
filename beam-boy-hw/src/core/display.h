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

 private:
  void addToPixel(uint16_t index, const Color& color, float weight);

  board::LedBus strip_;
  Color buffer_[board::kPixelCount];
  uint8_t brightness_ = board::kBrightnessCap;
  bool reversed_ = false;
  float shake_ = 0.0f;
};

}  // namespace beamboy
