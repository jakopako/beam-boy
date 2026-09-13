# Fishing -- a green bar drifts along the tube; keep the blue dot inside it
# for as long as possible. The bar gets faster and more erratic the longer
# you survive. Local-only cartridge for now (see PLAN.md's game-ideas list)
# -- not yet published to the store while the design settles.

var kStartingLives = 3

var kBarColor = 0x00FF28
var kDotColor = 0x1E90FF
var kDangerColor = 0xFF281E
var kLifeColor = 0x00FF5A

# Half the bar's width in normalised units; kept constant -- the challenge
# comes from the bar's movement getting faster and more erratic, not from a
# shrinking target on top of that.
var kBarHalfWidth = 0.11
# The bar's centre never comes closer to an edge than this, so the whole bar
# always fits on the strip.
var kBarMargin = kBarHalfWidth

var kDotMaxSpeed = 0.9
var kDotAccel = 12.0

# How long the dot may sit outside the bar before a life is lost. Long enough
# that a brief overshoot while chasing the bar doesn't cost a life, short
# enough that drifting away and not correcting still stings.
var kGraceMs = 1200.0
var kLifeLostMs = 700
var kGameOverGraceMs = 700

# Time to reach maximum difficulty. Difficulty is a 0..1 value derived from
# how long the whole game has run, and controls how twitchy the bar's
# movement is -- see difficulty() below. It only resets on a full restart
# (init()), not when a life is lost, the same way Reflex's round speed keeps
# climbing across misses and only resets on a fresh game.
var kDifficultyRampMs = 45000.0

# Bar movement tuning, low/high difficulty pairs. "Hold" is the bar resting in
# place; "drift" is the bar travelling to a new random point.
var kHoldMsMinLo = 900.0
var kHoldMsMaxLo = 1700.0
var kHoldMsMinHi = 150.0
var kHoldMsMaxHi = 450.0

var kDriftSpeedMinLo = 0.12
var kDriftSpeedMaxLo = 0.30
var kDriftSpeedMinHi = 0.55
var kDriftSpeedMaxHi = 1.05

# Small random jitter added to the bar every frame, even while holding, so
# "erratic" reads as jitter as well as raw speed. Zero at difficulty 0.
var kJitterPerSecHi = 0.05

# Chance per frame, at maximum difficulty, that a drifting bar suddenly picks
# a new target rather than reaching the one it was headed for -- the
# "erratic" direction changes that make the late game unpredictable.
var kRetargetChancePerFrameHi = 0.01

# Points per second spent inside the bar at the bar's edge, and the extra
# awarded per second at dead centre -- rewards precision, not just presence.
var kScoreBaseRate = 9.0
var kScoreBonusRate = 6.0

var MODE_HOLD = 0
var MODE_DRIFT = 1

var STATE_PLAYING = 0
var STATE_LIFE_LOST = 1
var STATE_GAME_OVER = 2

var state = STATE_PLAYING
var state_started_ms = 0
var survival_started_ms = 0

var lives = kStartingLives

var bar_center = 0.5
var bar_mode = MODE_HOLD
var mode_timer = 0.0
var drift_target = 0.5
var drift_speed = 0.2

var dot_pos = 0.5
var dot_velocity = 0.0

var outside_timer_ms = 0.0
var score_accum = 0.0

# Where the dot was when the life was lost, so the flash renders in place.
var lost_at = 0.0

def lerp(a, b, t)
  return a + (b - a) * t
end

def clamp(value, low, high)
  if value < low
    return low
  elif value > high
    return high
  end
  return value
end

# A triangle wave 0..1..0 over period_ms, used for a subtle breathing effect
# without depending on the math module a cartridge may not have.
def pulse01(period_ms)
  var t = real(beam.time() % period_ms)
  var half = period_ms / 2.0
  if t < half
    return t / half
  end
  return 2.0 - (t / half)
end

def lerp_color(c1, c2, t)
  if t <= 0.0
    return c1
  elif t >= 1.0
    return c2
  end
  var r1 = (c1 >> 16) & 0xFF
  var g1 = (c1 >> 8) & 0xFF
  var b1 = c1 & 0xFF
  var r2 = (c2 >> 16) & 0xFF
  var g2 = (c2 >> 8) & 0xFF
  var b2 = c2 & 0xFF
  var r = int(r1 + (r2 - r1) * t)
  var g = int(g1 + (g2 - g1) * t)
  var b = int(b1 + (b2 - b1) * t)
  return (r << 16) | (g << 8) | b
end

def difficulty()
  var elapsed = beam.time() - survival_started_ms
  var d = elapsed / kDifficultyRampMs
  return clamp(d, 0.0, 1.0)
end

def random01()
  return beam.random(0, 1000) / 1000.0
end

def pick_hold_ms()
  var d = difficulty()
  var lo = lerp(kHoldMsMinLo, kHoldMsMinHi, d)
  var hi = lerp(kHoldMsMaxLo, kHoldMsMaxHi, d)
  return lo + random01() * (hi - lo)
end

