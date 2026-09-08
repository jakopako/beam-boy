#include "scenes/bench_scene.h"

#include <math.h>

namespace beamboy {

constexpr uint16_t BenchScene::kSteps[4];

namespace {

// Entities drift at a spread of speeds and bounce off both ends, which is the
// same shape of work a real game does: integrate, test bounds, react.
constexpr float kMinSpeed = 0.05f;
constexpr float kMaxSpeed = 0.45f;

}  // namespace

void BenchScene::enter(Engine& engine) {
  step_ = 0;
  complete_ = false;
  beginStep(0);
  engine.display().clear();

  Serial.println();
  Serial.println("=== Phase 5 benchmark: NATIVE baseline ===");
  Serial.print("Frame budget: ");
  Serial.print(kFrameBudgetUs);
  Serial.print(" us at 60 fps, on ");
  Serial.print(engine.display().pixelCount());
  Serial.println(" pixels");
  Serial.print("Free heap: ");
  Serial.print(ESP.getFreeHeap());
  Serial.println(" bytes  <- the budget any VM must fit inside");
  Serial.println();
  Serial.println("naive_slowdown assumes a VM slows the draw calls too, which");
  Serial.println("it does not -- draw stays native. update_headroom is the real");
  Serial.println("pass mark: how many times slower than native the INTERPRETED");
  Serial.println("half may be and still hold 60 fps.");
  Serial.println();
  Serial.println("entities,mean_us,update_us,draw_us,worst_us,pct_of_budget,naive_slowdown,update_headroom");
}

void BenchScene::beginStep(uint8_t step) {
  step_ = step;
  entity_count_ = kSteps[step];
  if (entity_count_ > kMaxEntities) entity_count_ = kMaxEntities;

  frames_this_step_ = 0;
  total_us_ = 0;
  total_update_us_ = 0;
  total_draw_us_ = 0;
  worst_us_ = 0;

  // Deterministic setup, so every run of the benchmark measures identical work
  // and results can be compared across builds and across VMs.
  for (uint16_t i = 0; i < entity_count_; i++) {
    const float t = entity_count_ > 1
                        ? static_cast<float>(i) / (entity_count_ - 1)
                        : 0.0f;
    entities_[i].pos = t;
    entities_[i].velocity =
        (kMinSpeed + t * (kMaxSpeed - kMinSpeed)) * ((i % 2) ? 1.0f : -1.0f);
    entities_[i].color = Color::hsv(t, 1.0f, 1.0f);
  }
}

// This is the function a scripted implementation must replace, and nothing else.
// Keeping the boundary this tight is what makes the comparison meaningful.
void BenchScene::runWorkload(Engine& engine) {
  Display& display = engine.display();
  const float dt = 1.0f / 60.0f;

  // Clearing is part of a real frame's work, and without it the additive
  // blending would saturate the buffer to white within a second.
  display.clear();

  // Split the two halves. VM overhead falls almost entirely on calls across the
  // native boundary, so knowing how much of the frame is draw calls versus
  // arithmetic predicts the scripted cost far better than one combined number.
  const uint32_t update_start = micros();
  for (uint16_t i = 0; i < entity_count_; i++) {
    Entity& e = entities_[i];

    e.pos += e.velocity * dt;
    if (e.pos >= 1.0f) {
      e.pos = 1.0f;
      e.velocity = -e.velocity;
    } else if (e.pos <= 0.0f) {
      e.pos = 0.0f;
      e.velocity = -e.velocity;
    }
  }
  update_us_ = micros() - update_start;

  const uint32_t draw_start = micros();
  for (uint16_t i = 0; i < entity_count_; i++) {
    display.point(entities_[i].pos, entities_[i].color, 0.6f);
  }
  draw_us_ = micros() - draw_start;
}

void BenchScene::update(Engine& engine, float dt) {
  (void)dt;

  if (complete_) {
    if (engine.input().pressed(Button::kA)) {
      enter(engine);
    }
    return;
  }

  // Measure only the workload, not the engine's own frame overhead, so the
  // number is directly comparable with a scripted run of the same work.
  const uint32_t start = micros();
  runWorkload(engine);
  last_us_ = micros() - start;

  total_us_ += last_us_;
  total_update_us_ += update_us_;
  total_draw_us_ += draw_us_;
  if (last_us_ > worst_us_) worst_us_ = last_us_;
  frames_this_step_++;

  if (frames_this_step_ >= kFramesPerStep) {
    reportStep(engine);

    if (step_ + 1 < kStepCount) {
      beginStep(step_ + 1);
    } else {
      complete_ = true;
      Serial.println("=== benchmark complete (A to re-run) ===");
      Serial.println();
    }
  }
}

uint32_t BenchScene::meanWorkloadUs() const {
  return frames_this_step_ > 0 ? total_us_ / frames_this_step_ : 0;
}

void BenchScene::reportStep(Engine& engine) {
  (void)engine;

  const uint32_t mean = meanWorkloadUs();
  const float pct = mean * 100.0f / kFrameBudgetUs;

  // The naive figure: budget over total native cost. This UNDERSTATES the real
  // headroom, because it assumes a VM slows down the draw calls too.
  const float affordable =
      mean > 0 ? static_cast<float>(kFrameBudgetUs) / mean : 0.0f;

  const uint32_t mean_update =
      frames_this_step_ > 0 ? total_update_us_ / frames_this_step_ : 0;
  const uint32_t mean_draw =
      frames_this_step_ > 0 ? total_draw_us_ / frames_this_step_ : 0;

  // The honest figure. Draw calls stay native under any VM -- a script calls
  // beam.point() and the pixel maths inside still runs as compiled C++. Only
  // the update half is actually interpreted, so only it gets multiplied by the
  // VM's slowdown factor. This is the number a candidate has to beat.
  float update_headroom = 0.0f;
  if (mean_update > 0 && mean_draw < kFrameBudgetUs) {
    update_headroom = static_cast<float>(kFrameBudgetUs - mean_draw) /
                      static_cast<float>(mean_update);
  }

  Serial.print(entity_count_);
  Serial.print(",");
  Serial.print(mean);
  Serial.print(",");
  Serial.print(mean_update);
  Serial.print(",");
  Serial.print(mean_draw);
  Serial.print(",");
  Serial.print(worst_us_);
  Serial.print(",");
  Serial.print(pct, 2);
  Serial.print(",");
  Serial.print(affordable, 1);
  Serial.print(",");
  Serial.println(update_headroom, 1);
}

void BenchScene::render(Engine& engine) {
  Display& display = engine.display();

  // The workload already drew the entities into the framebuffer during update(),
  // so rendering is just the progress indicator on top. Clearing here would
  // throw away the very work being measured.

  if (complete_) {
    // A slow green sweep: the benchmark finished and the numbers are on serial.
    const float level = 0.3f + 0.3f * pulse(millis() / 1000.0f, 2.0f);
    display.rawPixel(0, colors::kGreen.scaled(level));
    display.rawPixel(display.pixelCount() - 1, colors::kGreen.scaled(level));
    return;
  }

  // Progress through the current step, drawn at the very first pixel. It is
  // deliberately tiny: the point is to see the workload, not the HUD.
  const float progress =
      static_cast<float>(frames_this_step_) / kFramesPerStep;
  display.rawPixel(0, colors::kAmber.scaled(0.15f + 0.6f * progress));
}

}  // namespace beamboy
