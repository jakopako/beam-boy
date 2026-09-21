#pragma once

// Transitional Settings root for Phase 10 point 1.
//
// Store and Network used to sit directly in the launcher. Moving them behind
// this scene preserves both utilities while the launcher becomes games-only.
// Phase 10 point 2 will expand this menu to WiFi, Update firmware, Store and
// Brightness; until then Network still contains its existing OTA flow.

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