def pick_drift_speed()
  var d = difficulty()
  var lo = lerp(kDriftSpeedMinLo, kDriftSpeedMinHi, d)
  var hi = lerp(kDriftSpeedMaxLo, kDriftSpeedMaxHi, d)
  return lo + random01() * (hi - lo)
end

def pick_target()
  var lo = kBarMargin
  var hi = 1.0 - kBarMargin
  if hi <= lo
    return 0.5
  end
  return lo + random01() * (hi - lo)
end

def enter_hold()
  bar_mode = MODE_HOLD
  mode_timer = pick_hold_ms() / 1000.0
end

def enter_drift()
  bar_mode = MODE_DRIFT
  drift_target = pick_target()
  drift_speed = pick_drift_speed()
end

def update_bar(dt)
  var d = difficulty()
  var jitter = (random01() * 2.0 - 1.0) * kJitterPerSecHi * d * dt
  bar_center = clamp(bar_center + jitter, kBarMargin, 1.0 - kBarMargin)

  if bar_mode == MODE_HOLD
    mode_timer -= dt
    if mode_timer <= 0.0
      enter_drift()
    end
    return
  end

  # MODE_DRIFT
  var delta = drift_target - bar_center
  var step = drift_speed * dt
  if step >= delta && step >= -delta
    bar_center = drift_target
    enter_hold()
    return
  end
  bar_center += delta > 0.0 ? step : -step

  if d > 0.0 && random01() < kRetargetChancePerFrameHi * d
    drift_target = pick_target()
  end
end

def update_dot(dt)
  var target = beam.stick("x") * kDotMaxSpeed
  dot_velocity += (target - dot_velocity) * kDotAccel * dt
  dot_pos += dot_velocity * dt

  if dot_pos < 0.0
    dot_pos = 0.0
    dot_velocity = 0.0
  elif dot_pos > 1.0
    dot_pos = 1.0
    dot_velocity = 0.0
  end
end

def bar_error()
  var error = dot_pos - bar_center
  if error < 0.0
    error = -error
  end
  return error
end

def award_score(dt, accuracy)
  score_accum += dt * (kScoreBaseRate + kScoreBonusRate * accuracy)
  var whole = int(score_accum)
  if whole >= 1
    beam.score(whole)
    score_accum -= whole
  end
end

def start_life()
  bar_center = 0.5
  enter_hold()
  dot_pos = bar_center
  dot_velocity = 0.0
  outside_timer_ms = 0.0
end

def init()
  lives = kStartingLives
  score_accum = 0.0
  survival_started_ms = beam.time()
  start_life()
  state = STATE_PLAYING
  state_started_ms = beam.time()
end

def update(dt)
  if state == STATE_GAME_OVER
    if beam.time() - state_started_ms > kGameOverGraceMs && beam.pressed("a")
      init()
    end
    return
  end

  if state == STATE_LIFE_LOST
    if beam.time() - state_started_ms >= kLifeLostMs
      if lives == 0
        state = STATE_GAME_OVER
        state_started_ms = beam.time()
        return
      end
      start_life()
      state = STATE_PLAYING
    end
    return
  end

  # --- Playing --------------------------------------------------------------

  update_bar(dt)
  update_dot(dt)

  var error = bar_error()
  if error <= kBarHalfWidth
    outside_timer_ms = 0.0
    var accuracy = 1.0 - (error / kBarHalfWidth)
    award_score(dt, accuracy)
    return
  end

  outside_timer_ms += dt * 1000.0
  if outside_timer_ms >= kGraceMs
    if lives > 0
      lives -= 1
    end
    lost_at = dot_pos
    state = STATE_LIFE_LOST
    state_started_ms = beam.time()
  end
end

def render_lives()
  var i = 0
  while i < lives
    beam.raw_pixel(beam.pixel_count() - 1 - i, kLifeColor, 0.35)
    i += 1
  end
end

def render()
  if state == STATE_GAME_OVER
    beam.clear()
    var elapsed = beam.time() - state_started_ms
    if elapsed < 500
      var progress = elapsed / 500.0
      beam.line(0.0, progress, kDangerColor, 0.5 * (1.0 - progress))
    else
      beam.show_score(elapsed - 500)
    end
    return
  end

  beam.clear()

  var bar_intensity = 0.3 + 0.15 * pulse01(1400)
  beam.line(bar_center - kBarHalfWidth, bar_center + kBarHalfWidth, kBarColor,
            bar_intensity)

  if state == STATE_LIFE_LOST
    var elapsed = beam.time() - state_started_ms
    var fade = 1.0 - (elapsed / real(kLifeLostMs))
    beam.pixel(lost_at, kDangerColor, fade)
  else
    # The dot bleeds from blue toward red as the grace period runs out, so
    # the player sees the life-loss coming rather than being surprised by it.
    var danger = clamp(outside_timer_ms / kGraceMs, 0.0, 1.0)
    var color = lerp_color(kDotColor, kDangerColor, danger)
    beam.pixel(dot_pos, color, 1.0)
  end

  render_lives()
end
