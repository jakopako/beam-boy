#pragma once

// Beam Boy — cartridge store scene.
//
// Entering this utility scene is the user's explicit opt-in to turn the radio
// on and talk to the store. It reuses the same Network state machine as the
// provisioning scene, then downloads script cartridges into /games/<id>/ so the
// existing CartridgeStore/GameList path can pick them up immediately.

#include "core/engine.h"
#include "core/network.h"
#include "core/store_index.h"

namespace beamboy {

enum class StoreState : uint8_t {
  kConnecting,
  kFetching,
  kReady,
  kInstalling,
  kSuccess,
  kFailed,
};

class StoreScene : public Scene {
 public:
  void enter(Engine& engine) override;
  void exit(Engine& engine) override;
  void idle(Engine& engine) override;
  void update(Engine& engine, float dt) override;
  void render(Engine& engine) override;

 private:
  static constexpr uint32_t kResultFlashMs = 1800;

  bool fetchIndex();
  bool installSelected(Engine& engine);
  void fail(const char* reason);
  void drawBusy(Engine& engine, const Color& color);
  void drawReady(Engine& engine);

  Network net_;
  StoreIndex index_;
  StoreState state_ = StoreState::kConnecting;
  uint8_t selected_ = 0;
  float phase_ = 0.0f;
  uint32_t settled_at_ms_ = 0;
  const char* error_ = "";
};

}  // namespace beamboy
