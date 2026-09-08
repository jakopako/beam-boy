#include "scenes/reflex_scene.h"

#include <math.h>

namespace beamboy {
namespace {

// Speeds are in normalised units per second, so a sweep takes the same time on
// a 10 px strip as on the 50 px tube.
constexpr float kStartSpeed = 0.55f;
constexpr float kSpeedPerRound = 0.075f;
constexpr float kMaxSpeed = 2.2f;

// The zone shrinks each round but never below a size that is actually hittable.
constexpr float kStartZoneHalf = 0.13f;
constexpr float kZoneShrink = 0.9f;
constexpr float kMinZoneHalf = 0.025f;

// Keep the zone away from the very ends, where the dot turns around and would
// otherwise linger inside it -- that turns a reflex test into a waiting game.
constexpr float kZoneMargin = 0.18f;

constexpr uint32_t kResultMs = 700;
constexpr uint8_t kStartingLives = 3;

const Color kDotColor(255, 255, 255);
// Actually green. The previous (0,180,120) was a spring-green whose blue
// content dominated once the brightness cap scaled it down to single digits.
const Color kZoneColor(0, 255, 40);
const Color kHitColor(140, 255, 140);
const Color kMissColor(255, 40, 30);
const Color kLifeColor(0, 255, 90);

}  // namespace

void ReflexScene::enter(Engine& engine) {
  lives_ = kStartingLives;
  score_ = 0;
  round_ = 0;
  speed_ = kStartSpeed;
  zone_half_width_ = kStartZoneHalf;

  dot_ = 0.0f;
  direction_ = 1.0f;

  randomSeed(micros());
  placeZone(engine.display());

  state_ = State::kSweeping;
  state_started_ms_ = millis();

  engine.display().clear();
}

void ReflexScene::placeZone(const Display& display) {
  // Never smaller than a pixel and a half, or it cannot be hit on a short strip
  // no matter how good your reflexes are.
  const float floor_half = display.pixelWidth() * 0.75f;
  if (zone_half_width_ < floor_half) zone_half_width_ = floor_half;
  if (zone_half_width_ < kMinZoneHalf) zone_half_width_ = kMinZoneHalf;

  const float low = kZoneMargin + zone_half_width_;
  const float high = 1.0f - kZoneMargin - zone_half_width_;

  if (high <= low) {
    zone_center_ = 0.5f;
    return;
  }

  const float t = random(0, 1001) / 1000.0f;
  zone_center_ = low + t * (high - low);
}

void ReflexScene::nextRound() {
  round_++;
  speed_ = kStartSpeed + kSpeedPerRound * round_;
  if (speed_ > kMaxSpeed) speed_ = kMaxSpeed;
  zone_half_width_ *= kZoneShrink;
}

void ReflexScene::update(Engine& engine, float dt) {
  Input& input = engine.input();
  Display& display = engine.display();

  if (state_ == State::kGameOver) {
    if (millis() - state_started_ms_ > 700 && input.pressed(Button::kA)) {
      enter(engine);
    }
    return;
  }

  if (state_ == State::kHit || state_ == State::kMiss) {
    if (millis() - state_started_ms_ >= kResultMs) {
      if (lives_ == 0) {
        state_ = State::kGameOver;
        state_started_ms_ = millis();
        return;
      }
      nextRound();
      placeZone(display);
      state_ = State::kSweeping;
      state_started_ms_ = millis();
    }
    return;
  }

  // --- Sweeping ------------------------------------------------------------

  dot_ += direction_ * speed_ * dt;

  // Bounce off both ends. Clamping as well as flipping keeps the dot on screen
  // even if a long frame overshot the end.
  if (dot_ >= 1.0f) {
    dot_ = 1.0f;
    direction_ = -1.0f;
  } else if (dot_ <= 0.0f) {
    dot_ = 0.0f;
    direction_ = 1.0f;
  }

  if (input.pressed(Button::kA)) {
    stopped_at_ = dot_;
    const float error = fabsf(dot_ - zone_center_);

    if (error <= zone_half_width_) {
      // Closer to the centre scores more, so there is something to master
      // beyond simply hitting the zone at all.
      const float accuracy = 1.0f - (error / zone_half_width_);
      score_ += 1 + static_cast<uint32_t>(accuracy * 4.0f);
      state_ = State::kHit;
    } else {
      if (lives_ > 0) lives_--;
      state_ = State::kMiss;
    }
    state_started_ms_ = millis();
  }
}

void ReflexScene::render(Engine& engine) {
  Display& display = engine.display();

  if (state_ == State::kGameOver) {
    display.clear();
    const uint32_t elapsed = millis() - state_started_ms_;
    if (elapsed < 500) {
      const float progress = elapsed / 500.0f;
      display.span(0.0f, progress, kMissColor, 0.5f * (1.0f - progress));
    } else {
      engine.renderScore(score_, elapsed - 500);
    }
    return;
  }

  display.clear();

  // The target zone is always drawn, so the player can aim before the dot
  // arrives rather than reacting to something they cannot see coming.
  display.span(zone_center_ - zone_half_width_, zone_center_ + zone_half_width_,
               kZoneColor, 0.35f);
  display.point(zone_center_, kZoneColor, 0.7f);

  if (state_ == State::kSweeping) {
    display.point(dot_, kDotColor);
    // A short trail behind the dot shows which way it is travelling, which
    // matters at high speed on a short strip.
    display.point(dot_ - direction_ * display.pixelWidth(), kDotColor, 0.35f);
  } else {
    const uint32_t elapsed = millis() - state_started_ms_;
    const float fade = 1.0f - (static_cast<float>(elapsed) / kResultMs);
    const Color& color = state_ == State::kHit ? kHitColor : kMissColor;

    if (state_ == State::kHit) {
      // A hit flashes the zone, tying the reward to the place it happened.
      display.span(zone_center_ - zone_half_width_,
                   zone_center_ + zone_half_width_, color, fade);
    }
    display.point(stopped_at_, color, fade);
  }

  for (uint8_t i = 0; i < lives_; i++) {
    display.rawPixel(display.pixelCount() - 1 - i, kLifeColor.scaled(0.35f));
  }
}

}  // namespace beamboy
