class BeamMock
  var a_pressed, b_held, total, resets, draws, pixels, random_high, forced_type, lines
  def init()
    self.a_pressed = false
    self.b_held = false
    self.total = 0
    self.resets = 0
    self.draws = []
    self.lines = []
    self.pixels = 50
    self.random_high = false
    self.forced_type = -1
  end
  def pressed(button)
    return button == "a" && self.a_pressed
  end
  def held(button)
    return button == "b" && self.b_held
  end
  def random(low, high)
    if low == 0 && high == 1 && self.forced_type >= 0
      return self.forced_type
    end
    return self.random_high ? high : low
  end
  def score(points)
    self.total += points
  end
  def reset_score()
    self.total = 0
    self.resets += 1
  end
  def clear()
    self.draws = []
    self.lines = []
  end
  def pixel(pos, color, intensity)
    assert(pos >= 0.0 && pos <= 1.0, "drawing outside tube")
    self.draws.push([pos, color, intensity])
  end
  def raw_pixel(index, color, intensity)
    assert(index >= 0 && index < self.pixels, "invalid pixel index")
    assert(index >= self.pixels - 3, "only life indicators should use raw pixels")
  end
  def show_lives(lives)
    assert(lives >= 0 && lives <= 3, "invalid life count")
  end
  def line(first, last, color, intensity)
    assert(first >= 0.0 && last <= 1.0 && first <= last, "invalid line")
    self.lines.push([first, last, color])
  end
  def pixel_width()
    return 1.0 / (self.pixels - 1)
  end
  def pixel_count()
    return self.pixels
  end
  def show_score(elapsed)
    assert(elapsed >= 0, "invalid score reveal time")
  end
end

var beam = BeamMock()

def prepare(ground)
  beam.a_pressed = false
  beam.b_held = false
  init()
  state = STATE_PLAYING
  var obstacle = Obstacle(ground, 0.001)
  obstacle.pos = kPlayerPos + 0.001
  obstacles = [obstacle]
  spawn_distance = 100.0
end

