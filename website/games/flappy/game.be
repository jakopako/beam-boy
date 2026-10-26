# Flappy -- tap A to flap; stay inside each gap as the incoming wall brightens.
# Height is represented along the tube, and the next wall is shown as two
# segments around its safe gap.

var kPipeColor = 0xFFB52E
var kBirdColor = 0xFFFFFF
var kHitColor = 0xFF281E

var kGravity = 1.55
var kFlapVelocity = 0.68
var kStartingGap = 0.38
var kSmallestGap = 0.18
var kGapShrinkPerPipe = 0.006
var kStartingWallSeconds = 1.8
var kFastestWallSeconds = 0.9
var kWallSpeedPerPipe = 0.025
var kWallDimIntensity = 0.06
var kWallBrightIntensity = 0.9
var kStartingLives = 3
var kHitSeconds = 0.6
var kRestartDelay = 0.7

var STATE_READY = 0
var STATE_PLAYING = 1
var STATE_HIT = 2
var STATE_GAME_OVER = 3

var state = STATE_READY
var bird_pos = 0.5
var bird_velocity = 0.0
var gap_center = 0.5
var wall_elapsed = 0.0
var score = 0
var game_over_elapsed = 0.0
var lives = kStartingLives
var hit_pos = 0.5

def clamp(value, low, high)
  if value < low
    return low
  elif value > high
    return high
  end
  return value
end

def gap_size()
  var gap = kStartingGap - score * kGapShrinkPerPipe
  return gap < kSmallestGap ? kSmallestGap : gap
end

def wall_seconds()
  var seconds = kStartingWallSeconds - score * kWallSpeedPerPipe
  return seconds < kFastestWallSeconds ? kFastestWallSeconds : seconds
end

def choose_gap()
  var margin = gap_size() / 2.0 + 0.06
  gap_center = margin + beam.random(0, 1000) / 1000.0 * (1.0 - margin * 2.0)
end

def start_life()
  state = STATE_PLAYING
  bird_pos = 0.5
  bird_velocity = kFlapVelocity
  wall_elapsed = 0.0
  game_over_elapsed = 0.0
  choose_gap()
end

def start_game()
  score = 0
  start_life()
end

def init()
  beam.reset_score()
  state = STATE_READY
  bird_pos = 0.5
  bird_velocity = 0.0
  score = 0
  wall_elapsed = 0.0
  game_over_elapsed = 0.0
  lives = kStartingLives
  choose_gap()
end

def hit()
  lives -= 1
  state = STATE_HIT
  game_over_elapsed = 0.0
  hit_pos = clamp(bird_pos, 0.0, 1.0)
  bird_velocity = 0.0
end

def update(dt)
  if state == STATE_GAME_OVER
    game_over_elapsed += dt
    if game_over_elapsed >= kRestartDelay && beam.pressed("a")
      init()
    end
    return
  end

  if state == STATE_HIT
    game_over_elapsed += dt
    if game_over_elapsed >= kHitSeconds
      if lives == 0
        state = STATE_GAME_OVER
        game_over_elapsed = 0.0
      else
        start_life()
      end
    end
    return
  end

  if state == STATE_READY
    if beam.pressed("a")
      start_game()
    else
      return
    end
  elif beam.pressed("a")
    bird_velocity = kFlapVelocity
  end

  bird_velocity -= kGravity * dt
  bird_pos += bird_velocity * dt
  if bird_pos <= 0.0 || bird_pos >= 1.0
    hit()
    return
  end

  wall_elapsed += dt
  if wall_elapsed >= wall_seconds()
    var half_gap = gap_size() / 2.0
    if bird_pos < gap_center - half_gap || bird_pos > gap_center + half_gap
      hit()
      return
    end
    wall_elapsed -= wall_seconds()
    score += 1
    beam.score(1)
    choose_gap()
  end
end

def render()
  beam.clear()
  if state == STATE_GAME_OVER
    if game_over_elapsed < 0.5
      var progress = game_over_elapsed / 0.5
      beam.line(0.0, progress, kHitColor, 0.5 * (1.0 - progress))
    else
      beam.show_score(int((game_over_elapsed - 0.5) * 1000.0))
    end
    return
  end

  if state == STATE_HIT
    var fade = 1.0 - clamp(game_over_elapsed / kHitSeconds, 0.0, 1.0)
    beam.pixel(hit_pos, kHitColor, fade)
    beam.show_lives(lives)
    return
  end

  var progress = clamp(wall_elapsed / wall_seconds(), 0.0, 1.0)
  var intensity = kWallDimIntensity +
      (kWallBrightIntensity - kWallDimIntensity) * progress
  var half_gap = gap_size() / 2.0
  var gap_low = gap_center - half_gap
  var gap_high = gap_center + half_gap
  if gap_low > 0.0
    beam.line(0.0, gap_low, kPipeColor, intensity)
  end
  if gap_high < 1.0
    beam.line(gap_high, 1.0, kPipeColor, intensity)
  end
  beam.pixel(bird_pos, kBirdColor, 1.0)
  beam.show_lives(lives)
end
