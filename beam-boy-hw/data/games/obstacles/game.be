# Obstacles -- A jumps orange ground obstacles; hold B to duck cyan overheads.
# Position zero is the controller end. Clear the entire length of each obstacle.

var kGroundColor = 0xFF6020
var kOverheadColor = 0x00DFFF
var kPlayerColor = 0xFFFFFF
var kHitColor = 0xFF281E
var kStandingIntensity = 0.25
var kJumpIntensity = 1.0
var kDuckIntensity = 0.10

var kPlayerPos = 0.08
var kSpawnPos = 1.02
var kJumpSeconds = 0.65
var kRecoverySeconds = 0.18
var kStartSpeed = 0.28
var kMaxSpeed = 0.80
var kSpeedPerClear = 0.013
var kStartingLives = 3
var kReadySeconds = 1.0
var kHitSeconds = 0.7
var kMaxObstacles = 4
var kGroundMinSeconds = 0.16
var kGroundMaxSeconds = 0.28
var kOverheadMinSeconds = 0.35
var kOverheadMaxSeconds = 0.95
var kMinGapSeconds = 0.95

class Obstacle
  var pos, length, ground
  def init(ground, length)
    self.ground = ground
    self.length = length
    self.pos = kSpawnPos
  end
end

var STATE_READY = 0
var STATE_PLAYING = 1
var STATE_HIT = 2
var STATE_GAME_OVER = 3

var state = STATE_READY
var state_timer = 0.0
var lives = kStartingLives
var cleared = 0
var jump_left = 0.0
var recovery_left = 0.0
var ducking = false
var obstacles = []
var next_obstacle = nil
var spawn_distance = 0.0
var game_over_elapsed = 0.0

def speed()
  var value = kStartSpeed + cleared * kSpeedPerClear
  return value > kMaxSpeed ? kMaxSpeed : value
end

def reset_actions()
  jump_left = 0.0
  recovery_left = 0.0
  ducking = false
end

def plan_obstacle()
  var ground = beam.random(0, 1) == 0
  var duration = ground ?
      beam.random(int(kGroundMinSeconds * 1000), int(kGroundMaxSeconds * 1000)) :
      beam.random(int(kOverheadMinSeconds * 1000), int(kOverheadMaxSeconds * 1000))
  var length = speed() * duration / 1000.0
  if !ground && length > 0.45
    length = 0.45
  end
  next_obstacle = Obstacle(ground, length)
end

def start_life()
  reset_actions()
  obstacles = []
  plan_obstacle()
  spawn_distance = 0.0
  state = STATE_READY
  state_timer = kReadySeconds
end

def init()
  beam.reset_score()
  lives = kStartingLives
  cleared = 0
  game_over_elapsed = 0.0
  start_life()
end

def update_actions(dt)
  if jump_left > 0.0
    jump_left -= dt
    if jump_left <= 0.0
      jump_left = 0.0
      recovery_left = kRecoverySeconds
    end
  elif recovery_left > 0.0
    recovery_left -= dt
    if recovery_left < 0.0
      recovery_left = 0.0
    end
  end

  if beam.pressed("a") && jump_left == 0.0 && recovery_left == 0.0
    jump_left = kJumpSeconds
  end
  # Jump takes priority, so pressing both buttons cannot clear both types.
  ducking = beam.held("b") && jump_left == 0.0
end

def spawn_obstacle()
  var previous_length = next_obstacle.length
  obstacles.push(next_obstacle)
  plan_obstacle()
  # Reserve a gap at the fastest speed possible before the queue reaches the
  # player, including score-driven acceleration from every queued obstacle.
  var gap_speed = kStartSpeed + (cleared + kMaxObstacles) * kSpeedPerClear
  if gap_speed > kMaxSpeed
    gap_speed = kMaxSpeed
  end
  spawn_distance = previous_length +
      gap_speed * (kMinGapSeconds + beam.random(0, 250) / 1000.0)
end

def hit()
  lives -= 1
  obstacles = []
  reset_actions()
  state = STATE_HIT
  state_timer = kHitSeconds
end

def update(dt)
  if state == STATE_GAME_OVER
    game_over_elapsed += dt
    if game_over_elapsed >= 0.7 && beam.pressed("a")
      init()
    end
    return
  end

  if state == STATE_READY || state == STATE_HIT
    state_timer -= dt
    if state_timer <= 0.0
      if state == STATE_HIT
        if lives == 0
          state = STATE_GAME_OVER
          game_over_elapsed = 0.0
        else
          start_life()
        end
      else
        state = STATE_PLAYING
      end
    end
    return
  end

  update_actions(dt)

  var distance = speed() * dt
  spawn_distance -= distance
  var i = 0
  while i < size(obstacles)
    var obstacle = obstacles[i]
    var previous_tail = obstacle.pos + obstacle.length
    obstacle.pos -= distance
    # Swept overlap checks the full obstacle, including frames that move both
    # ends past the player. Protection must last until the trailing edge clears.
    if obstacle.pos <= kPlayerPos && previous_tail >= kPlayerPos
      var safe = obstacle.ground ? jump_left > 0.0 : ducking
      if !safe
        hit()
        return
      end
    end
    if obstacle.pos + obstacle.length < kPlayerPos
      obstacles.remove(i)
      cleared += 1
      beam.score(1)
    else
      i += 1
    end
  end
  if spawn_distance <= 0.0 && size(obstacles) < kMaxObstacles
    spawn_obstacle()
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

  var index = 0
  while index < size(obstacles)
    var obstacle = obstacles[index]
    var color = obstacle.ground ? kGroundColor : kOverheadColor
    if obstacle.pos <= 1.0
      var first = obstacle.pos < 0.0 ? 0.0 : obstacle.pos
      var last = obstacle.pos + obstacle.length
      if last > 1.0
        last = 1.0
      end
      beam.line(first, last, color, 0.75)
      if obstacle.pos >= 0.0
        beam.pixel(obstacle.pos, color, 1.0)
      end
    end
    index += 1
  end

  if state == STATE_HIT
    beam.pixel(kPlayerPos, kHitColor, state_timer / kHitSeconds)
  elif jump_left > 0.0
    beam.pixel(kPlayerPos, kGroundColor, kJumpIntensity)
  elif ducking
    beam.pixel(kPlayerPos, kOverheadColor, kDuckIntensity)
  else
    beam.pixel(kPlayerPos, kPlayerColor, kStandingIntensity)
  end

  beam.show_lives(lives)
end
