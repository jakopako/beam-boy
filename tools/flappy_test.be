class BeamMock
  var a_pressed, total, resets, lines, pixels, score_elapsed
  def init()
    self.a_pressed = false
    self.total = 0
    self.resets = 0
    self.lines = []
    self.pixels = []
    self.score_elapsed = -1.0
  end
  def pressed(button)
    return button == "a" && self.a_pressed
  end
  def random(low, high)
    return low
  end
  def score(points)
    self.total += points
  end
  def reset_score()
    self.total = 0
    self.resets += 1
  end
  def clear()
    self.lines = []
    self.pixels = []
  end
  def line(first, last, color, intensity)
    assert(first >= 0.0 && last <= 1.0 && first <= last,
           "wall line outside tube")
    self.lines.push([first, last, color, intensity])
  end
  def pixel(pos, color, intensity)
    assert(pos >= 0.0 && pos <= 1.0, "bird outside tube")
    self.pixels.push([pos, color, intensity])
  end
  def show_score(elapsed)
    self.score_elapsed = elapsed
  end
end

var beam = BeamMock()

def test_cartridge()
  init()
  assert(state == STATE_READY && beam.resets == 1 && beam.total == 0)
  update(0.1)
  assert(state == STATE_READY && bird_pos == 0.5,
         "bird should wait for the first flap")

  beam.a_pressed = true
  update(0.1)
  beam.a_pressed = false
  assert(state == STATE_PLAYING && bird_pos > 0.5,
         "A should start and flap upward")
  render()
  assert(size(beam.lines) == 2 && size(beam.pixels) == 1,
         "render the wall around a visible gap and the bird")
  assert(beam.lines[0][3] >= kWallDimIntensity &&
         beam.lines[0][3] <= kWallBrightIntensity,
         "wall brightness must stay within its defined range")

  wall_elapsed = wall_seconds() * 0.5
  render()
  assert(beam.lines[0][3] > kWallDimIntensity,
         "incoming wall should brighten as it approaches")

  bird_pos = gap_center
  bird_velocity = 0.0
  wall_elapsed = wall_seconds() - 0.01
  update(0.02)
  assert(state == STATE_PLAYING && score == 1 && beam.total == 1,
         "passing through the gap should award one point")
  assert(gap_size() < kStartingGap && wall_seconds() < kStartingWallSeconds,
         "gaps and wall intervals should tighten with score")

  bird_pos = 0.95
  bird_velocity = 0.0
  wall_elapsed = wall_seconds() - 0.01
  update(0.02)
  assert(state == STATE_GAME_OVER && beam.total == 1,
         "hitting a wall outside its gap should end the run")
  game_over_elapsed = 0.6
  render()
  assert(beam.score_elapsed >= 0 &&
         beam.score_elapsed == int(beam.score_elapsed),
         "score reveal should receive integer elapsed milliseconds")

  beam.a_pressed = true
  update(kRestartDelay)
  beam.a_pressed = false
  assert(state == STATE_READY && score == 0 && beam.total == 0 &&
         beam.resets == 2, "A should restart with a fresh score")

  state = STATE_PLAYING
  bird_pos = 0.99
  bird_velocity = kFlapVelocity
  update(0.1)
  assert(state == STATE_GAME_OVER, "leaving the tube should end the run")
end
