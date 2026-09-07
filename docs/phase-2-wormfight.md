# Phase 2 — Wormfight

The first *real* Beam Boy game, built on the Phase 1 engine. It descends from the
original prototype (`docs/wormfight-prototype.cpp.txt`), keeping the core idea —
segmented worms crawl along the line toward you, and you shoot them before they
arrive — while gaining waves, lives, particles, screen shake and a proper death
sequence.

## Build & flash

```powershell
cd beam-boy-hw
& "$env:USERPROFILE\.platformio\penv\Scripts\pio.exe" run -e nodemcuv2-10px -t upload
& "$env:USERPROFILE\.platformio\penv\Scripts\pio.exe" device monitor -e nodemcuv2-10px
```

Wiring is unchanged from Phase 1 — see `docs/phase-1-core-engine.md`. As a
reminder: **power the joystick from 3V3, not 5V**, since VRx feeds the ADC
directly.

## Controls

| Control  | Action                                              |
|----------|-----------------------------------------------------|
| Joystick | Move the turret within your zone                    |
| A        | Fire (also restarts after game over)                |
| B        | Hold to charge, release to fire a piercing shot     |

### The charged shot

Holding B builds a charge; releasing it fires. The longer the hold, the more
segments the shot punches through before it is spent — up to four at full charge.
A charged shot travels *slower* than a normal one, which is both a real trade-off
and what makes its power legible as it crosses the line.

A quick stab of B fires an ordinary shot, so B is never a dead button — you can
play with either hand on either button and nothing feels wasted.

The charge reads off the turret itself rather than a separate HUD element: the
player pixel glows amber and pulses faster as the charge builds, then holds a
steady white core when fully charged.

This replaced an earlier push-back ability, which did not justify occupying the
only spare button on a two-button console.

## Reading the display

The whole game has to be legible on one line, so every element has a distinct
visual signature:

| Element        | Appearance                                                    |
|----------------|---------------------------------------------------------------|
| Player         | Cyan dot with a dim cyan halo on its far side                 |
| Charging       | Player glows amber, pulsing faster; white core at full charge |
| Muzzle flash   | Brief white flare just ahead of the player                    |
| Worm head      | Bright red, the leading (nearest) segment                     |
| Worm body      | Dimmer crimson, trailing away toward the far end              |
| Worm hit       | The entire worm flashes white for ~0.12 s                     |
| Shot           | Pale blue dot with a short trail behind it                    |
| Charged shot   | Larger warm-white dot with a long amber trail, moving slower  |
| Lives          | Dim green pips at the **far end** of the line                 |
| Wave cleared   | Green pulse travelling out along the line                     |
| Game over      | Red sweep, then the score in binary                           |

Index 0 is the player's end. Worms extend from their head *away* from you, so a
worm's length is also its remaining hit count — you can see how much work a
target needs before you commit to it.

## Rules

- You are confined to the nearest 30% of the line (`kPlayerZone`). This is a
  defence game; the tension comes from worms closing on your zone, not from
  roaming.
- Each hit removes one segment and scores **1 point**. Destroying a worm scores
  a further **3 points**.
- A worm reaching you costs a life. The line clears and the current wave resumes
  from where it was, rather than restarting your progress.
- Clearing a wave advances to the next: more worms, faster, and slowly longer.
- At zero lives, the score is displayed in binary. Press A to restart.

## What to look for

- [ ] Player movement is smooth and stops cleanly at both ends of the zone
- [ ] The turret cannot leave its zone
- [ ] A fires; the rate limit (~4.5 shots/s) feels responsive, not sluggish
- [ ] Shots visibly travel and disappear at the far end
- [ ] Hitting a worm shortens it from the head and flashes it white
- [ ] Killing a worm produces a particle burst and a small shake
- [ ] B: a quick stab fires a normal shot
- [ ] B: holding makes the turret glow amber and pulse faster
- [ ] B: at full charge the turret shows a steady white core
- [ ] B: releasing fires a slower, larger shot that pierces multiple segments
- [ ] A worm reaching you triggers shake, a burst, and the fade-out death effect
- [ ] Lives pips at the far end decrease correctly
- [ ] Clearing a wave shows the green pulse, then the next wave is harder
- [ ] Game over shows the red sweep and then the binary score
- [ ] A restarts after game over, but not instantly (700 ms lockout)
- [ ] `[PERF]` still reports ~60 fps

