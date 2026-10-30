# Wormfight -- A shoots one bullet; hold B to charge the cannon, release to fire.
# The cannon hurts every worm on screen and needs a long recharge between uses.
# Position zero is the controller end, where the player sits; lives are drawn
# at the far end. A worm dies when hit by exactly as many bullets as it has
# segments. Bullets that miss come back and pile up next to the player for the
# rest of the game, shrinking the distance a worm has to cross.

var kStartingLives = 3
var kLifePixels = 3
var kMinFreePixels = 10
var kBulletSpeed = 1.3
var kFireCooldown = 0.11
var kMaxBullets = 12
var kStartSpeed = 0.08
var kSpeedPerSecond = 0.0012
var kMaxSpeed = 0.3
var kGapPixels = 3
var kQueuePixels = 6
var kStartInterval = 2.6
var kIntervalPerSecond = 0.008
var kMinInterval = 1.2
var kMaxWormLength = 5
var kSecondsPerLength = 40.0
var kMaxCannonDamage = 3
var kCannonRecharge = 10.0
var kChargeMin = 0.2
var kChargeFull = 1.2
var kFlashSeconds = 0.6
var kHitFlashSeconds = 0.12
var kReadySeconds = 1.0
var kHitSeconds = 0.7

var kHeadColor = 0xFF2020
var kBodyColor = 0xB01030
var kBulletColor = 0x80C0FF
var kStackColor = 0x3070FF
var kPlayerColor = 0x00DFFF
var kChargeColor = 0xFFA000
var kHitColor = 0xFF281E

class Worm
  var head, hp, flash
  def init(head, hp)
    self.head = head
    self.hp = hp
    self.flash = 0.0
  end
end

var STATE_READY = 0
var STATE_PLAYING = 1
var STATE_HIT = 2
var STATE_GAME_OVER = 3

var state = STATE_READY
var state_timer = 0.0
var lives = kStartingLives
var elapsed = 0.0
var worms = []
var bullets = []
var stack = 0
var stack_max = 0
var pixels = 50
var pw = 0.02
var fire_cooldown = 0.0
var charge = 0.0
var flash_left = 0.0
var flash_level = 0
var cannon_cooldown = kCannonRecharge
var spawn_timer = 0.0
var game_over_elapsed = 0.0

def speed()
  var s = kStartSpeed + elapsed * kSpeedPerSecond
  return s > kMaxSpeed ? kMaxSpeed : s
end

def interval()
  var i = kStartInterval - elapsed * kIntervalPerSecond
  return i < kMinInterval ? kMinInterval : i
end

# Worms enter just before the life indicators at the far end.
def spawn_pos()
  return (pixels - 1 - kLifePixels) / (pixels - 1.0)
end

# A worm whose head gets this close has reached the player or the stack.
def barrier()
  return (stack + 0.5) * pw
end

def charge_level()
  if charge < kChargeMin
    return 0
  end
  var level = int(charge / kChargeFull * kMaxCannonDamage) + 1
  return level > kMaxCannonDamage ? kMaxCannonDamage : level
end

def start_life()
  worms = []
  bullets = []
  charge = 0.0
  flash_left = 0.0
  fire_cooldown = 0.0
  spawn_timer = 0.5
  state = STATE_READY
  state_timer = kReadySeconds
end

def init()
  beam.reset_score()
  pixels = beam.pixel_count()
  pw = beam.pixel_width()
  stack_max = pixels - kLifePixels - kMinFreePixels
  if stack_max < 0
    stack_max = 0
  end
  lives = kStartingLives
  elapsed = 0.0
  stack = 0
  cannon_cooldown = kCannonRecharge
  game_over_elapsed = 0.0
  start_life()
end

def spawn_worm()
  var max_len = 2 + int(elapsed / kSecondsPerLength)
  if max_len > kMaxWormLength
    max_len = kMaxWormLength
  end
  var hp = beam.random(1, max_len)
  var head = spawn_pos()
  var n = size(worms)
  if n > 0
    var last = worms[n - 1]
    var after = last.head + last.hp * pw + kGapPixels * pw
    if after > head
      head = after
    end
  end
  if head > spawn_pos() + kQueuePixels * pw
    return false
  end
  worms.push(Worm(head, hp))
  return true
end

def damage_worm(worm, amount)
  var dealt = amount < worm.hp ? amount : worm.hp
  worm.hp -= dealt
  worm.flash = kHitFlashSeconds
  if worm.hp <= 0
    beam.score(1)
  end
  return dealt
end

def remove_dead()
  var i = 0
  while i < size(worms)
    if worms[i].hp <= 0
      worms.remove(i)
    else
      i += 1
    end
  end
end

# Every worm currently on the display takes the full damage.
def fire_cannon(damage)
  var i = 0
  while i < size(worms)
    if worms[i].head <= spawn_pos()
      damage_worm(worms[i], damage)
    end
    i += 1
  end
  remove_dead()
  flash_left = kFlashSeconds
  flash_level = damage
  cannon_cooldown = kCannonRecharge
end

