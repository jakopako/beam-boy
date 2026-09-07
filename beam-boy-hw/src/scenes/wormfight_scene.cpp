#include "scenes/wormfight_scene.h"

#include <math.h>

namespace beamboy {
namespace {

// --- Tuning ---------------------------------------------------------------
// All distances are normalised (0 = player end, 1 = far end) and all speeds are
// per second, so the feel is identical on any strip length.

constexpr float kPlayerMaxSpeed = 0.55f;
constexpr float kPlayerAccel = 14.0f;

// The player is confined to their end of the line: this is a defence game, not
// a free-roaming one, and the tension comes from worms closing on your zone.
constexpr float kPlayerZone = 0.30f;

constexpr float kShotSpeed = 1.30f;
constexpr float kFireCooldown = 0.22f;

// Charged shot (B): hold to charge, release to fire. A charged shot pierces --
// it keeps travelling through worms until its power is spent -- which makes it
// the answer to a long worm or a cluster, rather than just "more damage".
constexpr float kChargeTimeMin = 0.25f;   // below this, treat as a normal shot
constexpr float kChargeTimeFull = 1.10f;  // fully charged
constexpr float kChargedShotSpeed = 0.95f;  // slower, so the power reads
constexpr uint8_t kChargedShotPower = 4;    // segments it can punch through

constexpr float kHitFlashTime = 0.12f;
constexpr float kMuzzleFlashTime = 0.06f;

constexpr float kParticleDrag = 2.2f;
constexpr float kParticleLife = 0.55f;

constexpr float kInvulnerableTime = 1.2f;
constexpr uint32_t kWaveClearMs = 1200;
constexpr uint32_t kDyingMs = 900;

const Color kPlayerColor(0, 200, 255);
const Color kShotColor(140, 230, 255);
const Color kChargeColor(255, 180, 40);
const Color kChargedShotColor(255, 230, 120);
const Color kWormHead(255, 40, 30);
const Color kWormBody(180, 20, 60);
const Color kLifeColor(0, 255, 90);

float clampf(float value, float low, float high) {
  if (value < low) return low;
  if (value > high) return high;
  return value;
}

}  // namespace

void WormfightScene::enter(Engine& engine) {
  state_ = State::kPlaying;
  state_started_ms_ = millis();

  player_ = 0.0f;
  player_velocity_ = 0.0f;
  lives_ = kStartingLives;
  score_ = 0;
  wave_ = 0;

  for (uint8_t i = 0; i < kMaxWorms; i++) worms_[i].active = false;
  for (uint8_t i = 0; i < kMaxShots; i++) shots_[i].active = false;
  for (uint8_t i = 0; i < kMaxParticles; i++) particles_[i].active = false;

  fire_cooldown_ = 0.0f;
  charge_ = 0.0f;
  charging_ = false;
  muzzle_flash_ = 0.0f;
  shake_time_ = 0.0f;

  randomSeed(micros());
  startWave(1);

  engine.display().clear();
}

void WormfightScene::startWave(uint8_t wave) {
  wave_ = wave;

  // Escalate on both axes: more worms, and faster ones. The segment count grows
  // more slowly, since a long worm on a short strip quickly fills the line.
  worms_remaining_in_wave_ = 2 + wave / 2;
  if (worms_remaining_in_wave_ > 8) worms_remaining_in_wave_ = 8;

  spawn_timer_ = 0.0f;
}

bool WormfightScene::spawnWorm(float head, float speed, uint8_t segments) {
  for (uint8_t i = 0; i < kMaxWorms; i++) {
    if (worms_[i].active) continue;
    worms_[i].head = head;
    worms_[i].speed = speed;
    worms_[i].segments = segments;
    worms_[i].hit_flash = 0.0f;
    worms_[i].active = true;
    return true;
  }
  return false;
}

bool WormfightScene::fire(uint8_t power, float speed) {
  if (fire_cooldown_ > 0.0f) return false;

  for (uint8_t i = 0; i < kMaxShots; i++) {
    if (shots_[i].active) continue;
    shots_[i].pos = player_;
    shots_[i].speed = speed;
    shots_[i].power = power;
    shots_[i].active = true;
    fire_cooldown_ = kFireCooldown;
    muzzle_flash_ = power > 1 ? kMuzzleFlashTime * 3.0f : kMuzzleFlashTime;

    if (power > 1) {
      emitBurst(player_, kChargeColor, 4, 0.6f);
      shake_time_ = 0.12f;
      shake_energy_ = 0.3f;
    }
    return true;
  }
  return false;
}

// Charging is driven from the button level rather than press/release edges: if a
// release edge were ever missed, an edge-driven charge would stick on forever.
void WormfightScene::updateCharge(Engine& engine, float dt) {
  const bool held = engine.input().held(Button::kB);

  if (held) {
    charging_ = true;
    charge_ += dt;
    if (charge_ > kChargeTimeFull) charge_ = kChargeTimeFull;
    return;
  }

  if (!charging_) return;

  // Released: a stab of B is just a normal shot, so B is never a dead button.
  bool fired;
  if (charge_ >= kChargeTimeMin) {
    const float ratio = clampf(
        (charge_ - kChargeTimeMin) / (kChargeTimeFull - kChargeTimeMin), 0.0f,
        1.0f);
    const uint8_t power =
        1 + static_cast<uint8_t>(ratio * (kChargedShotPower - 1) + 0.5f);
    fired = fire(power, power > 1 ? kChargedShotSpeed : kShotSpeed);
  } else {
    fired = fire(1, kShotSpeed);
  }

  // If the shot could not be released this frame (cooldown, or no free slot),
  // hold the charge and retry rather than silently swallowing a full second of
  // charging -- losing a charged shot to an invisible cooldown feels broken.
  if (!fired) return;

  charge_ = 0.0f;
  charging_ = false;
}

void WormfightScene::emitBurst(float pos, const Color& color, uint8_t count,
                               float energy) {
  for (uint8_t n = 0; n < count; n++) {
    for (uint8_t i = 0; i < kMaxParticles; i++) {
      Particle& p = particles_[i];
      if (p.active) continue;

      p.pos = pos;
      // random() returns a long; map it to a symmetric velocity range.
      const float spread = (random(-100, 101) / 100.0f);
      p.velocity = spread * energy;
      p.life = kParticleLife;
      p.color = color;
      p.active = true;
      break;
    }
  }
}

void WormfightScene::damagePlayer() {
  if (lives_ > 0) lives_--;

  shake_time_ = 0.45f;
  shake_energy_ = 1.0f;
  emitBurst(player_, kWormHead, 6, 0.8f);

  state_ = State::kDying;
  state_started_ms_ = millis();
}

void WormfightScene::updateParticles(float dt) {
  for (uint8_t i = 0; i < kMaxParticles; i++) {
    Particle& p = particles_[i];
    if (!p.active) continue;

    p.pos += p.velocity * dt;
    p.velocity -= p.velocity * kParticleDrag * dt;
    p.life -= dt;

    if (p.life <= 0.0f || p.pos < 0.0f || p.pos > 1.0f) {
      p.active = false;
    }
  }
}

void WormfightScene::update(Engine& engine, float dt) {
  Input& input = engine.input();
  Display& display = engine.display();

  // Screen shake decays regardless of state, so it can outlive the hit that
  // caused it and carry across the death transition.
  if (shake_time_ > 0.0f) {
    shake_time_ -= dt;
    const float decay = clampf(shake_time_ / 0.45f, 0.0f, 1.0f);
    const float jitter = (random(-100, 101) / 100.0f);
    display.setShake(jitter * decay * shake_energy_ * display.pixelWidth());
  } else {
    display.setShake(0.0f);
  }

  updateParticles(dt);

  // --- Non-playing states --------------------------------------------------

  if (state_ == State::kDying) {
    if (millis() - state_started_ms_ >= kDyingMs) {
      if (lives_ == 0) {
        state_ = State::kGameOver;
        state_started_ms_ = millis();
      } else {
        // Clear the line and resume the current wave, giving the player room to
        // recover rather than restarting progress.
        for (uint8_t i = 0; i < kMaxWorms; i++) worms_[i].active = false;
        for (uint8_t i = 0; i < kMaxShots; i++) shots_[i].active = false;
        player_ = 0.0f;
        player_velocity_ = 0.0f;
        state_ = State::kPlaying;
        state_started_ms_ = millis();
      }
    }
    return;
  }

  if (state_ == State::kGameOver) {
    // A deliberate pause before accepting input, so the reflexive shot that
    // killed you does not immediately restart the game.
    if (millis() - state_started_ms_ > 700 && input.pressed(Button::kA)) {
      enter(engine);
    }
    return;
  }

  if (state_ == State::kWaveClear) {
    if (millis() - state_started_ms_ >= kWaveClearMs) {
      startWave(wave_ + 1);
      state_ = State::kPlaying;
      state_started_ms_ = millis();
    }
    return;
  }

  // --- Playing -------------------------------------------------------------

  if (fire_cooldown_ > 0.0f) fire_cooldown_ -= dt;
  if (muzzle_flash_ > 0.0f) muzzle_flash_ -= dt;

  // Movement, with the same easing as the demo scene so the two feel related.
  const float target = input.stickX() * kPlayerMaxSpeed;
  player_velocity_ += (target - player_velocity_) * kPlayerAccel * dt;
  player_ += player_velocity_ * dt;

  if (player_ < 0.0f) {
    player_ = 0.0f;
    player_velocity_ = 0.0f;
  } else if (player_ > kPlayerZone) {
    player_ = kPlayerZone;
    player_velocity_ = 0.0f;
  }

  if (input.pressed(Button::kA)) fire(1, kShotSpeed);
  updateCharge(engine, dt);

  // --- Spawning ------------------------------------------------------------
  spawn_timer_ -= dt;
  if (worms_remaining_in_wave_ > 0 && spawn_timer_ <= 0.0f) {
    const float base_speed = 0.055f + 0.012f * wave_;
    const float variation = random(0, 40) / 1000.0f;
    // Cap the length relative to the strip: a worm longer than a quarter of the
    // line leaves nowhere to dodge or shoot into. On 10 px that is 2 segments;
    // on the 50 px tube it allows the full length.
    uint8_t segments = 2 + (wave_ / 3) + (random(0, 100) < 30 ? 1 : 0);
    uint8_t max_segments = display.pixelCount() / 4;
    if (max_segments < 2) max_segments = 2;
    if (max_segments > kMaxSegments) max_segments = kMaxSegments;
    if (segments > max_segments) segments = max_segments;

    // Only count the worm against the wave if a slot was actually free;
    // otherwise the wave would "spawn" worms that never appear and end early.
    if (spawnWorm(1.0f, -(base_speed + variation), segments)) {
      worms_remaining_in_wave_--;
      // Later waves overlap their spawns, so pressure builds within a wave too.
      spawn_timer_ = clampf(2.6f - 0.18f * wave_, 0.7f, 2.6f);
    } else {
      spawn_timer_ = 0.3f;  // all slots busy: retry shortly
    }
  }

  // --- Shots ---------------------------------------------------------------
  for (uint8_t i = 0; i < kMaxShots; i++) {
    Shot& shot = shots_[i];
    if (!shot.active) continue;

    shot.pos += shot.speed * dt;
    if (shot.pos > 1.0f) {
      shot.active = false;
      continue;
    }

    for (uint8_t w = 0; w < kMaxWorms; w++) {
      Worm& worm = worms_[w];
      if (!worm.active) continue;

      // Worms extend from the head toward the far end, so the tail is at a
      // higher coordinate than the head.
      const float seg = display.pixelWidth();
      const float tail = worm.head + seg * (worm.segments - 1);

      if (shot.pos >= worm.head - seg * 0.5f &&
          shot.pos <= tail + seg * 0.5f) {
        worm.segments--;
        worm.hit_flash = kHitFlashTime;
        score_++;
        emitBurst(shot.pos, kWormHead, 3, 0.45f);

        if (worm.segments == 0) {
          worm.active = false;
          score_ += 3;  // bonus for the kill, beyond the per-segment points
          emitBurst(worm.head, kWormHead, 5, 0.7f);
          shake_time_ = 0.15f;
          shake_energy_ = 0.35f;
        }

        // A charged shot spends one point of power per segment and keeps going;
        // a normal shot has a single point and so stops here.
        shot.power--;
        if (shot.power == 0) {
          shot.active = false;
          break;
        }
        // Do not break: with power left, the same step may reach the next worm.
      }
    }
  }

  // --- Worms ---------------------------------------------------------------
  bool any_active = false;
  for (uint8_t i = 0; i < kMaxWorms; i++) {
    Worm& worm = worms_[i];
    if (!worm.active) continue;
    any_active = true;

    worm.head += worm.speed * dt;
    if (worm.hit_flash > 0.0f) worm.hit_flash -= dt;

    // Reaching the player costs a life.
    if (worm.head <= player_) {
      worm.active = false;
      damagePlayer();
      return;
    }
  }

  if (!any_active && worms_remaining_in_wave_ == 0) {
    state_ = State::kWaveClear;
    state_started_ms_ = millis();
  }
}

void WormfightScene::renderPlayfield(Engine& engine) {
  Display& display = engine.display();
  const float seg = display.pixelWidth();

  // Worms: bright head, darker body, whole worm flashes white when hit.
  for (uint8_t i = 0; i < kMaxWorms; i++) {
    const Worm& worm = worms_[i];
    if (!worm.active) continue;

    const bool flashing = worm.hit_flash > 0.0f;

    for (uint8_t s = 0; s < worm.segments; s++) {
      const float pos = worm.head + seg * s;
      const Color& base = (s == 0) ? kWormHead : kWormBody;
      const Color color = flashing ? colors::kWhite : base;
      // Segments dim toward the tail, which makes the direction of travel and
      // the remaining hit count readable at a glance.
      const float intensity = flashing ? 1.0f : (s == 0 ? 1.0f : 0.55f);
      display.point(pos, color, intensity);
    }
  }

  // Shots. A charged shot is larger, warmer and has a longer trail, so its
  // power is obvious before it lands.
  for (uint8_t i = 0; i < kMaxShots; i++) {
    const Shot& shot = shots_[i];
    if (!shot.active) continue;

    if (shot.power > 1) {
      display.point(shot.pos, kChargedShotColor);
      display.point(shot.pos - seg, kChargedShotColor, 0.6f);
      display.point(shot.pos - seg * 2.0f, kChargeColor, 0.25f);
      display.point(shot.pos + seg, kChargedShotColor, 0.35f);
    } else {
      display.point(shot.pos, kShotColor);
      display.point(shot.pos - seg, kShotColor, 0.3f);
    }
  }

  // Particles fade out over their lifetime.
  for (uint8_t i = 0; i < kMaxParticles; i++) {
    const Particle& p = particles_[i];
    if (!p.active) continue;
    display.point(p.pos, p.color, (p.life / kParticleLife) * 0.7f);
  }

  // Player, with a muzzle flash and a halo so it reads as the anchor of the
  // scene rather than just another lit pixel.
  if (state_ != State::kDying) {
    display.point(player_, kPlayerColor);
    display.point(player_ + seg, kPlayerColor, 0.3f);

    if (muzzle_flash_ > 0.0f) {
      display.point(player_ + seg, colors::kWhite, 0.9f);
      display.point(player_ + seg * 2.0f, colors::kWhite, 0.4f);
    }
  }

  // Lives as pips at the very end of the line, behind the player. Using the
  // last pixels keeps them out of the playfield.
  for (uint8_t i = 0; i < lives_; i++) {
    display.rawPixel(display.pixelCount() - 1 - i, kLifeColor.scaled(0.35f));
  }

  // Charge: the player glows amber and pulses faster as the shot builds, so the
  // charge state is read from the turret itself rather than a separate HUD.
  if (charging_ && charge_ > 0.0f) {
    const float ratio = clampf(charge_ / kChargeTimeFull, 0.0f, 1.0f);
    const float rate = 6.0f + 18.0f * ratio;
    const float pulse =
        0.55f + 0.45f * sinf(millis() / 1000.0f * rate);

    display.point(player_, kChargeColor, ratio * pulse);
    if (charge_ >= kChargeTimeFull) {
      // Fully charged: a steady white core, unmistakable at a glance.
      display.point(player_, colors::kWhite, 0.8f);
    }
  }
}

void WormfightScene::render(Engine& engine) {
  Display& display = engine.display();

  if (state_ == State::kGameOver) {
    display.clear();
    const uint32_t elapsed = millis() - state_started_ms_;

    // A red sweep down the line, then the score in binary.
    if (elapsed < 600) {
      const float progress = elapsed / 600.0f;
      display.span(0.0f, progress, kWormHead, 0.5f * (1.0f - progress));
    } else {
      engine.renderScore(score_, elapsed - 600);
    }
    return;
  }

  if (state_ == State::kDying) {
    // Everything decays away, leaving only the particles from the impact.
    display.fade(0.15f);
    for (uint8_t i = 0; i < kMaxParticles; i++) {
      const Particle& p = particles_[i];
      if (!p.active) continue;
      display.point(p.pos, p.color, (p.life / kParticleLife));
    }
    return;
  }

  if (state_ == State::kWaveClear) {
    display.fade(0.2f);
    // A green pulse travelling out along the line as a reward beat.
    const float progress =
        static_cast<float>(millis() - state_started_ms_) / kWaveClearMs;
    display.point(progress, kLifeColor, 1.0f - progress);
    display.point(progress - display.pixelWidth(), kLifeColor,
                  (1.0f - progress) * 0.5f);
    for (uint8_t i = 0; i < lives_; i++) {
      display.rawPixel(display.pixelCount() - 1 - i, kLifeColor.scaled(0.35f));
    }
    return;
  }

  // A light fade rather than a clear leaves short trails on everything, which
  // makes fast movement readable on a low-resolution display.
  display.fade(0.55f);
  renderPlayfield(engine);
}

}  // namespace beamboy
