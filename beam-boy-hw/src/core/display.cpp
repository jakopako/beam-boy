#include "display.h"

namespace beamboy {
namespace {

float clamp01(float value) {
  if (value < 0.0f) return 0.0f;
  if (value > 1.0f) return 1.0f;
  return value;
}

uint8_t addChannel(uint8_t existing, uint16_t addition) {
  const uint16_t sum = static_cast<uint16_t>(existing) + addition;
  return sum > 255 ? 255 : static_cast<uint8_t>(sum);
}

}  // namespace

Color Color::hsv(float hue, float saturation, float value) {
  hue = fmodf(hue, 1.0f);
  if (hue < 0.0f) hue += 1.0f;

  const float h = hue * 6.0f;
  const int sector = static_cast<int>(h);
  const float f = h - sector;

  const float p = value * (1.0f - saturation);
  const float q = value * (1.0f - saturation * f);
  const float t = value * (1.0f - saturation * (1.0f - f));

  float r = 0.0f, g = 0.0f, b = 0.0f;
  switch (sector % 6) {
    case 0:
      r = value;
      g = t;
      b = p;
      break;
    case 1:
      r = q;
      g = value;
      b = p;
      break;
    case 2:
      r = p;
      g = value;
      b = t;
      break;
    case 3:
      r = p;
      g = q;
      b = value;
      break;
    case 4:
      r = t;
      g = p;
      b = value;
      break;
    default:
      r = value;
      g = p;
      b = q;
      break;
  }

  return Color(static_cast<uint8_t>(r * 255.0f),
               static_cast<uint8_t>(g * 255.0f),
               static_cast<uint8_t>(b * 255.0f));
}

Display::Display() : strip_(board::kPixelCount, board::kPinLedData) {}

void Display::begin() {
  strip_.Begin();
  clear();
  present();
}

void Display::clear() {
  for (uint16_t i = 0; i < board::kPixelCount; i++) {
    buffer_[i] = Color();
  }
}

void Display::addToPixel(uint16_t index, const Color& color, float weight) {
  if (index >= board::kPixelCount || weight <= 0.0f) return;

  // Scale and blend in one pass. Converting the weight to fixed-point here,
  // rather than calling scaled() and blending the result, keeps the single
  // unavoidable float->int conversion and drops the temporary Color entirely --
  // cheap on any target, and the difference that matters on a core without an
  // FPU.
  const uint16_t f =
      weight >= 1.0f ? 256 : static_cast<uint16_t>(weight * 256.0f);
  if (f == 0) return;

  Color& target = buffer_[index];

  // Additive blending: overlapping sprites brighten rather than overwrite,
  // which reads better than last-write-wins when entities cross on a 1D line.
  // The +128 rounds instead of truncating -- anti-aliased points spend most of
  // their time at partial weights, so a systematic downward bias here both dims
  // and discolours them.
  target.r =
      addChannel(target.r, (static_cast<uint16_t>(color.r) * f + 128) >> 8);
  target.g =
      addChannel(target.g, (static_cast<uint16_t>(color.g) * f + 128) >> 8);
  target.b =
      addChannel(target.b, (static_cast<uint16_t>(color.b) * f + 128) >> 8);
}

void Display::point(float pos, const Color& color, float intensity) {
  pos += shake_;
  if (pos < 0.0f || pos > 1.0f) return;
  if (reversed_) pos = 1.0f - pos;

  intensity = clamp01(intensity);
  if (intensity <= 0.0f) return;

  // Map to a continuous pixel coordinate, then split the light between the two
  // pixels it falls between. A point at exactly 3.0 lights only pixel 3; one at
  // 3.4 lights pixel 3 at 60% and pixel 4 at 40%.
  const float exact = pos * (board::kPixelCount - 1);
  const uint16_t lower = static_cast<uint16_t>(exact);
  const float frac = exact - lower;

  addToPixel(lower, color, (1.0f - frac) * intensity);
  if (frac > 0.0f) {
    addToPixel(lower + 1, color, frac * intensity);
  }
}

void Display::span(float from, float to, const Color& color, float intensity) {
  from += shake_;
  to += shake_;

  if (from > to) {
    const float tmp = from;
    from = to;
    to = tmp;
  }

  from = clamp01(from);
  to = clamp01(to);
  intensity = clamp01(intensity);
  if (intensity <= 0.0f) return;

  if (reversed_) {
    const float flipped_from = 1.0f - to;
    to = 1.0f - from;
    from = flipped_from;
  }

  const float start = from * (board::kPixelCount - 1);
  const float end = to * (board::kPixelCount - 1);

  const uint16_t first = static_cast<uint16_t>(start);
  const uint16_t last = static_cast<uint16_t>(end);

  if (first == last) {
    // The whole span sits inside one pixel, so light it proportionally to how
    // much of the pixel it actually covers.
    addToPixel(first, color, (end - start) * intensity);
    return;
  }

  // Partially-covered first and last pixels, fully-covered ones between.
  addToPixel(first, color, (1.0f - (start - first)) * intensity);
  for (uint16_t i = first + 1; i < last && i < board::kPixelCount; i++) {
    addToPixel(i, color, intensity);
  }
  addToPixel(last, color, (end - last) * intensity);
}

void Display::fade(float amount) {
  const float keep = clamp01(1.0f - amount);
  for (uint16_t i = 0; i < board::kPixelCount; i++) {
    buffer_[i] = buffer_[i].scaled(keep);
  }
}

void Display::rawPixel(uint16_t index, const Color& color) {
  if (index >= board::kPixelCount) return;
  const uint16_t target = reversed_ ? board::kPixelCount - 1 - index : index;
  buffer_[target] = color;
}

void Display::present() {
  // Never wait for the previous transfer. On the ESP32-S3 the RMT peripheral
  // drives WS2812 timing in hardware, so this should basically never be
  // false, but skipping a frame if it ever is stays invisible at 60 fps and
  // keeps present() bounded no matter what.
  if (!strip_.CanShow()) return;

  // The brightness cap is applied here, at the boundary, so games cannot exceed
  // it however they draw. It bounds worst-case current draw as well as setting
  // the overall look.
  for (uint16_t i = 0; i < board::kPixelCount; i++) {
    const Color& c = buffer_[i];
    // Round rather than truncate. At the low end -- a dim zone at 0.35
    // intensity lands around 15/255 after the cap -- there are few steps left
    // to express a hue with, so a consistent downward bias visibly shifts
    // colours.
    strip_.SetPixelColor(i, RgbColor((c.r * brightness_ + 127) / 255,
                                     (c.g * brightness_ + 127) / 255,
                                     (c.b * brightness_ + 127) / 255));
  }
  strip_.Show();
}

}  // namespace beamboy
