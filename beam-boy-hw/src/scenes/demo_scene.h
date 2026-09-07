#pragma once

// Phase 1 demo — exercises every part of the core engine.
//
// Steer a glowing dot with the joystick. The dot is drawn anti-aliased and
// leaves a fading trail, which on a short strip is the clearest demonstration
// that positions are continuous rather than snapped to pixels.
//
//   Joystick  move
//   A         drop a marker (and score a point)
//   B         hold to show the binary score readout
//   Stick     toggle the trail, to make the anti-aliasing obvious

#include "core/engine.h"

namespace beamboy {

class DemoScene : public Scene {
 public:
  void enter(Engine& engine) override;
  void update(Engine& engine, float dt) override;
  void render(Engine& engine) override;

 private:
  static constexpr uint8_t kMaxMarkers = 8;

  float position_ = 0.5f;
  float velocity_ = 0.0f;

  float markers_[kMaxMarkers];
  uint8_t marker_count_ = 0;
  uint32_t score_ = 0;

  bool trail_enabled_ = true;
  uint32_t score_shown_at_ = 0;
  bool showing_score_ = false;
  bool was_showing_score_ = false;
};

}  // namespace beamboy
