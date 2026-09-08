#pragma once

// Wormfight — the first real Beam Boy game.
//
// Grown from the original prototype in docs/wormfight-prototype.cpp.txt, which
// established the core idea: segmented worms crawl toward you along the line,
// and you shoot them before they arrive.
//
// What the engine adds over the prototype:
//   * normalised coordinates, so it runs on any strip length
//   * anti-aliased rendering, so movement is smooth rather than steppy
//   * waves, lives, particles, screen shake and a death sequence
//
//   Joystick  aim / move the turret
//   A         fire
//   B         hold to charge, release to fire a piercing shot

#include "core/engine.h"

namespace beamboy {

class WormfightScene : public Scene {
 public:
  void enter(Engine& engine) override;
  void update(Engine& engine, float dt) override;
  void render(Engine& engine) override;

  uint32_t score() const override { return score_; }

 private:
  // Tuned against a 10 px strip but expressed in normalised units, so the feel
  // carries over to the 50 px tube unchanged.
  static constexpr uint8_t kMaxWorms = 4;
  static constexpr uint8_t kMaxShots = 3;
  static constexpr uint8_t kMaxParticles = 12;
  static constexpr uint8_t kStartingLives = 3;
  // A worm may never be longer than a quarter of the line, or there is nowhere
  // left to play; this is the absolute ceiling on top of that rule.
  static constexpr uint8_t kMaxSegments = 5;

  enum class State : uint8_t {
    kPlaying,
    kWaveClear,
    kDying,
    kGameOver,
  };

  struct Worm {
    float head = 0.0f;     // normalised position of the leading segment
    float speed = 0.0f;    // units per second, always negative (toward player)
    uint8_t segments = 0;  // remaining hits; also the visual length
    bool active = false;
    float hit_flash = 0.0f;  // seconds remaining of the white hit flash
  };

  struct Shot {
    float pos = 0.0f;
    float speed = 0.0f;
    // Segments this shot can still destroy. A normal shot has 1 and dies on
    // its first hit; a charged shot keeps going until its power is spent.
    uint8_t power = 1;
    bool active = false;
  };

  struct Particle {
    float pos = 0.0f;
    float velocity = 0.0f;
    float life = 0.0f;  // seconds remaining
    Color color;
    bool active = false;
  };

  void startWave(uint8_t wave);
  bool spawnWorm(float head, float speed, uint8_t segments);
  bool fire(uint8_t power, float speed);
  void updateCharge(Engine& engine, float dt);
  void emitBurst(float pos, const Color& color, uint8_t count, float energy);
  void damagePlayer();
  void updateParticles(float dt);
  void renderPlayfield(Engine& engine);

  float segmentWidth(const Display& display) const {
    return display.pixelWidth();
  }

  State state_ = State::kPlaying;
  uint32_t state_started_ms_ = 0;

  float player_ = 0.0f;
  float player_velocity_ = 0.0f;
  uint8_t lives_ = kStartingLives;
  uint32_t score_ = 0;
  uint8_t wave_ = 0;

  Worm worms_[kMaxWorms];
  Shot shots_[kMaxShots];
  Particle particles_[kMaxParticles];

  float fire_cooldown_ = 0.0f;
  // Seconds B has been held. Driven from the button *level*, not edges, so a
  // missed release edge cannot strand the player mid-charge.
  float charge_ = 0.0f;
  bool charging_ = false;
  float muzzle_flash_ = 0.0f;
  float shake_time_ = 0.0f;
  float shake_energy_ = 0.0f;
  float spawn_timer_ = 0.0f;
  uint8_t worms_remaining_in_wave_ = 0;
};

}  // namespace beamboy
