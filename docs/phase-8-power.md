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
reports a usable 0–100% state-of-charge directly, along with cell voltage.
There is no divider ratio to get right and no hand-rolled
discharge-curve lookup table to tune.

The one thing the board does *not* expose is whether USB power is present, and
that is what charging detection needs. That signal comes from a small external
divider — see [USB power sense](#usb-power-sense) below.

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
  - `classifyChargeState(usb_present, voltage, percent, previous)` —
    `kOnBattery` / `kCharging` / `kFull`. No USB means on battery. With USB,
    `kFull` needs both `voltage >= kFullVoltage` (4.15 V) *and*
    `percent >= kFullPercent` (98%, not 100%, since the charger may
    terminate while the gauge still reads in the high 90s): voltage alone reaches ~4.2 V as soon as
    the charger enters its constant-voltage phase, at roughly 75%. `kFull` is
    latched until the cell sags below `kRechargeVoltage` (4.0 V) or USB is
    unplugged, so the post-charge relaxation doesn't flip it back to
    "charging".
  - `shouldEnterIdleSleep(idle_ms)` — true once idle for `kIdleSleepMs`,
    regardless of USB or charge state: a plugged-in console sleeps like any
    other.
  - `effectivePowerLevel(battery_level, usb_present)` — the level warnings and
    shutdown act on: `kNormal` while on USB, the battery's own level
    otherwise. The battery level keeps being classified underneath, so
    unplugging a near-empty console brings the warning or shutdown straight
    back.
  - `kIdleSleepMs = 120000` (2 minutes).
  - Native tests in `test/test_power_policy/` cover hysteresis in both
    directions, dithering survival, gauge-reading plausibility, charge-state
    classification (including the full-latch), the USB override of
    warnings, and idle-sleep gating.

- **`src/core/power.h`/`.cpp`** — the thin, ESP32-only hardware wrapper around
  Adafruit's `Adafruit_MAX1704X` library. Gated entirely by
  `board::kHasBatteryMonitor` (`false` on the DevKitC and the native test
  build, `true` on the Feather): `begin()` returns `false` without touching
  I2C at all on a board with no chip, so this class is inert rather than
  "try and fail" on unsupported boards. Polls at most once a second. Exposes
  `available()`, `percent()`, `voltage()`, `chargeState()`, `charging()`,
  `usbPowered()`, `level()`, `alertLevel()`. `level()` is the battery's own
  classification (diagnostics); `alertLevel()` applies
  `effectivePowerLevel()` and is what the engine acts on. The USB-sense pin is a plain `digitalRead`,
  sampled every frame and debounced over `kUsbDebounceMs`, so plugging in is
  reflected within a few frames rather than on the next gauge poll. Like
  `network.cpp`, this file is never compiled into the native test binary (see
  `[env:native]`'s `build_src_filter` in `platformio.ini`), so it needs no
  `BEAMBOY_NATIVE` guards of its own.

- **`Engine` integration** (`src/core/engine.h`/`.cpp`):
  - `updatePower()` runs every frame, right after input sampling and before
    the pause/exit-gesture logic — a critical battery pre-empts *everything*,
    mid-game, mid-pause, mid-menu.
  - The instant `alertLevel()` reads `kCritical`, `beginCriticalShutdown()`
    flushes storage (and the current game's score, if any) **before** a single
    frame of the shutdown animation plays, so the write is guaranteed to
    complete while power is still guaranteed — ahead of the hardware
    protection circuit's own abrupt cutoff. A red sweep closes in from both
    ends for `kCriticalShutdownMs`, then the device configures
    `esp_sleep_enable_ext1_wakeup()` on the A/B/stick-press GPIOs
    (`ESP_EXT1_WAKEUP_ANY_LOW`, since `Input` configures them
    `INPUT_PULLUP`) and calls `esp_deep_sleep_start()`.
  - **USB overrides both warnings.** While USB is present, the console runs
    from the cable through the Feather's load-sharing charger, so a critical
    battery triggers no shutdown and a low one shows no red pixel. Plugging in
    during the shutdown sweep aborts it (the flush that already happened is
    kept). Unplugging a critical battery starts the shutdown on the next
    frame.
  - While `alertLevel()` is `kLow`, a single pulsing red pixel is drawn at the last index in
    every remaining render path (paused and normal) — visible but not
    disruptive. `kCritical` is not shown as an overlay; it hands off to the
    shutdown sweep instead.
  - **Idle sleep** is a *derived, stateless-per-frame* check, not a latched
    state machine: every frame, `now_ms - last_activity_ms_` is compared
    against `kIdleSleepMs`. Activity is any button held or stick deflection
    past a small deadzone (`held()`, not `pressed()` — a control physically
    held down still counts, so a game left running with the stick pushed to
    one side doesn't idle out from under the player). Once idle, the
    framebuffer fades over `kIdleFadeMs` and the device enters the same deep
    sleep as a critical shutdown once the fade completes — whether or not USB
    is plugged in. This is deliberately not one-shot-latched, so any input
    mid-fade falls back out of the idle path automatically on the very next
    frame with no extra bookkeeping to unwind.

- **Launcher-only battery gauge** (`src/scenes/launcher_scene.h`/`.cpp`): the
  on-demand percentage readout is a **launcher gesture, not a global one** —
  holding **A + B together** for ~0.5 s in the launcher shows a proportional
  bar driven by `engine.power().percent()`. This is
  deliberately scene-local rather than engine-level, so no game ever has to
  reserve the A+B combo for itself. On a board with no fuel gauge (the
  DevKitC), the same gesture shows a dim, steady white pixel instead of
  inventing a reading. This is the **only place the tube shows the charge
  state**:

  | State | Bar |
  | --- | --- |
  | On battery | Steady; green > 50%, amber > 20%, red otherwise |
  | Charging | Green, breathing (55–100% brightness), length = percentage |
  | Full | Steady green, full length |

  On USB the bar is always green: the length already shows the level, and a
  red or amber bar while plugged in would read as a warning at exactly the
  moment the battery is being looked after.

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
   `kDeleteHoldMs` (now 3 s), so continuing to hold B for another 1.8 s after
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

## Trusting the gauge

The MAX17048 needs roughly 250 ms from reset before `VCELL` and `SOC` mean
anything, and Adafruit's `begin()` *resets the chip*. The original code read it
immediately afterwards and got `0.0% / 0.00V`.

Nothing downstream had any way to tell that apart from a battery about to die,
so on a full charge the console classified `kCritical`, ran the shutdown sweep
and went into deep sleep about a second into boot:

```
[power] MAX17048 found: 0.0%  0.00V  CRITICAL
[power] CRITICAL at 0.0%  0.00V -- flushing and shutting down
[power] level CRITICAL -> normal  (88.5%  4.09V)     <-- knew better, slept anyway
[power] entering deep sleep at 88.5%
```

Two independent faults, fixed separately:

- **A bad reading reached the policy layer at all.** `isPlausibleReading()`
  now gates every read, and `Power::readGauge()` commits percent and voltage
  to the members *together or not at all* — a rejected sample
  leaves the last known good values untouched. Voltage is the main
  discriminator: `0.00 V` is not something a board can read while executing
  this code, since the cell's protection circuit cuts long before that. An
  exact `0%` is rejected too, even at a normal voltage: on hardware the gauge
  was seen reporting `0%` with a believable VCELL shortly after boot. That
  costs nothing for a genuinely draining battery, which crosses the 5%
  critical threshold (and shuts down) long before it could read `0%`.
  `begin()` additionally
  gives the chip a bounded warm-up (8 × 50 ms) so the boot banner is
  meaningful; `update()` applies the same gate regardless, so a gauge slower
  than that budget still recovers on its own.
- **The shutdown was unconditional once started.** It now aborts if the
  battery climbs back out of critical mid-sweep. A real battery does not
  recover in two seconds, so this only ever fires on a reading that should not
  have been believed — but the console had already superseded that information
  and slept on it anyway, which is precisely the failure above.

`available()` deliberately conflates "no gauge" with "gauge not ready yet":
for every consumer the right response to both is identical — show nothing,
decide nothing, and above all do not shut down. Keeping them apart would mean
every call site had to check two things, and the one that forgot would be the
one that sleeps a healthy device. `gaugePresent()` exists only to tell the two
apart in diagnostics, and must not gate behaviour.

## Surviving deep sleep

Deep sleep powers down the digital IO subsystem, and two things that look like
firmware state are actually properties of that subsystem:

- **`INPUT_PULLUP` does not survive.** `Input::begin()` configures the digital
  pullups on A, B and the stick press. Once asleep those pins float, drift low,
  and trip `ESP_EXT1_WAKEUP_ANY_LOW` within moments. The console appeared to
  wake itself every couple of minutes: sleep → spurious wake → full reset →
  ~1.3 s of boot → launcher.

  Restoring them takes three steps, and the first two are easy to miss because
  omitting them fails *silently* rather than erroring:
  1. `rtc_gpio_init()` + `rtc_gpio_set_direction()` to move the pad onto the
     RTC mux. Until that happens the pad is still owned by the digital IO
     subsystem and any RTC pullup setting applies to something that is not
     listening — a no-op that looks exactly like a correct fix.
  2. `esp_sleep_pd_config(ESP_PD_DOMAIN_RTC_PERIPH, ESP_PD_OPTION_ON)` to keep
     the domain that *drives* those pullups powered. On `AUTO` the chip may
     power it down partway into the sleep, switching the pullups off and
     reintroducing the float intermittently, once already asleep.
  3. `rtc_gpio_pullup_en()` itself.

  The first attempt did only step 3 and still woke on GPIO10 (`mask 0x400`).
- **A floating data line lights the strip.** With `kPinLedData` floating next
  to a NeoPixel strip, the strip decodes noise and latches it — the half-strip
  of bright white seen during the spurious wake. The `display_.clear()` before
  sleeping *is* presented; it is then undone by the float. The pin is now
  driven low and pinned with `gpio_hold_en()` + `gpio_deep_sleep_hold_en()`.

Both fixes have a matching release in `Engine::begin()`, and forgetting either
is worse than the original bug, because both states survive the wake reset:

- `gpio_hold_dis()` — or the strip stays dark forever after the first sleep,
  since a held pad ignores the RMT peripheral.
- `rtc_gpio_deinit()` — or `Input::begin()`'s `pinMode()` configures a pad the
  RTC subsystem still owns, and the buttons stop responding.

`setup()` logs the wake cause and ext1 pin mask. A wake that reports no button
is the signature of this failure, which is otherwise invisible: a spurious wake
and a deliberate one produce an identical boot.

## Constants

| Constant | Value | Where |
| --- | --- | --- |
| `kLowBatteryPercent` / recover | 15 / 20 | `power_policy.h` |
| `kCriticalBatteryPercent` / recover | 5 / 10 | `power_policy.h` |
| `kIdleSleepMs` | 120000 (2 min) | `power_policy.h` |
| `kIdleFadeMs` | 1000 | `engine.h` |
| `kCriticalShutdownMs` | 1500 | `engine.h` |
| `kBatteryHoldMs` (A+B gauge) | 500 | `launcher_gestures.h` |
| `kDeleteHoldMs` | 3000 | `launcher_gestures.h` |
| `kMinPlausibleVoltage` / max | 2.5 / 5.0 V | `power_policy.h` |
| `kFullVoltage` / `kFullPercent` | 4.15 V / 98% | `power_policy.h` |
| `kRechargeVoltage` (full-latch release) | 4.0 V | `power_policy.h` |
| `kUsbDebounceMs` | 50 | `power_policy.h` |
| `kWarmupAttempts` × `kWarmupDelayMs` | 8 × 50 ms | `power.h` |

## Controls and tube vocabulary

- **A + B (hold, launcher only)**: show the battery gauge as a proportional
  bar — the only charge-state indicator (see the table above). Released at
  any point, the launcher returns to the normal list immediately.
- **Persistent low-battery pixel**: a single pulsing red pixel at the last
  index, shown in every scene once the battery is classified `kLow` — on
  battery only, hidden while USB is plugged in.
- **Critical shutdown sweep**: a red sweep closing in from both ends, played
  once after storage is already safely flushed, immediately before deep sleep.
  Never on USB; plugging in mid-sweep cancels it.
- **Idle fade + sleep**: after two minutes with no input, plugged in or not,
  the display fades out and the device deep-sleeps; any of A, B or the stick
  press wakes it.

## USB power sense

The Feather has no GPIO wired to VBUS, so a two-resistor divider brings it to
**GPIO12** (`board::kPinUsbSense`):

```
Feather "USB" pin ──[R1]──┬──[R2]── GND
                          │
                   Feather "12" pin
```

R1 : R2 must be **2 : 3**. Any pair from **10 kΩ / 15 kΩ** (the build uses
these) up to **100 kΩ / 150 kΩ** works — only the ratio sets the voltage.

- 5 V on USB becomes 3.0 V at the pin (2.85–3.15 V across the 4.75–5.25 V
  USB tolerance), comfortably above the S3's 2.48 V logic-HIGH threshold and
  below 3.3 V. On battery the `USB` pin is at 0 V and R2 holds the pin low.
- With 10k/15k the divider draws ~0.2 mA, from USB only — never from the
  battery. The lower impedance also makes the pin less susceptible to noise
  picked up from the LED strip.
- The pin is configured as plain `INPUT`. The S3's internal pull-down
  (~45 kΩ) in parallel with R2 would pull the high-value pairs too close to
  the threshold.
- `USB` and `12` are on the same 12-pin header edge (`BAT`, `EN`, `USB`, `13`,
  `12`, …); GND is on the opposite, 16-pin edge.

⚠️ The firmware assumes the divider is fitted. Without it GPIO12 floats and
the console will randomly believe it is plugged in.

## Power switch

The power switch sits **in series with the battery's + lead**, between the
LiPo and the Feather's JST connector. Off means the cell is physically
disconnected: zero drain, not just a low-current standby. Consequences:

- **The battery only charges while switched on.** Off, it is disconnected from
  the charger as well as from the load.
- **Plugging in USB while switched off still runs the console**, from USB
  alone. In that state there is no cell for the MAX17048 to measure: it reads
  the charger's output instead, so the battery percentage and charge state are
  meaningless until the switch is turned on.

## Current limitations / follow-up tuning

- **"Full" is an estimate.** The charger's own termination signal is not
  exposed either, so `kFull` is inferred from voltage + state-of-charge, which
  tends to read slightly early (the last few percent of the CV taper).
  `kFullPercent` is the knob if it proves too eager.
- **Hardware validation is complete.** The Feather finds the MAX17048 and
  reports plausible percentage/voltage values; idle deep sleep remains asleep
  until an actual button press, and the ext1 wake mask identifies the pressed
  button correctly. The only remaining follow-up is subjective threshold
  tuning after a full discharge cycle: the current 15% warning and 5% critical
  shutdown values are safe defaults, not unfinished functionality.
