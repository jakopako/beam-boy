#include "scenes/demo_scene.h"

namespace beamboy {
namespace {

// Full-deflection speed, in normalised units per second. 0.8 crosses the strip
// in about 1.25 s, which feels responsive without being twitchy on 10 px.
constexpr float kMaxSpeed = 0.8f;

// Smoothing applied to the stick. Without it the dot stops dead when the stick
// is released, which reads as mechanical; a little momentum feels much better.
constexpr float kAcceleration = 12.0f;

// Fraction of the previous frame's brightness removed each tick.
constexpr float kTrailFade = 0.35f;

}  // namespace

void DemoScene::enter(Engine& engine) {
  position_ = 0.5f;
  velocity_ = 0.0f;
  marker_count_ = 0;
  score_ = 0;
  showing_score_ = false;
  engine.display().clear();
}

void DemoScene::update(Engine& engine, float dt) {
  Input& input = engine.input();

  // Track the button's level rather than its edges. Deriving this from
  // pressed()/released() means a single missed release edge (bounce landing
  // inside the debounce window) leaves the readout stuck on; reading the level
  // is self-correcting.
  const bool want_score = input.held(Button::kB);
  if (want_score && !showing_score_) {
    score_shown_at_ = millis();
  }
  showing_score_ = want_score;

  // Movement is suspended while the score is shown.
  if (showing_score_) return;

  if (input.pressed(Button::kStick)) {
    trail_enabled_ = !trail_enabled_;
  }

  // Ease the velocity toward what the stick is asking for.
  const float target = input.stickX() * kMaxSpeed;
  velocity_ += (target - velocity_) * kAcceleration * dt;
  position_ += velocity_ * dt;

  // Bounce off the ends, so the dot cannot be lost off the edge.
  if (position_ < 0.0f) {
    position_ = 0.0f;
    velocity_ = -velocity_ * 0.4f;
  } else if (position_ > 1.0f) {
    position_ = 1.0f;
    velocity_ = -velocity_ * 0.4f;
  }

  if (input.pressed(Button::kA)) {
    if (marker_count_ < kMaxMarkers) {
      markers_[marker_count_++] = position_;
    } else {
      // Oldest marker falls off the front.
      for (uint8_t i = 1; i < kMaxMarkers; i++) {
        markers_[i - 1] = markers_[i];
      }
      markers_[kMaxMarkers - 1] = position_;
    }
    score_++;
  }
}

void DemoScene::render(Engine& engine) {
  Display& display = engine.display();

  if (showing_score_) {
    display.clear();
    engine.renderScore(score_, millis() - score_shown_at_);
    was_showing_score_ = true;
    return;
  }

  if (trail_enabled_) {
    // Clear outright on the first frame back from the score readout, otherwise
    // the trail fade would leave the score's pixels visibly decaying over the
    // game view.
    if (was_showing_score_) {
      display.clear();
    } else {
      display.fade(kTrailFade);
    }
  } else {
    display.clear();
  }
  was_showing_score_ = false;

  // Markers dim with age, so the most recent is brightest and the oldest is
  // about to be recycled. Without this the ring buffer looks like random
  // pixels appearing and vanishing rather than a visible history.
  for (uint8_t i = 0; i < marker_count_; i++) {
    const float age = marker_count_ > 1
                          ? static_cast<float>(i) / (marker_count_ - 1)
                          : 1.0f;
    display.point(markers_[i], colors::kAmber, 0.12f + 0.33f * age);
  }

  // Hue tracks position, so colour reinforces where the dot is -- useful when
  // the strip is short and spatial resolution is scarce.
  const Color dot = Color::hsv(position_ * 0.8f, 1.0f, 1.0f);
  display.point(position_, dot);

  // A dim halo either side makes the dot read as a glowing object rather than a
  // lit pixel, and exaggerates the sub-pixel motion.
  const float halo = display.pixelWidth();
  display.point(position_ - halo, dot, 0.25f);
  display.point(position_ + halo, dot, 0.25f);
}

}  // namespace beamboy
