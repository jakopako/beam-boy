#pragma once

// Settings root: WiFi, Update firmware, Store and Brightness.

#include "core/engine.h"

namespace beamboy {

class SettingsScene : public Scene {
 public:
  void enter(Engine& engine) override;
  void update(Engine& engine, float dt) override;
  void render(Engine& engine) override;

 private:
  Scene* selectedScene() const;

  uint8_t selected_ = 0;
  float highlight_ = 0.0f;
};

}  // namespace beamboy