def test_cartridge()
  init()
  assert(state == STATE_READY && beam.total == 0 && lives == 3)
  update(0.5)
  assert(state == STATE_READY && size(obstacles) == 0)
  update(0.5)
  assert(state == STATE_PLAYING)

  prepare(true)
  beam.a_pressed = true
  update(0.02)
  assert(beam.total == 1 && lives == 3 && size(obstacles) == 0, "jump must clear ground")
  update(0.02)
  assert(beam.total == 1, "one point per obstacle")

  prepare(true)
  beam.b_held = true
  update(0.02)
  assert(lives == 2 && beam.total == 0 && state == STATE_HIT, "duck must fail ground")

  prepare(false)
  beam.b_held = true
  update(0.02)
  assert(beam.total == 1 && lives == 3, "duck must clear overhead")

  prepare(false)
  beam.a_pressed = true
  update(0.02)
  assert(lives == 2 && beam.total == 0, "jump must fail overhead")

  prepare(false)
  update(0.02)
  assert(lives == 2, "standing must fail overhead")

  prepare(false)
  beam.a_pressed = true
  beam.b_held = true
  update(0.02)
  assert(lives == 2, "both buttons must not evade every obstacle")

  prepare(true)
  update(0.5)
  assert(lives == 2, "crossing collision must survive a large timestep")
  update(kHitSeconds)
  assert(state == STATE_READY && lives == 2)

  prepare(true)
  jump_left = 0.01
  update(0.02)
  assert(lives == 2, "expired jump must not protect")

  prepare(true)
  obstacles[0].pos = kSpawnPos
  beam.a_pressed = true
  update_actions(0.01)
  beam.a_pressed = false
  update_actions(kJumpSeconds)
  assert(jump_left == 0.0 && recovery_left > 0.0)
  beam.a_pressed = true
  update_actions(0.01)
  assert(jump_left == 0.0, "landing cooldown must prevent immediate rejump")
  beam.a_pressed = false
  update_actions(kRecoverySeconds)
  beam.a_pressed = true
  update_actions(0.01)
  assert(jump_left > 0.0, "jump must recover")
  beam.a_pressed = false
  jump_left = 0.0
  beam.b_held = true
  update_actions(0.01)
  assert(ducking)
  beam.b_held = false
  update_actions(0.01)
  assert(!ducking, "duck must follow held level")

  prepare(true)
  lives = 1
  update(0.02)
  assert(lives == 0 && state == STATE_HIT)
  update(kHitSeconds)
  assert(state == STATE_GAME_OVER)
  render()
  beam.a_pressed = true
  update(0.1)
  assert(state == STATE_GAME_OVER, "restart grace")
  update(0.6)
  assert(state == STATE_READY && lives == 3 && cleared == 0 && beam.total == 0)

  cleared = 100
  assert(speed() == kMaxSpeed)
  assert(kMinGapSeconds > kJumpSeconds + kRecoverySeconds,
         "minimum gap must allow jump recovery")

  prepare(true)
  obstacles = []
  assert(kPlayerPos > 0.0 && kPlayerPos < 0.15, "player near controller end")
  jump_left = 0.2
  render()
  assert(beam.draws[0][1] == kGroundColor && size(beam.draws) == 1 &&
         beam.draws[0][0] == kPlayerPos && beam.draws[0][2] == 1.0,
         "jump must be one full-intensity dot without a trail")
  jump_left = 0.0
  ducking = true
  render()
  assert(beam.draws[0][1] == kOverheadColor && size(beam.draws) == 1 &&
         beam.draws[0][0] == kPlayerPos && beam.draws[0][2] == 0.10,
         "duck must be one dim dot at the same position")
  ducking = false
  render()
  assert(beam.draws[0][1] == kPlayerColor && size(beam.draws) == 1 &&
         beam.draws[0][0] == kPlayerPos && beam.draws[0][2] == 0.25,
         "standing player must be one white dot dimmer than jumping")

  obstacles = [Obstacle(false, 0.3)]
  render()
  assert(size(beam.lines) == 0 && size(beam.draws) == 1,
         "new obstacle must start entirely outside tube")
  obstacles[0].pos = 0.98
  render()
  assert(size(beam.lines) == 1 && beam.lines[0][0] == 0.98 &&
         beam.lines[0][1] == 1.0, "entry must reveal only the visible portion")
  obstacles[0].pos = 0.9
  render()
  assert(beam.lines[0][0] == 0.9 && beam.lines[0][1] == 1.0,
         "visible length must grow as obstacle enters")
  obstacles[0].pos = 0.6
  render()
  assert(beam.lines[0][1] < 1.0, "fully entered obstacle must show its trailing edge")

  # Long obstacles require protection for their entire overlap, not just entry.
  prepare(false)
  obstacles[0].length = 0.2
  obstacles[0].pos = kPlayerPos + 0.001
  beam.b_held = true
  update(0.02)
  assert(lives == 3 && beam.total == 0 && size(obstacles) == 1)
  beam.b_held = false
  update(0.02)
  assert(lives == 2 && beam.total == 0, "releasing duck inside obstacle must hit")

  prepare(true)
  obstacles[0].length = speed() * kGroundMaxSeconds
  obstacles[0].pos = kPlayerPos + 0.001
  beam.a_pressed = true
  update(0.02)
  beam.a_pressed = false
  var step = 0
  while step < 20
    update(1.0 / 60.0)
    step += 1
  end
  assert(lives == 3 && beam.total == 1, "longest ground obstacle fits one jump")

  prepare(false)
  obstacles[0].length = 0.2
  obstacles[0].pos = kPlayerPos + 0.001
  beam.b_held = true
  step = 0
  while step < 50
    update(1.0 / 60.0)
    step += 1
  end
  assert(lives == 3 && beam.total == 1, "long overhead scores once at trailing edge")
  obstacles = []
  render()
  assert(beam.draws[0][1] == kOverheadColor, "clear must preserve duck colour")
  beam.b_held = false
  update_actions(0.01)
  render()
  assert(beam.draws[0][1] == kPlayerColor, "standing after clear must be white, not green")

  var difficulty = 0
  while difficulty <= 50
    cleared = difficulty
    beam.random_high = false
    plan_obstacle()
    var short_length = next_obstacle.length
    assert(next_obstacle.ground)
    beam.forced_type = 0
    beam.random_high = true
    plan_obstacle()
    assert(next_obstacle.ground && next_obstacle.length > short_length,
           "orange obstacles must vary in length")
    assert(next_obstacle.length / speed() <= kGroundMaxSeconds + 0.001)
    assert(next_obstacle.length / speed() < kJumpSeconds)
    beam.forced_type = 1
    beam.random_high = false
    plan_obstacle()
    var short_overhead = next_obstacle.length
    beam.random_high = true
    plan_obstacle()
    assert(!next_obstacle.ground && next_obstacle.length > short_overhead,
           "cyan obstacles must vary in length")
    assert(next_obstacle.length > short_length)
    assert(short_length / speed() < kJumpSeconds)
    beam.forced_type = -1
    difficulty += 1
  end
  beam.random_high = false

  # Queue must fill before the leading obstacle reaches the player.
  init()
  state = STATE_PLAYING
  step = 0
  while step < 110
    update(1.0 / 60.0)
    step += 1
  end
  assert(size(obstacles) >= 2, "multiple approaching obstacles must be visible")
  render()

  # Play successfully through the full speed ramp with mixed obstacle lengths.
  init()
  var frame = 0
  while frame < 20000
    beam.random_high = frame % 2 == 0
    beam.a_pressed = false
    beam.b_held = false
    if size(obstacles) > 0
      var first = obstacles[0]
      beam.b_held = !first.ground
      beam.a_pressed = first.ground && jump_left == 0.0 &&
          first.pos > kPlayerPos && first.pos - kPlayerPos <= speed() * 0.1
    end
    update(1.0 / 60.0)
    render()
    assert(lives == 3, "valid actions must clear mixed obstacles at every speed")
    var i = 1
    while i < size(obstacles)
      var gap = obstacles[i].pos - (obstacles[i - 1].pos + obstacles[i - 1].length)
      assert(gap / speed() >= kMinGapSeconds - 0.001,
             "accelerating queue must retain its recovery gap")
      i += 1
    end
    frame += 1
  end
  assert(speed() == kMaxSpeed && beam.total > 50, "test must reach maximum difficulty")
  beam.random_high = false

  # Run real update/render callbacks for 10,000 frames, including every state.
  init()
  frame = 0
  while frame < 10000
    beam.a_pressed = frame % 45 == 0
    beam.b_held = frame % 90 < 45
    update(1.0 / 60.0)
    render()
    assert(lives >= 0 && lives <= 3)
    assert(speed() >= kStartSpeed && speed() <= kMaxSpeed)
    assert(size(obstacles) <= kMaxObstacles)
    var i = 1
    while i < size(obstacles)
      var gap = obstacles[i].pos - (obstacles[i - 1].pos + obstacles[i - 1].length)
      assert(gap / speed() >= kMinGapSeconds - 0.001, "queue gap must allow recovery")
      i += 1
    end
    frame += 1
  end
  print("Obstacles cartridge tests passed (30,000 frames, including the full speed ramp).")
end
