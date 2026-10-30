class BeamMock
  var a_pressed, b_held, total, pixels, rolls
  def init()
    self.a_pressed = false
    self.b_held = false
    self.total = 0
    self.pixels = 50
    self.rolls = 0
  end
  def pressed(button)
    return button == "a" && self.a_pressed
  end
  def held(button)
    return button == "b" && self.b_held
  end
  def random(low, high)
    self.rolls += 1
    return low + (self.rolls * 7) % (high - low + 1)
  end
  def score(points)
    self.total += points
  end
  def reset_score()
    self.total = 0
  end
  def clear()
  end
  def pixel(pos, color, intensity)
    assert(pos >= 0.0 && pos <= 1.0, "drawing outside tube")
  end
  def raw_pixel(index, color, intensity)
    assert(index >= 0 && index < self.pixels - 3, "stack must not touch lives")
  end
  def show_lives(lives)
    assert(lives >= 0 && lives <= 3, "invalid life count")
  end
  def line(first, last, color, intensity)
    assert(first >= 0.0 && last <= 1.0 && first <= last, "invalid line")
    assert(intensity >= 0.0 && intensity <= 1.0, "invalid intensity")
  end
  def pixel_width()
    return 1.0 / (self.pixels - 1)
  end
  def pixel_count()
    return self.pixels
  end
  def show_score(elapsed)
  end
end

var beam = BeamMock()

def prepare()
  beam.a_pressed = false
  beam.b_held = false
  init()
  state = STATE_PLAYING
  spawn_timer = 100.0
end

def shoot()
  beam.a_pressed = true
  update(0.01)
  beam.a_pressed = false
end

def run(seconds)
  var t = 0.0
  while t < seconds
    update(0.01)
    render()
    t += 0.01
  end
end

def test_cartridge()
  init()
  assert(state == STATE_READY && lives == 3 && beam.total == 0)

  # exactly N bullets kill a worm of length N, N-1 do not
  prepare()
  worms = [Worm(0.5, 3)]
  shoot()
  run(0.7)
  shoot()
  run(0.7)
  assert(size(worms) == 1 && worms[0].hp == 1 && beam.total == 0, "2 hits leave 1 segment")
  shoot()
  run(0.7)
  assert(size(worms) == 0 && beam.total == 1, "3rd bullet kills")

  # a miss stacks next to the player, permanently, and shrinks the barrier
  prepare()
  var before = barrier()
  shoot()
  run(1.0)
  assert(stack == 1 && size(bullets) == 0 && barrier() > before, "miss must stack")
  worms = [Worm(barrier() + 0.01, 2)]
  run(0.5)
  assert(lives == 2 && state == STATE_HIT && stack == 1, "worm reaching stack costs a life")

  # stack never grows past the cap
  prepare()
  var n = 0
  while n < 100
    shoot()
    run(0.2)
    n += 1
  end
  assert(stack == stack_max, "stack capped")

  # cannon: unavailable at first, then hurts every worm by up to 3
  prepare()
  worms = [Worm(0.4, 1), Worm(0.5, 5), Worm(0.6, 2)]
  beam.b_held = true
  run(2.0)
  assert(charge == 0.0, "cannon must not charge while recharging")
  beam.b_held = false
  run(0.1)
  assert(size(worms) == 3 && beam.total == 0, "cannon not ready")
  cannon_cooldown = 0.0
  beam.b_held = true
  run(2.0)
  assert(charge_level() == 3)
  var hp_before = worms[1].hp
  beam.b_held = false
  update(0.01)
  assert(size(worms) == 1 && worms[0].hp == hp_before - 3 && beam.total == 2, "cannon hits all worms")
  assert(flash_left > 0.0 && cannon_cooldown > 9.0, "flash and long recharge")
  run(1.0)
  assert(flash_left <= 0.0)

  prepare()
  cannon_cooldown = 0.0
  worms = [Worm(0.5, 4), Worm(0.6, 4)]
  beam.b_held = true
  run(0.3)
  beam.b_held = false
  update(0.01)
  assert(worms[0].hp == 3 && worms[1].hp == 3, "level 1 charge deals 1 to each")

  prepare()
  cannon_cooldown = 0.0
  worms = [Worm(0.5, 2)]
  beam.b_held = true
  run(0.1)
  beam.b_held = false
  run(0.1)
  assert(size(worms) == 1 && worms[0].hp == 2, "tiny charge does not fire")
  # game over and restart
  prepare()
  lives = 1
  worms = [Worm(barrier() + 0.001, 1)]
  run(0.1)
  run(1.0)
  assert(state == STATE_GAME_OVER)
  run(1.0)
  beam.a_pressed = true
  update(0.01)
  beam.a_pressed = false
  assert(state == STATE_READY && lives == 3)

  # long unattended and hyperactive runs stay valid
  init()
  var frame = 0
  while frame < 30000
    beam.a_pressed = frame % 9 == 0
    beam.b_held = (frame / 200) % 3 == 0
    update(0.016)
    render()
    assert(size(bullets) <= kMaxBullets && stack <= stack_max)
    frame += 1
  end
  print("Wormfight cartridge tests passed.")
end
