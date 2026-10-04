#pragma once

#include "core/engine.h"

namespace beamboy {

// Placeholder only: no radio, update check, flash write or reboot.
class UpdateFirmwareScene : public Scene {
 public:
  void enter(Engine& engine) override;
  void update(Engine& engine, float dt) override;
  void render(Engine& engine) override;
};

}  // namespace beamboy
