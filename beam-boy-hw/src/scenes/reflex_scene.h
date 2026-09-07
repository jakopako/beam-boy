#pragma once

// Beam Boy — Reflex.
//
// A deliberately tiny game, and the second entry in the launcher.
//
// A dot sweeps back and forth along the tube. Press A when it is inside the
// target zone. Hit it and the zone shrinks and the dot speeds up; miss and you
// lose a life. That is the whole game.
//
// It exists for three reasons: the launcher needs something to choose between,
// it proves the engine handles a second game with no changes, and it is the
// natural first cartridge to port to script in Phase 6 because it is small
// enough to reason about completely.
//
//   A   stop the dot
//   B   (unused -- pause via the nav button to exit)

#include "core/engine.h"

namespace beamboy {

class ReflexScene : public Scene {
 public:
  void enter(Engine& engine) override;
  void update(Engine& engine, float dt) override;
  void render(Engine& engine) override;

  uint32_t score() const override { return score_; }

 private:
  enum class State : uint8_t {
    kSweeping,
    kHit,
    kMiss,
    kGameOver,
  };

  void nextRound();
  void placeZone(const Display& display);

  State state_ = State::kSweeping;
  uint32_t state_started_ms_ = 0;

  float dot_ = 0.0f;
  float direction_ = 1.0f;
  float speed_ = 0.0f;

  float zone_center_ = 0.5f;
  float zone_half_width_ = 0.0f;

  uint8_t lives_ = 3;
  uint32_t score_ = 0;
  uint8_t round_ = 0;

  // Where the dot was when A was pressed, so the result can be shown in place.
  float stopped_at_ = 0.0f;
};

}  // namespace beamboy
