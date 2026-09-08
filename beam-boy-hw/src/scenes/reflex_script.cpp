#include "scenes/reflex_script.h"

namespace beamboy {

// Beam Boy — Reflex, scripted.
//
// Direct port of scenes/reflex_scene.cpp: a dot sweeps back and forth, press A
// when it's inside the target zone. Zone shrinks and speed grows each round;
// missing, or hitting outside the zone, costs a life. See PLAN.md's cartridge
// API section for what beam.* means and docs/phase-6-cartridges.md for how
// this compares to the native version it was ported from.
//
//   A   stop the dot
//   B   (unused -- pause via the nav button to exit, same as every game)
const char kReflexScript[] = R"be(
var kStartSpeed = 0.55
var kSpeedPerRound = 0.075
var kMaxSpeed = 2.2

var kStartZoneHalf = 0.13
var kZoneShrink = 0.9
var kMinZoneHalf = 0.025
var kZoneMargin = 0.18

var kResultMs = 700
var kStartingLives = 3

var kDotColor = 0xFFFFFF
var kZoneColor = 0x00FF28
var kHitColor = 0x8CFF8C
var kMissColor = 0xFF281E
var kLifeColor = 0x00FF5A

# STATE_* mirror the native ReflexScene::State enum, kept as plain ints since
# Berry has no enum type worth the ceremony for four values used only here.
var STATE_SWEEPING = 0
var STATE_HIT = 1
var STATE_MISS = 2
var STATE_GAME_OVER = 3

var state = STATE_SWEEPING
var state_started_ms = 0

var dot = 0.0
var direction = 1.0
var speed = kStartSpeed

var zone_center = 0.5
var zone_half_width = kStartZoneHalf

var lives = kStartingLives
var round = 0

# Where the dot was when A was pressed, so the result can be shown in place.
var stopped_at = 0.0

def place_zone()
  var floor_half = beam.pixel_width() * 0.75
  if zone_half_width < floor_half
    zone_half_width = floor_half
  end
  if zone_half_width < kMinZoneHalf
    zone_half_width = kMinZoneHalf
  end

  var low = kZoneMargin + zone_half_width
  var high = 1.0 - kZoneMargin - zone_half_width

  if high <= low
    zone_center = 0.5
    return
  end

  zone_center = low + (beam.random(0, 1000) / 1000.0) * (high - low)
end

def next_round()
  round += 1
  speed = kStartSpeed + kSpeedPerRound * round
  if speed > kMaxSpeed
    speed = kMaxSpeed
  end
  zone_half_width *= kZoneShrink
end

def init()
  lives = kStartingLives
  round = 0
  speed = kStartSpeed
  zone_half_width = kStartZoneHalf

  dot = 0.0
  direction = 1.0

  place_zone()

  state = STATE_SWEEPING
  state_started_ms = beam.time()
end

def update(dt)
  if state == STATE_GAME_OVER
    if beam.time() - state_started_ms > 700 && beam.pressed("a")
      init()
    end
    return
  end

  if state == STATE_HIT || state == STATE_MISS
    if beam.time() - state_started_ms >= kResultMs
      if lives == 0
        state = STATE_GAME_OVER
        state_started_ms = beam.time()
        return
      end
      next_round()
      place_zone()
      state = STATE_SWEEPING
      state_started_ms = beam.time()
    end
    return
  end

  # --- Sweeping -------------------------------------------------------------

  dot += direction * speed * dt

  # Bounce off both ends. Clamping as well as flipping keeps the dot on
  # screen even if a long frame overshot the end.
  if dot >= 1.0
    dot = 1.0
    direction = -1.0
  elif dot <= 0.0
    dot = 0.0
    direction = 1.0
  end

  if beam.pressed("a")
    stopped_at = dot
    var error = dot - zone_center
    if error < 0
      error = -error
    end

    if error <= zone_half_width
      # Closer to the centre scores more, so there is something to master
      # beyond simply hitting the zone at all.
      var accuracy = 1.0 - (error / zone_half_width)
      beam.score(1 + int(accuracy * 4.0 + 0.5))
      state = STATE_HIT
    else
      if lives > 0
        lives -= 1
      end
      state = STATE_MISS
    end
    state_started_ms = beam.time()
  end
end

def render()
  if state == STATE_GAME_OVER
    beam.clear()
    var elapsed = beam.time() - state_started_ms
    if elapsed < 500
      var progress = elapsed / 500.0
      beam.line(0.0, progress, kMissColor, 0.5 * (1.0 - progress))
    else
      beam.show_score(elapsed - 500)
    end
    return
  end

  beam.clear()

  # The target zone is always drawn, so the player can aim before the dot
  # arrives rather than reacting to something they cannot see coming.
  beam.line(zone_center - zone_half_width, zone_center + zone_half_width,
            kZoneColor, 0.35)
  beam.pixel(zone_center, kZoneColor, 0.7)

  if state == STATE_SWEEPING
    beam.pixel(dot, kDotColor, 1.0)
    beam.pixel(dot - direction * beam.pixel_width(), kDotColor, 0.35)
  else
    var elapsed = beam.time() - state_started_ms
    var fade = 1.0 - (elapsed / real(kResultMs))
    var color = state == STATE_HIT ? kHitColor : kMissColor

    if state == STATE_HIT
      # A hit flashes the zone, tying the reward to the place it happened.
      beam.line(zone_center - zone_half_width, zone_center + zone_half_width,
                color, fade)
    end
    beam.pixel(stopped_at, color, fade)
  end

  var i = 0
  while i < lives
    beam.raw_pixel(beam.pixel_count() - 1 - i, kLifeColor, 0.35)
    i += 1
  end
end
)be";

}  // namespace beamboy
