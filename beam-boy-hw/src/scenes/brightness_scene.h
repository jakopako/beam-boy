#pragma once

#include "core/engine.h"

namespace beamboy {

class BrightnessScene : public Scene {
 public:
  void enter(Engine& engine) override;
  void exit(Engine& engine) override;
  void update(Engine& engine, float dt) override;
  void render(Engine& engine) override;

 private:
  void apply(Engine& engine, uint8_t value);
  void save(Engine& engine);

  uint8_t current_brightness_ = board::kBrightnessCap;
  float feedback_remaining_ = 0.0f;
  bool save_failed_ = false;
};

}  // namespace beamboy