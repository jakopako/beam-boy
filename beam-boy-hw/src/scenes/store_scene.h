#pragma once

// Beam Boy — cartridge store scene.
//
// Entering this utility scene is the user's explicit opt-in to turn the radio
// on and talk to the store. It reuses the same Network state machine as the
// provisioning scene, then downloads script cartridges into /games/<id>/ so the
// existing CartridgeStore/GameList path can pick them up immediately.

#include "core/cartridge_store.h"
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

// Whether an index entry is new, matches what's installed, or has an update
// waiting -- computed once after the index is fetched (and again after an
// install), never live: the index snapshot from this connection is what's
// compared against, not a fresh network round-trip per entry. See
// StoreScene::computeStatuses().
enum class CartridgeStatus : uint8_t {
  kNotInstalled,
  kUpToDate,
  kUpdateAvailable,
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
  void computeStatuses();
  void fail(const char* reason);
  void drawBusy(Engine& engine, const Color& color);
  void drawReady(Engine& engine);

  Network net_;
  StoreIndex index_;
  // Rescanned after fetchIndex() and after every install, purely to drive
  // computeStatuses() -- the engine's own gameList() stays the source of
  // truth for what is actually launchable.
  CartridgeStore installed_;
  CartridgeStatus statuses_[StoreIndex::kMaxEntries] = {};
  StoreState state_ = StoreState::kConnecting;
  uint8_t selected_ = 0;
  float phase_ = 0.0f;
  uint32_t settled_at_ms_ = 0;
  const char* error_ = "";
};

}  // namespace beamboy
