# Phase 3 — Launcher & persistence

The console stops being a single game and becomes a *device*: it boots to a menu,
remembers your highscores across power cycles, and lets you switch games without
reflashing.

## What's new

- **A launcher** — each game is a coloured block on the tube; scroll and launch.
- **Persistence** — settings and per-game highscores in LittleFS.
- **A second game** — *Reflex*, so the launcher has something to choose between.
- **Pause & exit** — nav-press pauses, then hold **B** to return to the launcher.
- **`navDelta()`** — a device-independent navigation abstraction (see below).

Deferred: **power management** (§5 of the plan's Phase 3). Battery sensing, the
low-battery pulse, the charging sweep and deep-sleep all need the ESP32 Feather's
voltage divider and its LiPo charging circuit, so they wait for the hardware.

## Working without the rotary encoder

The wheel hasn't arrived, so the launcher is driven by the joystick — but *not*
by reading the joystick directly.

Menus want **discrete steps**: one detent, one item. So `Input` grew
`navDelta()`, which returns the number of steps taken since the last frame. Right
now those steps are synthesized from the stick: push past a threshold and it
emits one step immediately, then auto-repeats while held, like a held arrow key.
There's hysteresis (step at 0.55 deflection, re-arm below 0.30) so a hovering
thumb doesn't spray steps.

When the encoder arrives, **only `Input::updateNav()` changes** — it will read
quadrature pulses and write the same counter. The launcher, and every future
menu, needs no changes at all.

That's the real reason to build it this way. The missing hardware forced the
abstraction that the project wanted anyway: menu code that never talks to a
specific input device. `Input::kNavButton` does the same job for the confirm
button (currently the stick's push switch, later the encoder's).

## Build & flash

```powershell
cd beam-boy-hw
& "$env:USERPROFILE\.platformio\penv\Scripts\pio.exe" run -e nodemcuv2-10px -t upload
& "$env:USERPROFILE\.platformio\penv\Scripts\pio.exe" device monitor -e nodemcuv2-10px
```

Wiring is unchanged from Phase 1. The filesystem is formatted automatically on
first boot — no `Upload Filesystem Image` step is needed, since the save file is
created by the firmware rather than shipped with it.

## Controls

**In the launcher**

| Control              | Action                                    |
|----------------------|-------------------------------------------|
| Stick left/right     | Change selection (one step per push)      |
| A, or stick press    | Launch the selected game                  |
| B (hold)             | Show that game's highscore in binary      |

**In a game**

| Control       | Action                                            |
|---------------|---------------------------------------------------|
| Stick press   | Pause / unpause                                   |
| B (hold, while paused) | Return to the launcher, saving your score |

### Why exit is gated behind the pause

The obvious design is "hold B to quit". It doesn't work here: Wormfight already
holds B for up to 1.1 s to charge a shot, so a bare hold-to-exit would fight the
game's own controls, and any future cartridge could collide the same way.

Requiring the pause first makes the two unambiguous, and it costs nothing —
you're not quitting mid-action anyway. While paused the game is frozen and
dimmed, with a slow amber breathing pulse at both ends so the console never
looks crashed; holding B fills the line from the player's end as a progress bar.

The engine handles all of this **before** the scene's `update()` runs, so every
game gets identical behaviour and none can swallow the gesture. That matters more
later than it does now: once cartridges are community-written, a downloadable
game must not be *able* to trap you inside it.

## Reading the launcher

Each game occupies a block of pixels in its accent colour. The selected block is
bright and breathing; the others sit dim at 22%.

The selection moves, **not the list** — so a game keeps the same physical
position on the tube and you learn it bodily ("Wormfight is the red one on the
left") rather than by reading. If there are more games than fit, the list scrolls
only when the selection would otherwise fall off the end, and a dim white pixel
appears at whichever end continues.

Launching floods the tube with the game's colour before handing over, which makes
the launch feel like a commitment rather than an instant cut.

| Game      | Accent | Id (storage key) |
|-----------|--------|------------------|
| Wormfight | Red    | `wormfight`      |
| Reflex    | Blue   | `reflex`         |

## Reflex — the second game

A dot sweeps back and forth; press **A** when it's inside the green target zone.
Hit it and the zone shrinks and the dot speeds up; miss and you lose a life.
Accuracy matters — the closer to the centre, the more points — so there's
something to master beyond simply hitting the zone.

It's deliberately tiny. It exists to give the launcher a real choice, to prove
the engine handles a second game with no changes, and because it's the natural
first cartridge to port to script in Phase 6: small enough to reason about
completely.

The zone is kept away from the very ends of the tube, where the dot turns around
and would otherwise linger inside it — that would turn a reflex test into a
waiting game.

## Persistence

Settings and highscores are written to `/beamboy.sav` in LittleFS as a single
versioned, checksummed binary record. Not JSON: parsing costs flash and RAM
better spent on games, and the data is small and fixed-size.

The record carries a version byte and an FNV-1a checksum. A Beam Boy is expected
to be reflashed often — that's the whole point of the cartridge model — and
struct layouts change between firmware versions. Rather than reading a corrupt
save, a version mismatch or bad checksum **silently resets to defaults**.

Highscores are keyed by the game's **string id**, not its index in the registry.
Installing a new game must not shuffle everyone else's scores. Those ids are
permanent: renaming one orphans every score filed under it.

Writes are deferred and only happen when something actually changed. `commit()`
is called when returning to the launcher — precisely the moment a dropped frame
is invisible, since the screen is about to be replaced anyway. Never mid-game.

Scores are filed by the *engine*, not by each game, via the `Scene::score()`
override. A cartridge can't forget to save, and can't report a score it didn't
earn.

## The game registry

`core/game_registry.h` is the list of installed games: an id, a title, an accent
colour, and a scene pointer.

It's deliberately shaped like the `meta.json` that downloadable cartridges will
carry. When games become scripts, the registry is populated by scanning the
filesystem instead of from a static table — and the launcher doesn't change.

Adding a native game is three steps: write the scene, include it in
`game_registry.cpp`, add a row to `kGames`.

## What to look for

**Launcher**
- [ ] Boots into the launcher showing two coloured blocks (red, blue)
- [ ] One step of the stick moves the selection by exactly one game, not several
- [ ] Holding the stick auto-repeats after a short delay
- [ ] The selection breathes; the unselected game stays dim
- [ ] Selection clamps at both ends rather than wrapping
- [ ] A (or stick press) floods the tube with the game's colour, then launches
- [ ] Hold B shows the highscore in binary (dim single pixel if it's zero)

**Pause & exit**
- [ ] Stick press during a game freezes it and dims the display
- [ ] Amber pulses appear at both ends while paused
- [ ] Stick press again resumes exactly where it left off
- [ ] Holding B while paused fills an amber bar, then returns to the launcher
- [ ] Releasing B early cancels, leaving the game paused

**Persistence**
- [ ] Play, score, exit; the highscore shows in the launcher (hold B)
- [ ] Power-cycle the board; the highscore is still there
- [ ] The launcher opens on the game you played last
- [ ] Serial banner reports `LittleFS mounted` and lists both games with bests

**Specifically worth trying to break**
- [ ] Pause Wormfight *while holding B mid-charge*, then keep holding B — it
      must **not** exit. The gesture only arms once B has been released.
- [ ] Double-tap the nav button quickly — you should get two toggles, not one.

**Reflex**
- [ ] The dot sweeps and bounces cleanly off both ends
- [ ] A inside the zone scores; the zone shrinks and the dot speeds up
- [ ] A outside the zone costs a life
- [ ] Lives pips at the far end decrease
- [ ] Game over shows the binary score; A restarts

**Performance**
- [ ] `[PERF]` still reports ~60 fps in both games and in the launcher

## Bugs caught in review

A code review of this phase found four real defects, all fixed. Worth recording,
because three of them would have been miserable to diagnose from the symptom.

**Pausing mid-charge exited instantly.** `holdDuration()` measures from when the
button went down, which could be *before* the pause began. Pausing Wormfight
while holding B to charge (~1.1 s) meant the first paused frame already saw more
than the 1.2 s exit threshold, and the console jumped to the launcher with no
gesture and no progress bar. Exactly the collision the pause gate was introduced
to prevent, reintroduced one layer down. Fixed by *arming*: B must be observed
released after the pause before the gesture counts.

**The debounce dropped transitions rather than delaying them.** A press and
release that both fell inside the 25 ms lockout vanished completely, so a fast
double-tap of the nav button registered as one toggle — leaving the console
frozen under the pause overlay when the player thought they'd resumed. The
debounce now *latches* the last raw level and applies it when the lockout
expires, so a transition can be delayed but never lost. This affects every
button, not just pause.

**Long game ids could never be found again.** Ids were stored truncated to 11
characters but compared across all 12, so any id at the limit never matched what
was written: the score would read as zero forever, and every exit would append a
duplicate entry until all 12 slots were consumed. Fixed by comparing only the
stored length, and by rejecting over-long ids outright — truncation could
otherwise silently collide two different games onto one highscore.

**A failed flash write reported success.** `save()` discarded both `write()`
return values and cleared the dirty flag regardless. A short write leaves a file
that `load()` correctly rejects, so every score would silently vanish on next
boot — and because the data was no longer dirty, no later `commit()` would ever
retry. Now the write is checked and the data stays dirty on failure.

The first two are both variants of the same underlying trap this project keeps
hitting: **state that persists across frames must not be derived from edges**.
Worth re-reading before writing any new input handling.

## Play-test result

Both games tested on hardware: **no bugs found**, and both felt smooth. The only
report was that colours look slightly off — in particular Reflex's "green" zone
not reading as green. That turned out to be real, and worth fixing properly.

### Why the green wasn't green

Two causes compounding:

1. **It genuinely wasn't green.** `kZoneColor` was `(0, 180, 120)` — a
   spring-green with substantial blue. Now `(0, 255, 40)`.
2. **Quantisation at low intensity.** The zone draws at 0.35 intensity, and the
   global brightness cap (64/255) then scales it again. The value reaching the
   LED was roughly `g=15, b=10` out of 255. At that level very few steps remain
   to express a hue, and WS2812 blue is perceptually stronger per unit than
   green, so a 15:10 ratio reads much bluer than the numbers suggest.

Both `present()` and `addToPixel()` also **truncated** rather than rounded,
adding a consistent downward bias precisely where the fewest steps were left.
Both now round with `+128` / `+127`.

### ⚠️ `Color::scaled()` must keep truncating

`scaled()` was deliberately **left truncating** while its siblings were changed.
This asymmetry is load-bearing:

`fade()` calls `scaled()` repeatedly **on its own output**. With rounding, a
channel at 1 scaled by 0.9 gives `(1*230+128)>>8 == 1` — a faded pixel would
never reach black and would stay faintly lit forever. That is exactly the
stuck-pixel class of bug fought in Phase 1.

**Rule: round in write-once paths, truncate in any path that feeds back into
itself.** `present()` and `addToPixel()` write to fresh destinations each frame,
so rounding is safe there.

## Tuning knobs

**Navigation** (`core/input.cpp`)

| Constant             | Default | Effect                                  |
|----------------------|---------|-----------------------------------------|
| `kNavThreshold`      | 0.55    | Deflection that counts as one step      |
| `kNavRelease`        | 0.30    | Must fall below this to step again      |
| `kNavRepeatDelayMs`  | 400     | Pause before auto-repeat begins         |
| `kNavRepeatRateMs`   | 140     | Auto-repeat interval                    |

**Launcher** (`scenes/launcher_scene.cpp`) — `kMinBlockPixels` (2),
`kMaxBlockPixels` (6), `kGapPixels` (1). On the 10 px strip two games get 4 px
blocks; on the 50 px tube they'll get the 6 px maximum.

**Reflex** (`scenes/reflex_scene.cpp`) — `kStartSpeed` (0.55),
`kSpeedPerRound` (0.075), `kStartZoneHalf` (0.13), `kZoneShrink` (0.9).

**Exit gesture** — `Engine::kExitHoldMs` (1200 ms).

## Still to do in this phase

Power management, once the Feather arrives:

1. Battery voltage sensing on ADC1 with a LiPo discharge curve
2. A hold-**B** battery meter in the launcher (the gesture is already free there)
3. The low-battery pulse and critical-voltage safe shutdown
4. The charging sweep animation
5. Idle deep-sleep with button wake

And the encoder, when it arrives: rewrite `Input::updateNav()` to read
quadrature, and move `kNavButton` to the encoder's push switch. Nothing else
should need to change — if it does, the abstraction was wrong.
