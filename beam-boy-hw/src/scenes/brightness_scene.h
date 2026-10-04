#pragma once

#include "core/engine.h"

namespace beamboy {

class BrightnessScene : public Scene {
 public:
  void enter(Engine& engine) override;
  void update(Engine& engine, float dt) override;
  void render(Engine& engine) override;

 private:
  float current_brightness_;
};

}  // namespace beamboy