## Tuning knobs

All in the anonymous namespace at the top of
`beam-boy-hw/src/scenes/wormfight_scene.cpp`. Everything is in normalised units
(0 = player end, 1 = far end) and per second, so the feel carries across strip
lengths unchanged.

| Constant           | Default | Effect                                     |
|--------------------|---------|--------------------------------------------|
| `kPlayerMaxSpeed`  | 0.55    | How fast the turret traverses the line     |
| `kPlayerAccel`     | 14.0    | Movement snappiness; lower = floatier      |
| `kPlayerZone`      | 0.30    | How much of the line you may occupy        |
| `kShotSpeed`       | 1.30    | Shot travel speed                          |
| `kFireCooldown`    | 0.22    | Minimum time between shots                 |
| `kChargeTimeMin`   | 0.25    | Below this, B fires a normal shot          |
| `kChargeTimeFull`  | 1.10    | Hold time for a full-power charge          |
| `kChargedShotSpeed`| 0.95    | Charged shot speed (slower than normal)    |
| `kChargedShotPower`| 4       | Segments a full charge punches through     |

Wave difficulty lives in `update()`: `base_speed` (`0.055 + 0.012 * wave`),
worm count (`2 + wave / 2`, capped at 8) and the spawn interval
(`2.6 - 0.18 * wave`, floored at 0.7 s).

Expect to adjust these on 10 px. The strip is short enough that worm speed and
the fire cooldown dominate the difficulty, and the values above were chosen
before play-testing.

## Notes on the implementation

**Worm length scales with the strip.** A worm is capped at a quarter of the
line (`display.pixelCount() / 4`, ceiling `kMaxSegments = 5`). On the 10 px strip
that means 2 segments; on the 50 px tube, the full 5. Without this, late-wave
worms would fill a short strip entirely and leave nowhere to play.

**Spawns that fail are retried, not lost.** `spawnWorm()` returns whether a slot
was free, and the wave counter only decrements on success. Otherwise a wave could
"spawn" worms that never appeared and end early.

**Screen shake** is a display-level offset (`Display::setShake()`) applied inside
`point()` and `span()`. `rawPixel()` deliberately bypasses it, so the lives pips
and the push-cooldown indicator stay rock-steady while the playfield shakes.

**A light fade instead of a clear** each frame leaves short trails on everything,
which makes fast movement far more readable at this resolution.

## Toolchain note

`Engine::renderScore()` contains two `noinline` helpers
(`bitIntensity()`, `drawScoreBit()`). These are **not** stylistic: with the code
inlined, the Xtensa GCC shipped with the ESP32 platform hits an internal compiler
error (`insn does not satisfy its constraints` during the postreload pass) trying
to load a float literal directly into an FP register. Splitting the float maths
out sidesteps it. Do not "simplify" them back inline without rebuilding the
ESP32-S3 environment.

**Charging is level-driven, not edge-driven.** `updateCharge()` reads
`input.held(Button::kB)` rather than press/release edges. An edge-driven charge
would stick on forever if a release edge were ever lost to debounce — the same
bug that produced the stuck score pixels in Phase 1.

**A charge is never silently swallowed.** `fire()` reports whether the shot
actually left; if it could not (fire cooldown, or no free shot slot), the charge
is *held* and retried next frame. Losing a full second of charging to an
invisible cooldown feels broken in a way that is very hard to diagnose by feel.

## Still to tune on the 50 px tube

The 10 px strip is too short to judge pacing properly — a worm crosses it in a
couple of seconds and there is barely room for the player zone and a worm at the
same time. The following are all expected to need revisiting once the 1 m tube
arrives:

- Worm base speed and per-wave escalation, which currently dominate difficulty
- `kPlayerZone` — 30% of 10 px is 3 pixels, which is cramped
- Worm length: capped at 2 segments on 10 px, but up to 5 on the tube, so late
  waves will play quite differently
- `kChargeTimeFull` and `kChargedShotSpeed` — a slow piercing shot has much more
  room to read as powerful on a longer line
- Spawn cadence, which may want to be denser once there is space for it
