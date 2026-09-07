#pragma once

// Beam Boy — Phase 5 benchmark harness.
//
// The question this exists to answer: can a scripted cartridge do a frame's
// worth of real game work inside the 16.6 ms budget?
//
// A number on its own is meaningless, so this measures a *native* implementation
// of a representative workload first. That baseline is the control: when the
// same workload is later driven by a script VM, the ratio between the two is the
// answer, and it is a far more honest measure than an absolute figure that
// depends on clock speed and compiler flags.
//
// The workload deliberately mirrors what a real game does each frame -- integrate
// a set of entities, bounce them off the ends, then draw each one -- rather than
// a synthetic arithmetic loop, because VM overhead is dominated by the cost of
// *calls across the native boundary*, not by raw arithmetic. Fifty entities and
// fifty draw calls is roughly Wormfight at its busiest.
//
//   A     step to the next entity count
//   B     hold to freeze the display so results can be read
//   Nav   pause / exit as usual

#include "core/engine.h"

namespace beamboy {

class BenchScene : public Scene {
 public:
  void enter(Engine& engine) override;
  void update(Engine& engine, float dt) override;
  void render(Engine& engine) override;

  // Number of entities in the current step of the sweep.
  uint16_t entityCount() const { return entity_count_; }

  // Mean microseconds spent in the workload, over the sample window.
  uint32_t meanWorkloadUs() const;
  uint32_t worstWorkloadUs() const { return worst_us_; }

  // True once the sweep has finished and results have been printed.
  bool complete() const { return complete_; }

 private:
  static constexpr uint16_t kMaxEntities = 100;

  // The sweep: each count runs for a fixed number of frames, then reports.
  static constexpr uint16_t kSteps[4] = {10, 25, 50, 100};
  static constexpr uint8_t kStepCount = 4;
  static constexpr uint16_t kFramesPerStep = 180;  // 3 seconds at 60 fps
  static constexpr uint32_t kFrameBudgetUs = 16667;

  void beginStep(uint8_t step);
  void reportStep(Engine& engine);

  // The measured workload. Kept in one function so that the scripted version
  // can replace exactly this and nothing else.
  void runWorkload(Engine& engine);

  struct Entity {
    float pos = 0.0f;
    float velocity = 0.0f;
    Color color;
  };

  Entity entities_[kMaxEntities];
  uint16_t entity_count_ = 0;

  uint8_t step_ = 0;
  uint16_t frames_this_step_ = 0;
  uint32_t total_us_ = 0;
  uint32_t total_update_us_ = 0;
  uint32_t total_draw_us_ = 0;
  uint32_t worst_us_ = 0;
  uint32_t last_us_ = 0;
  uint32_t update_us_ = 0;
  uint32_t draw_us_ = 0;

  bool complete_ = false;
};

}  // namespace beamboy