def update_weapons(dt)
  if fire_cooldown > 0.0
    fire_cooldown -= dt
  end
  if cannon_cooldown > 0.0
    cannon_cooldown -= dt
  end
  if flash_left > 0.0
    flash_left -= dt
    charge = 0.0
  elif cannon_cooldown > 0.0
    charge = 0.0
  elif beam.held("b")
    charge += dt
    if charge > kChargeFull
      charge = kChargeFull
    end
  elif charge > 0.0
    var level = charge_level()
    charge = 0.0
    if level > 0
      fire_cannon(level)
    end
  end

  if beam.pressed("a") && fire_cooldown <= 0.0 && size(bullets) < kMaxBullets
    bullets.push(barrier())
    fire_cooldown = kFireCooldown
  end
end

def update_bullets(dt)
  var end_pos = spawn_pos()
  var i = 0
  while i < size(bullets)
    var pos = bullets[i] + kBulletSpeed * dt
    var consumed = false
    var w = 0
    while w < size(worms)
      var worm = worms[w]
      if worm.head <= pos && worm.head <= end_pos
        damage_worm(worm, 1)
        consumed = true
        break
      end
      w += 1
    end
    if !consumed && pos >= end_pos
      if stack < stack_max
        stack += 1
      end
      consumed = true
    end
    if consumed
      bullets.remove(i)
    else
      bullets[i] = pos
      i += 1
    end
  end
  remove_dead()
end

def update_worms(dt)
  var distance = speed() * dt
  var i = 0
  while i < size(worms)
    var worm = worms[i]
    worm.head -= distance
    if worm.flash > 0.0
      worm.flash -= dt
    end
    if worm.head <= barrier()
      lives -= 1
      state = STATE_HIT
      state_timer = kHitSeconds
      worms = []
      bullets = []
      charge = 0.0
      flash_left = 0.0
      return
    end
    i += 1
  end
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

  elapsed += dt
  update_weapons(dt)
  update_bullets(dt)
  update_worms(dt)
  if state != STATE_PLAYING
    return
  end

  spawn_timer -= dt
  if spawn_timer <= 0.0 && spawn_worm()
    spawn_timer = interval() + beam.random(0, 500) / 1000.0
  end
end

def render_cannon_flash()
  var progress = 1.0 - flash_left / kFlashSeconds
  var first = barrier()
  var last = spawn_pos()
  var envelope = 1.0 - progress
  var strobe = int(progress * 36.0) % 2
  var color = strobe == 0 ? 0xFFFFFF : (flash_level >= 3 ? 0xFF40FF : 0x00DFFF)
  var gain = 0.45 + 0.18 * flash_level
  var intensity = gain * envelope * (0.4 + beam.random(0, 60) / 100.0)
  beam.line(first, last, color, intensity > 1.0 ? 1.0 : intensity)
  # A bright front races to the far end during the first third of the flash.
  var front = first + progress * 3.0 * (last - first)
  if front < last
    beam.line(first, front, 0xFFFFFF, 0.9 * envelope)
  end
end

def render_worm(worm)
  var last = spawn_pos()
  var i = 0
  while i < worm.hp
    var pos = worm.head + i * pw
    if pos > last + 0.0001
      break
    end
    var color = i == 0 ? kHeadColor : kBodyColor
    var intensity = i == 0 ? 1.0 : (i % 2 == 0 ? 0.65 : 0.4)
    if worm.flash > 0.0
      color = 0xFFFFFF
      intensity = 1.0
    end
    beam.pixel(pos, color, intensity)
    i += 1
  end
end

def render_player()
  if state == STATE_HIT
    beam.pixel(0.0, kHitColor, state_timer / kHitSeconds)
    return
  end
  var level = charge_level()
  if charge > 0.0
    var rate = 3.0 + charge * 8.0
    var phase = elapsed * rate - int(elapsed * rate)
    var tri = phase < 0.5 ? phase * 2.0 : 2.0 - phase * 2.0
    if level >= kMaxCannonDamage
      beam.pixel(0.0, 0xFFFFFF, 1.0)
    else
      beam.pixel(0.0, kChargeColor, 0.3 + 0.7 * tri)
    end
    var k = 1
    while k <= level
      beam.pixel(barrier() + k * pw * 1.5, kChargeColor, 0.5)
      k += 1
    end
  else
    beam.pixel(0.0, kPlayerColor, 0.6)
    # Amber marker next to the player: steady when ready, slow pulse that
    # brightens as the recharge progresses otherwise.
    if cannon_cooldown <= 0.0
      beam.pixel(barrier() + pw, kChargeColor, 0.25)
    else
      var phase = elapsed * 0.8 - int(elapsed * 0.8)
      var tri = phase < 0.5 ? phase * 2.0 : 2.0 - phase * 2.0
      var progress = 1.0 - cannon_cooldown / kCannonRecharge
      beam.pixel(barrier() + pw, kChargeColor, (0.03 + 0.17 * tri) * (0.3 + 0.7 * progress))
    end
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

  if flash_left > 0.0
    render_cannon_flash()
  end

  var index = 0
  while index < size(worms)
    render_worm(worms[index])
    index += 1
  end
  index = 0
  while index < size(bullets)
    beam.pixel(bullets[index], kBulletColor, 1.0)
    index += 1
  end

  var j = 1
  while j <= stack
    beam.raw_pixel(j, kStackColor, 0.5)
    j += 1
  end
  render_player()
  beam.show_lives(lives)
end
