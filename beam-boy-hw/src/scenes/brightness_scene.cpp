#include "scenes/brightness_scene.h"

#include "core/brightness_policy.h"

namespace beamboy {
namespace {

constexpr float kFeedbackSeconds = 0.6f;
constexpr Color kDefaultMarker(0, 180, 255);

}  // namespace

void BrightnessScene::enter(Engine& engine) {
  current_brightness_ = brightness::clamp(engine.display().brightness());
  engine.display().setBrightness(current_brightness_);
  feedback_remaining_ = 0.0f;
  save_failed_ = false;
}

void BrightnessScene::exit(Engine& engine) {
  save(engine);
}

void BrightnessScene::apply(Engine& engine, uint8_t value) {
  if (value == current_brightness_) return;
  current_brightness_ = value;
  engine.display().setBrightness(value);
  // Stage in RAM so the engine's sleep/critical-shutdown flush saves it too.
  engine.storage().setBrightness(value);
  feedback_remaining_ = 0.0f;
  Serial.print("[brightness] preview: ");
  Serial.println(value);
}

void BrightnessScene::save(Engine& engine) {
  engine.storage().commit();
  save_failed_ = engine.storage().dirty();
  feedback_remaining_ = kFeedbackSeconds;
  if (save_failed_) {
    Serial.println("[brightness] save failed; pending changes retained for retry");
  } else {
    Serial.println("[brightness] saved");
  }
}

void BrightnessScene::update(Engine& engine, float dt) {
  if (feedback_remaining_ > 0.0f) feedback_remaining_ -= dt;

  if (engine.input().pressed(Button::kStick)) {
    apply(engine, brightness::kDefault);
  } else {
    apply(engine, brightness::stepped(current_brightness_,
                                     engine.input().navDelta()));
  }

  if (engine.input().pressed(Button::kA)) save(engine);
}

void BrightnessScene::render(Engine& engine) {
  Display& display = engine.display();
  display.clear();

  const float progress =
      board::kBrightnessCap == brightness::kMinimum
          ? 0.0f
          : static_cast<float>(current_brightness_ - brightness::kMinimum) /
                (board::kBrightnessCap - brightness::kMinimum);
  const float minimum_fill = fminf(1.0f, 3.0f * display.pixelWidth());
  const float fill = minimum_fill + (1.0f - minimum_fill) * progress;
  const Color bar = feedback_remaining_ > 0.0f
                        ? (save_failed_ ? colors::kRed : colors::kGreen)
                        : colors::kWhite;
  display.span(0.0f, fill, bar);
  // Keep at least one full pixel visible, even on a short development strip.
  display.rawPixel(0, bar);
  if (display.pixelCount() > 1) {
    display.rawPixel(display.pixelCount() - 1,
                     kDefaultMarker.scaled(
                         current_brightness_ == brightness::kDefault ? 1.0f
                                                                    : 0.35f));
  }
}

}  // namespace beamboy
