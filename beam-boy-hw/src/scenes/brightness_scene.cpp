#include "scenes/brightness_scene.h"

namespace beamboy {

constexpr float kAcceleration = 0.01f;

void BrightnessScene::enter(Engine& engine) {
  (void)engine;
  current_brightness_ = engine.display().brightness();
}

void BrightnessScene::update(Engine& engine, float dt) {
  const float brightness_dt = engine.input().stickX() * dt * kAcceleration;
  float new_brightness = current_brightness_ + brightness_dt;
  if (new_brightness < 0.0f) {
    new_brightness = 0.0f;
  } else if (new_brightness > board::kBrightnessCap) {
    new_brightness = board::kBrightnessCap;
  }

  if (new_brightness != current_brightness_) {
    Serial.print("[brightness] ");
    Serial.print("current: ");
    Serial.print(current_brightness_);
    Serial.print(" delta: ");
    Serial.print(brightness_dt);
    Serial.print(" new: ");
    Serial.println(new_brightness);

    current_brightness_ = new_brightness;
  }
}

void BrightnessScene::render(Engine& engine) {
  Display& display = engine.display();
  display.clear();

  engine.display().setBrightness(current_brightness_);

  float brightness_scaled = current_brightness_ / board::kBrightnessCap;

  display.span(0, brightness_scaled, colors::kWhite);
}

}  // namespace beamboy