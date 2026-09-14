# Phase 8 — Power & battery management

Goal: untether from USB, run safely on battery power, and manage energy in
the OS, without ever risking a corrupted flash write when the battery runs
out.

## Hardware-reality correction

The plan for this phase was written before the Feather ESP32-S3 No PSRAM
arrived, and assumed a battery-sense ADC pin fed by a voltage divider (as many
Feather boards have). That assumption was wrong for this specific board:
Adafruit's own docs are explicit that "there is no pin on the Feather ESP32-S3
that returns battery voltage." Instead, the board has an on-board **MAX17048
fuel gauge** chip on I2C (address `0x36`), sharing the STEMMA QT bus (SDA/SCL,
which don't conflict with any button/stick/LED pin already in use).

This turned out to be a better situation than the original plan, not a worse
one: the MAX17048 already linearises the LiPo discharge curve internally and
reports a usable 0–100% state-of-charge directly, along with cell voltage and
charge rate. There is no divider ratio to get right and no hand-rolled
discharge-curve lookup table to tune.

## What's implemented

- **`src/core/power_policy.h`** — pure, host-testable decision logic, with no
  Arduino or I2C dependency, mirroring the existing `net_policy.h` split
  between "decide" and "do":
  - `classifyPowerLevel(percent, previous)` — `kNormal` / `kLow` / `kCritical`,
    with **two-sided hysteresis** so a percentage dithering right at a
    boundary can't flicker the displayed level: `kLowBatteryPercent = 15`
    (recovers at `20`), `kCriticalBatteryPercent = 5` (recovers at `10`).
    `classifyPowerLevel` takes the *previous* level explicitly, since recovery
    depends on which direction the battery is heading, not just where it sits.
  - `isChargingRate(percent_per_hour)` — a small positive threshold, not
    `> 0`, so resting jitter on the gauge's own charge-rate reading never
    reads as "charging."
  - `shouldEnterIdleSleep(idle_ms, charging)` — never true while charging:
    USB is powering the device regardless of what the ESP32 does, so sleeping
    saves nothing and only costs the charging-animation feedback.
  - `kIdleSleepMs = 120000` (2 minutes).
  - 11 native tests in `test/test_power_policy/` cover hysteresis in both
    directions, dithering survival, the charging threshold, and idle-sleep
    gating.

- **`src/core/power.h`/`.cpp`** — the thin, ESP32-only hardware wrapper around
  Adafruit's `Adafruit_MAX1704X` library. Gated entirely by
  `board::kHasBatteryMonitor` (`false` on the DevKitC and the native test
  build, `true` on the Feather): `begin()` returns `false` without touching
  I2C at all on a board with no chip, so this class is inert rather than
  "try and fail" on unsupported boards. Polls at most once a second. Exposes
  `available()`, `percent()`, `voltage()`, `charging()`, `level()`. Like
  `network.cpp`, this file is never compiled into the native test binary (see
  `[env:native]`'s `build_src_filter` in `platformio.ini`), so it needs no
  `BEAMBOY_NATIVE` guards of its own.

- **`Engine` integration** (`src/core/engine.h`/`.cpp`):
  - `updatePower()` runs every frame, right after input sampling and before
    the pause/exit-gesture logic — a critical battery pre-empts *everything*,
    mid-game, mid-pause, mid-menu.
  - The instant the level classifies as `kCritical`, `beginCriticalShutdown()`
    flushes storage (and the current game's score, if any) **before** a single
    frame of the shutdown animation plays, so the write is guaranteed to
    complete while power is still guaranteed — ahead of the hardware
    protection circuit's own abrupt cutoff. A red sweep closes in from both
    ends for `kCriticalShutdownMs`, then the device configures
    `esp_sleep_enable_ext1_wakeup()` on the A/B/stick-press GPIOs
    (`ESP_EXT1_WAKEUP_ANY_LOW`, since `Input` configures them
    `INPUT_PULLUP`) and calls `esp_deep_sleep_start()`.
  - While `kLow`, a single pulsing red pixel is drawn at the last index in
    every remaining render path (paused and normal) — visible but not
    disruptive. `kCritical` is not shown as an overlay; it hands off to the
    shutdown sweep instead.
  - **Idle sleep** is a *derived, stateless-per-frame* check, not a latched
    state machine: every frame, `now_ms - last_activity_ms_` is compared
    against `kIdleSleepMs`. Activity is any button held or stick deflection
    past a small deadzone (`held()`, not `pressed()` — a control physically
    held down still counts, so a game left running with the stick pushed to
    one side doesn't idle out from under the player). If idle and not
    charging, the framebuffer fades over `kIdleFadeMs` and the device enters
    the same deep sleep as a critical shutdown once the fade completes. If
    idle *and* charging, a green sweep animation (`kChargingSweepMs` period)
    plays instead of fading — this is deliberately not one-shot-latched, so
    any input, or plugging in USB mid-fade, falls back out of the idle path
    automatically on the very next frame with no extra bookkeeping to unwind.

- **Launcher-only battery gauge** (`src/scenes/launcher_scene.h`/`.cpp`): the
  on-demand percentage readout is a **launcher gesture, not a global one** —
  holding **A + B together** for ~0.5 s in the launcher shows a proportional
  bar (green → amber → red) driven by `engine.power().percent()`. This is
  deliberately scene-local rather than engine-level, so no game ever has to
  reserve the A+B combo for itself. On a board with no fuel gauge (the
  DevKitC), the same gesture shows a dim, steady white pixel instead of
  inventing a reading. While charging, the bar breathes rather than holding
  steady — there's no cable icon to draw on a 1D display.

- **`src/scenes/launcher_gestures.h`** — the arbitration between the three
  gestures that now share two buttons, extracted as pure logic and covered by
  17 native tests in `test/test_launcher_gestures/`. Adding the A+B combo
  turned the launcher's button handling into a small state machine with three
  overlapping claims on A and B, and all three bugs it produced were *ordering*
  bugs rather than threshold ones — see "Gesture ordering" below.

## Gesture ordering

Adding A+B to a launcher that already used A and B individually broke three
things, none of which reproduce reliably by hand (they depend on millisecond
ordering between two thumbs) and two of which destroyed user data:

1. **Pressing A a few milliseconds before B launched a game** instead of
   showing the gauge, because A's press edge had already fired by the time B
   arrived. Fixed by launching on A's *release* rather than its press: at
   press time it simply isn't yet knowable whether B is about to join. A tap
   releases within a frame or two, so this costs no perceptible latency.
2. **Letting go of A first after a few seconds on the gauge deleted the
   selected cartridge**, because B's hold duration was by then well past
   `kDeleteHoldMs`. Fixed with a `combo_engaged_` latch that is set the moment
   both buttons are down and cleared only once *both* are back up — so it
   deliberately outlives the combo itself, covering the messy window where one
   thumb has lifted and the other hasn't.
3. **Holding B to exit a game rolled straight on into a delete.** The engine's
   exit gesture is `kExitHoldMs` (1.2 s) and the launcher's delete is
   `kDeleteHoldMs` (2.5 s), so continuing to hold B for another 1.3 s after
   landing in the launcher wiped the cartridge the player was only trying to
   leave. This one predates the battery gauge entirely. Fixed with an `armed_`
   flag mirroring `Engine::exit_armed_`: both buttons must be seen up once
   after entering before either's gesture counts.

Two details in that logic are load-bearing and easy to undo by accident:

- `armed_` is read for the launch decision *before* it can be set later in the
  same call. Arming on the same frame as a release would defeat the guard
  entirely — the release that ends a carried-in hold is exactly the frame that
  would both arm the arbiter and fire a launch off the back of it. A test
  caught this during implementation.
- The arbiter returns the delete hold's *duration* rather than a bare "delete
  now" boolean, and both `update()` and `render()` read that one number. That
  is what keeps the countdown the player sees and the deletion that eventually
  fires from disagreeing: a suppressed hold renders no warning *and* performs
  no delete, with no second condition to keep in sync.

## Constants

| Constant | Value | Where |
| --- | --- | --- |
| `kLowBatteryPercent` / recover | 15 / 20 | `power_policy.h` |
| `kCriticalBatteryPercent` / recover | 5 / 10 | `power_policy.h` |
| `kIdleSleepMs` | 120000 (2 min) | `power_policy.h` |
| `kIdleFadeMs` | 1000 | `engine.h` |
| `kCriticalShutdownMs` | 1500 | `engine.h` |
| `kChargingSweepMs` | 2600 | `engine.cpp` |
| `kBatteryHoldMs` (A+B gauge) | 500 | `launcher_gestures.h` |
| `kDeleteHoldMs` | 2500 | `launcher_gestures.h` |

## Controls and tube vocabulary

- **A + B (hold, launcher only)**: show the battery gauge as a proportional
  bar. Released at any point, the launcher returns to the normal list
  immediately.
- **Persistent low-battery pixel**: a single pulsing red pixel at the last
  index, shown in every scene once the battery is classified `kLow`.
- **Charging sweep**: a green sweep animation, shown instead of idling out
  while plugged in.
- **Critical shutdown sweep**: a red sweep closing in from both ends, played
  once after storage is already safely flushed, immediately before deep sleep.
- **Idle fade + sleep**: after two minutes with no input and not charging, the
  display fades out and the device deep-sleeps; any of A, B or the stick
  press wakes it.

## Current limitations / not yet validated from this environment

- **Charging is detected with a lag of minutes, not seconds.** The MAX17048
  reports charging via its `CRATE` (charge rate, %/hr) register, which is
  derived from the ModelGauge algorithm's filtered state-of-charge trend
  rather than from a direct current measurement. After plugging in, it takes
  a while to converge past `kChargingRateThreshold` (1 %/hr), so the board's
  own orange CHG LED lights up well before the firmware agrees that it's
  charging. This is inherent to the sensor, not a bug — but it does mean
  "plugged in and the bar isn't pulsing yet" is expected behaviour for the
  first few minutes. Lowering the threshold would trade that lag for false
  positives from resting jitter, which is the failure mode the threshold
  exists to prevent. A genuine fix would need a real VBUS-present signal,
  which this board doesn't expose.
- The MAX17048 detection, its actual percentage/voltage readings, and the
  ext1 deep-sleep wake path can only be verified on real hardware — this
  environment has no physical Feather attached. Both firmware builds compile
  cleanly and the full native suite passes, but on-device behaviour (does the
  gauge get found on the STEMMA QT bus, do the thresholds feel right in
  practice, does the board actually wake on a button press) is still open.
