# Beam Boy — Feasibility & Implementation Plan

A minimalist handheld game console with a **one-dimensional display**: a 1 m addressable
LED neon tube (50 px), two buttons, an analog 2-axis joystick with push button, WiFi, and downloadable games.

---

## 1. Verdict: does the idea make sense?

**Yes — and there is proven prior art.** The concept is a handheld descendant of
_Line Wobbler_ (Robin Baumgarten) and its open-source homage
[**TWANG**](https://github.com/bdring/TWANG), a 1D dungeon crawler running on an
addressable LED strip. TWANG has been built and played on strips of 60, 144, 288 and
450 LEDs, so **50 px is a proven, playable resolution** — at the short end, which means
game design should favour _timing and reflexes_ over _spatial detail_.

The one genuinely novel part of Beam Boy is the **downloadable game framework**. Nothing
off-the-shelf does this for LED-strip games, so it is the part of the project that needs
the most deliberate design — which is exactly what this plan front-loads.

### Key findings from research

| Topic                  | Finding                                                                                                                                                                                                                                                                                                  | Consequence for Beam Boy                                                                                                                                                                                                                                    |
| ---------------------- | -------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------- | ----------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------- |
| **MCU**                | On ESP8266, the standard NeoPixel driver disables interrupts for the whole strip write, which starves the WiFi stack (flicker, dropped packets, watchdog resets). ESP32 has the **RMT peripheral**, which generates WS2812 timing _in hardware_ with zero CPU blocking, and a second core for the radio. | **Move off the NodeMCU to an ESP32.** Confirmed with you.                                                                                                                                                                                                   |
| **Downloadable games** | Full-firmware OTA replaces the entire ~1 MB image: one game resident at a time, ~1 MB per switch. A scripting VM lets each game be a few-KB file in the flash filesystem, with dozens resident.                                                                                                          | **Native engine + script "cartridges."** Confirmed with you.                                                                                                                                                                                                |
| **Which VM**           | MicroPython/Espruino are too heavy to drive a 60 fps loop. **Berry** (the Tasmota scripting language) and **Lua** are lightweight bytecode VMs designed for this class of device. wasm3 is fast and sandboxed but is in "minimal maintenance."                                                           | Start with **Berry** as the front-runner, but **prototype-benchmark it before committing** (Phase 5 has an explicit bake-off). **Update: research confirms Berry is ESP32-only** — it cannot run on the ESP8266, so the bake-off happens on the S3 Feather. |
| **Frame budget**       | 50 px × 24 bits × 1.25 µs ≈ **1.5 ms per frame** on the wire — trivial. At 60 fps that is 9 % of the time budget, all handled by RMT hardware.                                                                                                                                                           | Rendering is a non-issue. The VM has ~15 ms/frame of headroom.                                                                                                                                                                                              |
| **WiFi setup**         | `WiFiManager` (captive portal) is the most battle-tested and needs no companion app, but phone captive-portal auto-popup is inconsistent and Android may drop an AP it deems internet-less. `improv-wifi` (BLE) is purpose-built for screenless devices but has no iOS web support.                      | **WiFiManager**, with the fallback "open `192.168.4.1` manually" documented. It also fits your "offline must work" requirement naturally.                                                                                                                   |
| **Power**              | **Measured** (10 px @ cap 25/255): 39 mA full white, 9.5 mA for a realistic game frame → ~3.9 mA/px. Projected to 50 px: **~195 mA full white, ~33 mA in normal play**.                                                                                                                                  | With the MCU at ~35 mA, normal play is **~70 mA** → **30+ hours** from a 2500 mAh cell. Power is a non-issue; the brightness cap was raised from 25 to **64** and can go higher.                                                                            |
| **Charging**           | A bare TP4056 has no load sharing: playing while plugged in draws through the battery, confusing end-of-charge detection and wasting cycles. A DevKitC + TP4056 build also ends up with two USB ports.                                                                                                   | **Use a board with integrated LiPo charging** (Adafruit Feather ESP32-S3): one USB-C port for charge _and_ flash, correct load sharing, battery sense pre-wired. See §2.1.                                                                                  |
| **LiPo direct drive**  | WS2812B tolerates ~3.5–5.3 V. Driving the strip straight off the LiPo (3.7–4.2 V) avoids both a boost converter _and_ the 3.3 V→5 V data level shifter, since VCC and logic level then nearly match.                                                                                                     | **Skip the boost converter and the level shifter.** The cell's protection cutoff (~3.4 V) keeps the strip in range; firmware shuts down cleanly before it trips. Verify on your specific tube in Phase 0.                                                   |
| **Input**              | TWANG's spring-doorstop + MPU6050 controller is genre-defining but built for a floor-standing cabinet. Its feel depends on _analog_ input — an encoder cannot express "move slowly left".                                                                                                                | **Analog thumbstick (2-axis + push) + 2 buttons.** Stick gives absolute and velocity control for both X and Y axes; push button and buttons A/B provide rich interactions with minimalist hardware.                                                         |
| **Monetization**       | Paid cartridges need a backend, accounts, per-device keys and signed+encrypted code — and DRM on an openly self-flashable device is defeatable by rebuilding the firmware. Comparable projects (TWANG, ESPboy) monetize via hardware.                                                                    | **Keep games free and open; sell hardware kits.** The free library is what makes the hardware worth buying. Store design leaves the door open for paid games later.                                                                                         |

### Risks, honestly

1. **The scripting VM is the make-or-break unknown.** If Berry can't hold 60 fps, the fallback
   is a "data-driven engine" (games are declarative level/behaviour descriptions interpreted
   by native C++ code) — less flexible but guaranteed fast. Phase 5 decides this with a benchmark, early.
   **Update — measured, and the risk is retired.** On the ESP8266 (the pessimistic board,
   no FPU) at 100 entities the whole frame costs 11 % of budget. ~84 % of that is _draw_
   calls, which stay native under any VM; only the update half is interpreted. That leaves
   **~52× of headroom on the interpreted half**. The native+scripted architecture is sound.
   See [`docs/phase-5-vm-bakeoff.md`](docs/phase-5-vm-bakeoff.md).
2. **50 px is a low resolution.** Mitigate by rendering in an abstract 1000-unit coordinate
   space (TWANG's trick) with **sub-pixel anti-aliasing** — a "dot" at position 12.4 lights
   px 12 at 60 % and px 13 at 40 %. This makes 50 px feel dramatically smoother and is cheap.
3. **A silicone IP67 neon tube is hard to open and hard to solder to.** Plan the wire entry and
   strain relief before designing the case.
4. **Scope.** The plan is ordered so you have a playable device at the end of Phase 2, long
   before any of the download machinery exists.

---

## 2. Hardware

### Bill of materials

| Part              | Choice                                                                                     | Notes                                                                                                                                                                                                                                                                                                          |
| ----------------- | ------------------------------------------------------------------------------------------ | -------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------- |
| MCU               | **Adafruit Feather ESP32-S3 (8 MB flash, 2 MB PSRAM)** — _recommended_                     | ~€18. **Has LiPo charging and a JST battery connector built in**, sharing the same USB-C port used for flashing: one port for everything, proper load sharing, and an on-board MAX17048 fuel gauge (I2C) for battery percentage — no ADC divider to wire or calibrate. Solves the charging design in one part. |
| _MCU alternative_ | **ESP32-S3-DevKitC-1 (N16R8)** + separate TP4056 USB-C charger                             | ~€10 + €2. Cheaper and more flash, but you must solve charging yourself — see §2.1.                                                                                                                                                                                                                            |
| Display           | Your **WS2812B silicone neon tube, 50 px / 1 m, IP67**                                     | Already owned.                                                                                                                                                                                                                                                                                                 |
| Stick             | **2-axis analog thumbstick with push switch** (PS2-style module)                           | ~€2. Gives absolute + velocity control on X and Y axes.                                                                                                                                                                                                                                                        |
| Buttons           | **2 × 6 mm tactile switches**                                                              | Named **A** (action/confirm) and **B** (back/cancel).                                                                                                                                                                                                                                                          |
| Battery           | **LiPo pouch cell, 2000–2500 mAh, with JST-PH connector and built-in protection**          | Rechargeable — the user never buys a battery. A pouch cell fits a flat handheld grip far better than a cylindrical 18650. Must include a protection circuit (most pouch cells with a JST lead do).                                                                                                             |
| Power switch      | **SPST slide switch between Feather `EN` pin and `GND`**                                   | Pulling `EN` to GND disables the 3.3V LDO regulator (sub-microamp standby). **USB charging remains fully functional when switched off**, allowing true zero-power off while retaining single-port charging.                                                                                                    |
| Misc              | JST connector for the tube, 470 µF cap across strip power, 330 Ω resistor in the data line | Standard NeoPixel hygiene — the cap absorbs inrush, the resistor tames data ringing.                                                                                                                                                                                                                           |

**Total: roughly €35–45** on top of what you own (Feather route), or €27–37 with the
DevKitC + separate charger.

### 2.1 Charging & power management

The device must be **rechargeable over USB-C** — no consumable batteries, ever. Three things
have to be right, and the naive build gets all three wrong:

**1. One USB port, not two.** A DevKitC + TP4056 build ends up with _two_ USB ports: one to
charge, one to flash. That's confusing for users and awkward to lay out in a case. The Feather
route collapses them into one connector.

**2. Load sharing.** ⚠️ A bare TP4056 wires the load directly across the battery terminals, so
playing while plugged in draws current _through_ the battery. This confuses the charger's
end-of-charge detection and causes needless charge cycles that shorten the cell's life. Proper
load sharing powers the system from USB and charges the battery separately when both are
present. The Feather's charger does this correctly; if you go the DevKitC route, buy a
**TP4056 module with load-sharing / "ideal diode" output** (sometimes sold as TP4056 + DW01 +
FS8205 with separate `OUT+`/`OUT-` pads), and power the system from `OUT`, never from the
battery pads directly.

**3. Low-voltage cutoff.** The cell's protection circuit must cut off around **3.0–3.5 V**.
This protects the battery _and_ keeps the WS2812 tube inside its ~3.5 V minimum operating
range — below that the LEDs start to misbehave before the battery is actually flat. Firmware
should warn well before this point (see below).

**Battery life.** Measured in Phase 0 on a 10 px strip and projected to the 50 px tube:
a realistic game frame draws **~33 mA** from the LEDs, plus ~35 mA for the ESP32-S3 with the
radio off — about **70 mA** in normal play. A 2500 mAh cell therefore gives **30+ hours**,
with a worst-case full-white frame at ~195 mA still only reaching ~4 hours. Recharge is
~2–3 h at 500 mA.

This is comfortable enough that **brightness is a look-and-feel decision, not a power
constraint** — the cap exists to bound the worst case, not to ration the battery.

**Firmware side (implemented in Phase 8):**

- Read battery state from the Feather's on-board **MAX17048 fuel gauge** over I2C (not an ADC pin
  — the Feather ESP32-S3 has none for battery sense; see Phase 8 below), which already linearises
  the LiPo discharge curve and reports 0–100% directly.
- **Battery meter on demand:** in the launcher, hold A+B to render charge level as a bar along the tube, green → amber → red.
- **Low-battery warning:** below ~3.5 V, subtle, persistent/periodic red pixel indicator during play — visible but not disruptive.
- **Critical cutoff:** below ~3.35–3.4 V, save state, show a red sweep, and deep-sleep before the
  hardware protection circuit cuts out mid-game (protects game data & flash storage).
- **Charging indicator:** while charging, animate a slow filling green sweep along the tube;
  solid green when full. The tube _is_ the status LED — no extra indicator needed, which
  suits the minimalist brief.
- **Idle sleep:** after ~2 minutes with no input, fade out and deep-sleep; wake on a button
  press. This is the single biggest real-world battery win.
- **Power cutoff & charging design:** A physical SPST switch connects the Feather's `EN` pin to `GND`.
  When switched OFF, the 3.3V LDO is disabled (sub-microamp standby), while the battery charger remains
  directly connected to USB-C and the LiPo cell — allowing safe charging over USB while completely powered off.

### Interim: what you can build on the NodeMCU today

Phases 1–3 (≈5 days of work: core engine, first game, launcher) are **fully doable on your
existing ESP8266** while the ESP32 ships. Two rules make the port a config change rather than
a rewrite:

1. **Use `NeoPixelBus` from the very first line of code**, with `NeoEsp8266DmaWs2812xMethod`
   on the ESP8266 and `NeoEsp32RmtNWs2812xMethod` on the S3. Same API, same library — only
   the method typedef differs. Do _not_ start on `Adafruit_NeoPixel`; its bit-banged output
   is the exact thing that breaks WiFi later.

   ⚠️ **DMA is interrupt-safe, not contention-free — on the ESP8266.** It still drives the
   data pin for the length of the strip plus a reset gap on every refresh, and
   `NeoPixelBus::Update()` spins on `yield()` waiting for the previous transfer — which on the
   ESP8266 runs the SDK's scheduled work from inside the render path. Refreshing every frame
   while the radio was associating crashed the device in the PHY layer (`DefFreqCalTimerCB`,
   `ppCheckTxIdle`) with 39 KB of heap free. `Display::setRefreshDivider()` reduced but did not
   eliminate the crash there. **Confirmed fixed on the ESP32-S3**, whose RMT peripheral and
   FreeRTOS scheduling remove the contention structurally — `NetworkScene` no longer calls
   `setRefreshDivider()` at all. The ESP8266 remains broken here by design; see
   `docs/phase-4-wifi.md`.

2. **All hardware access lives behind `Display` and `Input`.** Game and scene code never
   touches a GPIO.

Two ESP8266 limitations to be aware of: it has **only one ADC pin**, so the 2-axis thumbstick
can't be tested until the S3 arrives (add stick support on real S3 hardware), and it is short on usable GPIOs — you may need to drop
a button temporarily. Phases 4+ (WiFi, OTA, scripting VM, store) should wait for the ESP32.

### Pin map (ESP32-S3, starting point)

| Signal        | GPIO          | Notes                                                                                          |
| ------------- | ------------- | ---------------------------------------------------------------------------------------------- |
| LED data      | 17            | RMT-capable WS2812B data line. Via 330 Ω.                                                      |
| Button A      | 15            | `INPUT_PULLUP`                                                                                 |
| Button B      | 16            | `INPUT_PULLUP`                                                                                 |
| Stick push    | 18            | `INPUT_PULLUP`                                                                                 |
| Stick X       | 4             | ADC1 channel (ADC1_CH3) — usable while WiFi is active.                                         |
| Stick Y       | 5             | ADC1 channel (ADC1_CH4) — usable while WiFi is active.                                         |
| Battery sense | I2C (SDA/SCL) | MAX17048 fuel gauge (Feather only) — no dedicated GPIO; shares the STEMMA QT bus. See Phase 8. |

⚠️ Keep every analog input on **ADC1**. ADC2 is shared with the WiFi radio and reads garbage
whenever WiFi is on — a classic ESP32 trap that would silently break analog inputs in Phase 4.

### The minimalist control scheme

Physical inputs cover everything by reusing them contextually:

- **Stick X / Y** — analog movement / aim / velocity control.
- **Stick X (flick)** — discrete stepping (`navDelta`); scrolls menus.
- **A** — primary action; in the menu, "launch / confirm".
- **B** — secondary action; in the launcher, hold-B deletes an installed cartridge.
- **Stick press** — in game: pause (then hold B to exit); in launcher: hold to show highscore.

---

## 3. Software architecture

```
┌───────────────────────────────────────────────┐
│  Games (downloadable script cartridges)       │  ← .be / .beam files in LittleFS
├───────────────────────────────────────────────┤
│  Beam API   — draw, input, timing, save, rng  │  ← the stable contract
├───────────────────────────────────────────────┤
│  Script VM (Berry)                            │
├───────────────────────────────────────────────┤
│  Launcher · Store client · WiFi provisioning  │
├───────────────────────────────────────────────┤
│  Core: Display · Input · Storage · Power      │
├───────────────────────────────────────────────┤
│  ESP32 (RMT LED driver, LittleFS, WiFi)       │
└───────────────────────────────────────────────┘
```

### The Beam API — the most important design decision

This is the contract every game is written against. Keep it **tiny and stable**; every
addition is forever. Draft:

```lua
-- Coordinates are floats in 0..1 (resolution-independent!), colors are 0xRRGGBB.
beam.clear()
beam.pixel(pos, color, brightness)      -- sub-pixel anti-aliased
beam.line(from, to, color)
beam.fade(amount)                        -- for trails/afterglow
beam.pixel_count() / beam.pixel_width()

beam.stick(axis) / beam.stick_x/y()      -- analog axis ("x"|"y"), deadzoned + calibrated, -1..1
beam.pressed("a"|"b"|"stick")            -- edge
beam.held("a"|"b"|"stick")               -- level

beam.time()                              -- ms since game start
beam.random(n) / beam.random(lo, hi)
beam.score(add) / beam.highscore()       -- highscores, persisted per game
beam.show_score(elapsed_ms)              -- engine-drawn binary score animation
beam.raw_pixel(idx, color, intensity)    -- direct index for UI chrome
beam.exit()                              -- return to launcher
```

Each game script exposes `init()` and `tick(dt)`. The engine owns the main loop, the
frame pacing, the brightness cap and the pause overlay — the game never does.

### Showing numbers: binary score readout

There is no way to render digits on a 1D display, so **lean into it**: scores are shown in
**binary**, LSB at the near end, one pixel per bit.

- Lit pixel = 1, dark = 0. A score of 37 lights pixels 0, 2 and 5.
- Colour the bits by nibble (e.g. alternating warm/cool every 4 bits) so you can read the
  place values at a glance without counting.
- Animate it: on game over, sweep the bits in one at a time with a rising pitch of brightness,
  then hold. It reads as a deliberate "score reveal" rather than a limitation.
- 50 px is far more than the ~17 bits any sane score needs, so render it in the middle third
  of the tube and keep the rest dark.

Put this in the **engine** as `beam.show_score()`, not in each game — it becomes a
recognisable piece of Beam Boy's identity, consistent across every cartridge.

Rendering in **0..1 float space** means a game written today still works if you later
build a 144 px Beam Boy. That is worth the tiny cost now.

### Game "cartridge" format

A game is a folder in LittleFS:

```
/games/wormfight/
  game.be        -- the script
  meta.json      -- name, author, version, api_version, accent color
```

`meta.json` carries an **`api_version`**, so the launcher can refuse to run a cartridge
built against a newer API than the firmware provides, and tell the user to update.

### The "store"

Deliberately dumb, and therefore robust: a **static JSON index plus script files hosted on
GitHub Pages** (or any static host).

```json
{
  "api_version": 1,
  "games": [
    {
      "id": "wormfight",
      "name": "Wormfight",
      "version": "1.2",
      "url": "https://.../wormfight/game.be",
      "sha256": "..."
    }
  ]
}
```

The device fetches the index, shows the list on the tube, downloads the chosen script,
verifies the hash, writes it to LittleFS. **No server to run, no backend to maintain**, and
publishing a new game is a `git push`. Firmware updates use ordinary HTTPS OTA on top of this.

---

## 4. Step-by-step implementation plan

Every phase ends with **something you can see or play**.

### Phase 0 — Hardware bring-up _(½ day — start today on the ESP8266)_

> _Goal: the tube lights up, on battery._

1. Order the MCU (**Adafruit Feather ESP32-S3** recommended — see §2.1 for why charging drives
   this choice), thumbstick module, buttons, and a **2000–2500 mAh LiPo with
   JST-PH connector**. Meanwhile, do steps 2–5 on the NodeMCU you already have.
2. Swap `Adafruit_NeoPixel` → **`NeoPixelBus`** with `NeoEsp8266DmaWs2812xMethod`
   (later `NeoEsp32RmtNWs2812xMethod`). This is the single most important early decision —
   see "Interim" above.
3. Breadboard: tube data → GPIO via 330 Ω, 470 µF across strip power.
4. Run a rainbow sweep. Confirm 50 px, confirm the pixel order/direction.
5. **Critical experiment:** power the tube directly from the LiPo and check it still
   lights cleanly at ~3.5 V (simulate with a bench supply if you have one). If it misbehaves,
   add a 5 V boost + 74AHCT125 level shifter and update the BOM.
6. **Verify charging end-to-end** once the board arrives: plug in USB-C, confirm the charge
   LED behaves, confirm you can **play while charging** without the charge state glitching,
   and confirm flashing still works with the battery connected.
7. **Measure actual current draw** at your intended brightness cap, with a USB power meter or
   multimeter. This converts the 7–15 h estimate in §2.1 into a real number and sets the
   final brightness default.

✅ _Visible result: a glowing 1 m tube running off a rechargeable battery, charging over USB-C._

### Phase 1 — Core engine _(1–2 days — ESP8266 is fine)_

> _Goal: the foundation everything else stands on._

1. `Display` class: float 0..1 coordinate space, **sub-pixel anti-aliased** `pixel()`,
   `fade()`, global brightness cap (start at 25/255), `present()`.
2. `Input` class: analog joystick reading with deadzone, response curve shaping, debounced buttons,
   `pressed()` / `held()` / `released()` edges.
3. Fixed-timestep game loop at 60 fps with a frame-time budget assert.

   **Sanctioned exception:** the frame gate assumes the frame loop is the only thing with a
   deadline. That is true for games and false for the WiFi stack, whose deadlines are enforced
   in the SDK — a missed one is a fault in the PHY, not a dropped frame. `Scene::idle()` +
   `Engine::setIdleServiced()` let a scene be serviced on the frames the engine _skips_. This
   permits being called **more often**, never blocking, and is opt-in per scene (cleared on
   every scene change). Games are unaffected and still see a fixed timestep.

4. `beam.show_score()`: the binary score readout, animated bit-by-bit.
5. A `demo` scene: a joystick-controlled anti-aliased dot with a fading trail.

✅ _Visible result: a smooth, glowing dot you steer with the stick. This is the first moment the device feels real — the anti-aliasing is the "wow"._

### Phase 2 — First real game, native _(2–3 days)_

> _Goal: a genuinely fun game, written in C++ against the engine API._

Port your `main.cpp` monster-shooter into the engine as **"Wormfight"** and deepen it:

- **Stick** moves the shooter (analog — you can creep or dash); **A** fires; **B** is a
  short cooldown-limited "push back".
- Waves of monsters with rising speed; multiple monsters at once.
- Life system rendered as a few pixels at your end; screen-shake / white-flash on hit.
- Death animation, then `beam.show_score()` in binary, restart with **A**.

Once the S3 arrives, wire up the thumbstick here and tune the deadzone and response curve —
analog feel is worth spending real time on, since it defines how the console plays.

Write it against the _exact_ API shape you intend to expose to scripts — so the port to
a script in Phase 6 is mechanical.

✅ _Visible result: a game you actually want to hand to someone. Get feedback here before building any infrastructure._

### Phase 3 — Launcher & persistence _(1–2 days)_

> _Goal: more than one thing on the device._

1. ✅ Mount **LittleFS**; store settings and per-game highscores in NVS/LittleFS.
2. ✅ A **launcher scene**: each installed game is a colored block on the tube; the stick scrolls,
   the selected one pulses, **A** launches. Its accent color comes from `meta.json`.
3. ✅ Stick-press during a game → pause → hold **B** → back to launcher.
4. ✅ Add a second, tiny native game (e.g. a reflex "stop the dot in the zone" game) so the
   launcher has something to choose _between_.
5. ⏸ **Power management** (needs the ESP32 Feather; moved to Phase 8): battery voltage sensing,
   battery gauge indicator, low-battery warning on the tube, critical shutdown with data protection,
   charging sweep animation, and idle deep-sleep. _See Phase 8._

**Navigation via stick.** `Input` has `navDelta()`:
discrete steps synthesized from horizontal joystick movement (threshold + hysteresis + auto-repeat).
Menus talk to `navDelta()` rather than raw continuous axis data.

**Exit is gated behind pause**, not a bare hold-B: Wormfight already holds B for up to 1.1 s to
charge, and future cartridges will collide the same way. The engine handles pause/exit _before_
the scene updates, so no game — including a future community cartridge — can trap the player.

✅ _Visible result: switch between two games without reflashing, and see your battery level. It's a console now._

### Phase 4 — WiFi, opt-in _(1–2 days)_

> _Goal: online, but only when you say so._

**Two independent update paths — don't confuse them:**

|              | Games (Phases 6–7)           | **Firmware OTA** (step 4 below)                         |
| ------------ | ---------------------------- | ------------------------------------------------------- |
| Updates      | Cartridge scripts + metadata | The C++ engine: renderer, input, launcher, network code |
| Written to   | LittleFS data partition      | The **app partition** (executable)                      |
| Reboot       | No                           | Yes                                                     |
| Failure risk | One broken game              | **A bricked console**                                   |

Firmware OTA is what lets you fix an engine bug, extend the script API, or patch a
security hole on a device already in a user's hands, without asking them for a USB
cable. It works by splitting flash into **two app slots**: the device runs from A,
downloads into B, verifies, then flips a pointer and reboots into B. An interrupted
or corrupt download leaves A untouched and the console boots normally. This is why
the BOM specifies 8 MB flash — two complete copies must fit.

Both current environments already have OTA-capable layouts (ESP8266: 325 KB of a
1019 KB slot; ESP32-S3: 366 KB of 2 MB).

1. ✅ **Provisioning via captive portal.** Trigger: a "Network" entry at the end of the
   launcher list. Radio stays **off** until then. WiFiManager was rejected during
   implementation — it blocks the main loop, which would freeze the tube for the whole
   portal session. Hand-rolled instead (SoftAP + DNS hijack + small web server), serviced
   from the frame loop. **Crashed on the ESP8266** in the WiFi PHY — LED DMA contending
   with the radio — and was left unfixed there deliberately; **confirmed fixed on the
   ESP32-S3 DevKitC**, where the same repro is stable. The ESP8266 remains dev-only for
   anything touching the radio; WiFiManager stays rejected, since the S3 doesn't need the
   protection its blocking portal would have accidentally provided.
2. ✅ Show provisioning state _on the tube_: portal-active = slow amber pulse; connecting =
   blue sweep; connected = green flash; failed = red flash. Each state has a distinct
   _motion_ as well as a colour, since hue quantises badly when dim and red/green alone
   excludes colourblind players.
3. ✅ Verify offline behaviour is untouched: no saved credentials ⇒ never scans, never blocks,
   boots straight into the launcher.
4. ✅ Add **firmware OTA** ("Update" from the Network scene) with a progress bar drawn on the
   tube. ⚠️ Uses `setInsecure()` — **image signing is required before any real release**; see
   [`docs/phase-4-wifi.md`](docs/phase-4-wifi.md).

✅ _Visible result: configure WiFi from your phone with no display, and push firmware updates over the air._

_Four bugs were caught in review; five more crashes were found on hardware. The last of
those was confirmed fixed on ESP32-S3 hardware and left unfixed on the ESP8266 by
design. See [`docs/phase-4-wifi.md`](docs/phase-4-wifi.md)._

### Phase 5 — VM bake-off ⚠️ _(1–2 days — do this before Phase 6)_

> _Goal: prove the scripting model before betting the architecture on it._

> **Update:** Berry only needs an ESP32 core, not the Feather's charging
> circuit, so this ran on the `esp32-s3-devkitc-1-n16r8` dev board rather than
> waiting for the Feather. See
> [`docs/phase-5-vm-bakeoff.md`](docs/phase-5-vm-bakeoff.md) for the vendoring
> notes, the real hardware measurements (per-entity and batched), and the
> resulting cartridge-API design constraint.

1. ✅ Build the **native baseline** (`BenchScene`) — runs on the ESP8266 today, no
   Feather needed. Sweeps 10/25/50/100 entities and prints, as CSV, the frame
   cost split into update vs. draw plus **`update_headroom`**: how many times
   slower than native the _interpreted_ half may be and still hold 60 fps. That
   number is the pass mark for every candidate. Running it on the ESP8266 is
   deliberate — it is the pessimistic board.
   **✅ Result: 52× headroom at 100 entities. Scripting is viable.** The run also
   exposed a soft-float bottleneck in the renderer, since fixed to 8.8 integer
   maths for a 35 % cut in draw cost.
2. ✅ Embed **Berry** into the firmware on the S3 (`VmBenchScene`, both a
   per-entity-call and a batched-call variant, toggled with B).
   **✅ Result on real hardware: `update_headroom` = 3.5 at 100 entities
   per-entity, 69.5 batched.** The per-entity number is a real pass but tight —
   almost entirely per-call VM↔native crossing overhead. Batching (one call per
   frame over a persistent list) confirms that: headroom jumps ~20× and stops
   shrinking with entity count.
3. ✅ Reimplement `runWorkload()` — and _only_ that function — as a script. The tight
   boundary is what makes the comparison meaningful.
4. ✅ **Compare against the baseline.** Pass = beats `max_vm_slowdown` with headroom.
   **Passed comfortably with the batched calling convention (69.5× at 100
   entities).**
5. ✅ **Batch the call.** One script call over the whole entity list, looping
   internally, instead of one call per entity.
   **✅ Result: confirms the gap was call overhead, not interpretation cost —
   see item 2.** Concrete consequence for Phase 6: the cartridge API should
   expose one "update all entities" entry point that owns its own list, not a
   per-entity callback.
6. mruby/c comparison (`vm-mruby-compare`) is no longer necessary to justify
   Berry on performance — parked, not pursued, unless authoring ergonomics or
   footprint become a problem later. If Berry ever does fail outright, fall back to
   the **data-driven engine** (games as declarative JSON describing entities,
   waves and rules, interpreted natively). _mJS was rejected on expressiveness —
   no closures or classes makes for a poor cartridge language._

✅ _Visible result: a hard number that de-risks the whole rest of the project. Don't skip it._

### Phase 6 — Games as scripts _(2–3 days)_

> _Goal: the cartridge model, working locally._

> **Update:** `beam` API bound, Reflex ported to a script and confirmed playing
> identically to the native version on real ESP32-S3 hardware, cartridges load
> from `/games/` on LittleFS, and script cartridges are now sandboxed (time
> budget + memory ceiling). Phase 6 is functionally complete; item 5
> (hot-reload) is deliberately deferred. See
> [`docs/phase-6-cartridges.md`](docs/phase-6-cartridges.md) for the API
> surface, the cartridge format, and what's still open.

1. ✅ Expose the full Beam API to the VM (`src/vm/beam_api.*`).
2. ✅ Port a game from C++ to script. **Reflex**, not Wormfight -- deliberately
   the smaller cartridge, chosen in docs/phase-5-vm-bakeoff.md as "the natural
   first cartridge to port... small enough to reason about completely."
   **Result: plays identically, confirmed on hardware, no bugs found.**
   Wormfight itself is not yet ported.
3. ✅ Launcher enumerates `/games/*/meta.json` — installed games are now _data_, not code.
   `src/core/cartridge_store.*` scans the filesystem at boot and merges what it
   finds with the built-in registry into one list, so the launcher, score filing
   and `beam.highscore()` are unchanged. Format is documented in
   `beam-boy-hw/data/README.md`; `data/games/reflex/` is a working example.
4. ✅ **Sandboxing:** a per-call time budget (8 ms, half the 60 fps frame budget) and a VM memory
   ceiling (64 KB) so a buggy game can't hang the console or exhaust RAM — on overrun, the
   script call is aborted via a Berry exception and the console returns to the launcher.
   Built on Berry's existing observability hook (`be_set_obs_hook`), which fires periodically
   from inside the interpreter loop and on GC events, with no changes to vendored Berry source.
   See `src/vm/beam_api.h`/`.cpp` (`installSandbox`, `beginSandboxedCall`) and
   `src/vm/script_scene.cpp`.
5. ⏸️ **Deferred.** Dev quality-of-life: a `dev` build that pushes `game.be` over serial or HTTP
   and hot-reloads it, so iterating on a game takes seconds, not a flash cycle. Parked
   deliberately: `uploadfs` already covers occasional installs (single command, no firmware
   flash, source re-read on each launch), so this isn't worth building until iteration speed
   is actually painful in practice. Revisit later if that changes.

✅ _Visible result: write a game, push it, play it — no reflash._ \*_(Met via
`uploadfs`: a game can be added or edited without rebuilding firmware. Pushing
it over the air is item 5.)_

### Phase 7 — The store _(2 days)_

> _Goal: download games from the internet._

> **Update:** first firmware slice implemented. A **Store** utility scene connects
> using stored credentials, fetches a strict `index.json`, shows remote games as
> coloured blocks, downloads the selected `game.be`, checks size + SHA-256, writes
> `/games/<id>/{meta.json,game.be}`, rescans cartridges, and makes the install
> playable without rebooting. The store URL defaults to this repo's GitHub Pages
> index (`https://jakopako.github.io/beam-boy/games/index.json`) and remains
> configurable via `BEAMBOY_STORE_INDEX_URL`. See
> [`docs/phase-7-store.md`](docs/phase-7-store.md).

1. ✅ Publish a first `games/` index to GitHub Pages: `docs/games/index.json`
   plus `docs/games/reflex/game.be`. GitHub Pages still has to be enabled for
   the repo if `https://jakopako.github.io/beam-boy/` returns 404.
2. ✅ Firmware "Store" scene: fetch index → show available games as blocks → **A** downloads →
   SHA-256 verify → install → appears in the launcher. First slice uses a static working
   animation during the blocking download/write; this is acceptable while LittleFS writes
   make smooth animation unreliable anyway.
3. ✅ Show updates for installed games; allow deleting a game (hold **B** on it in the launcher).
4. ✅ Handle failure gracefully: no credentials/network, bad index, bad length, bad hash,
   full/unwritable flash all land in a red failure state with the exact reason on serial.

✅ _Visible result: your friend picks a game on the device and plays it 20 seconds later._

**Known issues to fix (reported after real-hardware use):**

- ✅ Launcher hold-B was overloaded (highscore + delete). Resolved: hold-B now
  only deletes an installed cartridge (with a red countdown), and holding the
  nav button/stick (rather than tapping it to launch) shows the selected
  game's highscore instead — instantly, not the bit-by-bit reveal used for a
  score just earned. See `docs/phase-3-launcher.md` and
  `docs/phase-7-store.md` for the updated control tables.
- ✅ After installing a game in the Store, exiting back to the launcher
  required pressing the joystick/nav button first and _then_ holding B — the
  same two-step gesture games use (pause, then hold-B-while-paused). Root
  cause: `GameList::build()` re-numbers Store/Network whenever an install adds
  a _new_ id, since they always sit after every installed cartridge; the
  engine tracked "what's currently running" by that numeric index, so after
  install it silently mistook the Store scene for the newly-inserted game and
  demanded the game exit gesture instead. Fixed by re-resolving the running
  scene's index by identity (`GameList::indexOf()`) right after the rebuild,
  in `StoreScene::installSelected()`. Plain hold-B now exits the Store
  immediately after an install, matching its own documented behaviour.

### Phase 8 — Power & Battery Management _(1–2 days)_

> _Goal: untether from USB, run safely on battery power, and manage energy in the OS._

> **Hardware-reality correction (post-planning):** the Adafruit Feather ESP32-S3
> No PSRAM has **no battery-sense ADC pin at all** — Adafruit's own docs are
> explicit that "there is no pin on the Feather ESP32-S3 that returns battery
> voltage." Instead it has an on-board **MAX17048 fuel gauge** on I2C (address
> `0x36`, shared with the STEMMA QT bus; SDA/SCL don't conflict with any
> existing button/stick/LED pin). This is a better situation than the
> originally planned ADC divider: the chip already linearises the LiPo
> discharge curve and reports voltage and state-of-charge percentage directly,
> so there's no divider ratio to calibrate and no discharge-curve lookup table
> to tune. The sections below describe what was actually built.

1. **Hardware & Sensing Layer (`src/core/power.*`):**
   - `Power` wraps Adafruit's `Adafruit_MAX1704X` library (I2C, `Wire`) and is gated entirely by `board::kHasBatteryMonitor` — `false` on the DevKitC and native (no chip, no I2C traffic attempted), `true` on the Feather.
   - Polls the gauge at most once a second; exposes `percent()`, `voltage()`, `charging()`, `level()`.
   - All the actual _decisions_ — is this worth warning about, is it worth shutting down for, is it worth staying awake for — live in `src/core/power_policy.h` as plain, host-testable functions over floats (`classifyPowerLevel`, `isChargingRate`, `shouldEnterIdleSleep`), following the same pure-logic/untestable-driver split already used for `net_policy.h`. 11 native tests cover the hysteresis and charging/idle-sleep decisions.
   - Charging is detected via the gauge's own `chargeRate()` (%/hr), with a small positive threshold (not `> 0`) so resting jitter never reads as charging.

2. **Battery Gauge & Status — launcher only:**
   - Holding **A + B together** for ~0.5 s in the launcher shows the battery gauge as a proportional bar (green → amber → red), for as long as it's held. This is deliberately **launcher-only**, not a global engine-level gesture, so no game ever has to reserve the combo for itself.
   - On a board with no fuel gauge (the DevKitC), the same gesture shows a dim, steady white pixel instead of a fake reading.
   - While charging, the bar breathes rather than holding steady (no icon to draw on a 1D display).
   - Adding a third gesture to two buttons that already had two turned the launcher's input handling into a small state machine, which is now arbitrated in one place (`src/scenes/launcher_gestures.h`) and covered by 17 native tests. It closes three ordering bugs, two of which destroyed user data: A pressed slightly before B launching a game instead of showing the gauge; releasing A first after the gauge deleting the selected cartridge; and holding B to exit a game (1.2 s) rolling straight on past the delete threshold (2.5 s). See [`docs/phase-8-power.md`](docs/phase-8-power.md#gesture-ordering).

3. **Persistent Low-Battery Warning Overlay:**
   - Two-sided hysteresis, not a single threshold: `kLowBatteryPercent = 15` / recovers at `20`, `kCriticalBatteryPercent = 5` / recovers at `10` — so a percentage dithering right at a boundary can't flicker the indicator on and off.
   - While `kLow`, a single pulsing red pixel at the last index is drawn every frame, in every scene (game, pause, launcher) — visible but not disruptive.
   - `kCritical` is not shown as an overlay at all; it immediately hands off to the shutdown sweep below instead.

4. **Safe Shutdown & Data Protection:**
   - The instant `updatePower()` classifies the level as `kCritical`, it pre-empts _everything_ — mid-game, mid-pause, mid-menu — before the pause/exit-gesture logic even runs.
   - `beginCriticalShutdown()` flushes storage (and the current game's score, if any) **before** a single frame of the shutdown animation plays, so the write is guaranteed to complete while power is still guaranteed, ahead of the hardware protection circuit's own abrupt cutoff.
   - A red sweep closing in from both ends plays for `kCriticalShutdownMs`, then the device configures `esp_sleep_enable_ext1_wakeup()` on the A/B/stick-press GPIOs (`ESP_EXT1_WAKEUP_ANY_LOW`, since they're `INPUT_PULLUP`) and calls `esp_deep_sleep_start()`.

5. **Charging Animation & Idle Sleep:**
   - **Charging visualizer**: while plugged in, a green sweep animates along the tube (`kChargingSweepMs` period) instead of the idle-sleep countdown — sleeping while charging would save nothing (USB is powering the device regardless) and only costs the feedback. Note that charging detection lags the board's own CHG LED by minutes: the MAX17048's charge-rate register is a filtered state-of-charge trend, not a current measurement. See [`docs/phase-8-power.md`](docs/phase-8-power.md) for why lowering the threshold to chase it is the wrong trade.
   - **Idle sleep**: after `kIdleSleepMs` (2 minutes) with no button held and no stick deflection past a small deadzone, the framebuffer fades out over `kIdleFadeMs` and the device enters the same deep sleep as a critical shutdown, waking on any button press. This check is re-derived every frame from the time since the last activity rather than latched, so any input — or plugging in USB mid-fade — falls out of the idle path on the very next frame with no extra state to unwind.

✅ _Visible result: fully cordless operation with clear charge feedback, an on-demand battery gauge in the launcher, low-battery warning during play, and zero risk of flash corruption when the battery runs out._

### Phase 9 — Enclosure _(2–4 days, iterative)_

> _Goal: a real object._

1. Design a handle/grip that holds the MCU, LiPo and controls, with the tube exiting
   through a strain-relieved gland. Consider a spine or channel supporting the 1 m tube.
2. **The USB-C port must be reachable without opening the case** — it is both the charge port
   and the recovery/flashing port. Model a chamfered cutout aligned to the board's connector;
   this is the single most common thing to get wrong on the first print.
3. Give the LiPo a snug pocket with no sharp edges or screw bosses against the pouch, and
   route wires so the cell is never compressed or flexed. Do not glue it in — it should be
   replaceable after a few hundred cycles.
4. Place the power slide switch so it can't be knocked accidentally in a bag.
5. Print, test the feel, iterate. Expect **three revisions** — mainly on button and stick placement
   and USB port alignment.

✅ _Visible result: Beam Boy v1._

### Phase 10 — Polish

A boot animation · a factory-reset gesture · brightness setting in the launcher (directly
trades runtime for visibility) · charge-cycle-friendly "storage mode" if left unused · a
`docs/making-games.md` so others can write cartridges · optional haptic motor (a click on hit
adds a lot for very little).

---

## 5. Suggested repo layout

```
beam-boy/
  beam-boy-hw/       PlatformIO firmware
    src/
      core/          display, input, storage, power
      scenes/        launcher, store, wifi setup, pause
      script/        VM binding + Beam API
      main.cpp
  beam-boy-games/    game cartridges (source of truth for the store)
    wormfight/
  beam-boy-store/    the GitHub Pages static site (index.json + game files)
  beam-boy-case/     3D models
  docs/
    beam-api.md      the API contract
    making-games.md
```

---

## 6. Timeline

| Phase               | Effort | Cumulative |
| ------------------- | ------ | ---------- |
| 0 Hardware bring-up | ½ d    | ½ d        |
| 1 Core engine       | 1–2 d  | 2½ d       |
| 2 First game        | 2–3 d  | 5½ d       |
| 3 Launcher          | 1–2 d  | 7½ d       |
| 4 WiFi + OTA        | 1–2 d  | 9½ d       |
| 5 VM bake-off       | 1–2 d  | 11½ d      |
| 6 Script games      | 2–3 d  | 14½ d      |
| 7 Store             | 2 d    | 16½ d      |
| 8 Power & Battery   | 1–2 d  | 18 d       |
| 9 Enclosure         | 2–4 d  | 21 d       |

**≈ 3 weeks of focused work**, with something playable from day 5.

---

## 7. Monetization

The static-index store handles **free** games perfectly. Paid games are where it gets
expensive, because payment fundamentally requires a server that knows who bought what.

| Option                                                                | Effort   | Assessment                                                                                                                  |
| --------------------------------------------------------------------- | -------- | --------------------------------------------------------------------------------------------------------------------------- |
| **Sell hardware kits / assembled units**                              | Low      | **Best ROI by far.** How TWANG, ESPboy and comparable projects actually monetize. The value you're selling is the _object_. |
| **Donations** (Ko-fi / GitHub Sponsors, credit in the boot animation) | Very low | Fits the DIY audience genuinely well.                                                                                       |
| **Paid cartridges** with signed licenses                              | High     | Needs store backend, accounts, per-device keypairs, signed+encrypted cartridges, and firmware that refuses unsigned code.   |
| Ads                                                                   | —        | No.                                                                                                                         |

**On paid cartridges specifically:** technically doable — the device generates a keypair, the
store issues a license signed against that device's public key, the cartridge ships encrypted.
But **DRM on an openly self-flashable device is defeatable**: anyone can build the firmware
themselves and remove the check. You'd invest weeks of infrastructure to protect revenue
likely smaller than selling a handful of kits.

**Recommendation: keep games free and open, sell hardware.** The growing free library is
precisely what makes the hardware worth buying — treat it as a product feature, not lost
revenue. Nothing here is a one-way door: because cartridges are already fetched by `id` from
an index and verified by hash, adding signature verification and a licensing server later is
an additive change.

### Community cartridges (your long-term goal)

Since you want to accept community games eventually, two things move from "nice" to
**mandatory**, and both are cheap if designed in from the start:

1. **Real sandboxing** (Phase 6.4) — memory cap and per-tick instruction limit, so a hostile
   or merely buggy cartridge can't brick or hang the console. This is the main reason the
   script-VM architecture beats native OTA cartridges for your goals.
2. **A submission process** — community games arrive as pull requests to the games repo. Review
   is manual, the index is regenerated by CI, and `git` gives you provenance and rollback for
   free. No moderation backend required.

---

## 8. Game ideas that suit a 1D display

- **Wormfight** — your monster shooter (Phase 2).
- **Wobble** — a TWANG-style dungeon crawler: move, attack, dodge hazard zones, level up.
- **Precision** — stop a sweeping dot inside a shrinking zone. Trivial to build, brutally addictive.
- **Pulse** — a rhythm game: pulses travel down the tube, hit **A** as they reach your end.
- **Snake 1D** — the tail occupies pixels behind you; eat, grow, don't get boxed in by hazards.
- **obstacles** - basically the dino jump game that you can play in Chrome when there is no internet connectivity, but viewed from above
- **fishing** - a moving bar, player has to make sure a dot stays within the bar. If dot outside of the bar for too long -> loose
- **rhythm** - light dots / lines move towards a target area of the player, which has to press everytime an item crosses their line. A bit like this light sabor game
- **Tug of War** — _(later)_ 2-player via ESP-NOW between two Beam Boys, one tube each.
- **Sonar** — a hidden target; the tube shows only "hotter/colder" as a glow intensity. A game
  the 1D format uniquely enables.
- **crossy road** (find different title) N-player game where each player's tube is a lane and a player
  is responsible for the 'chicken' when it is on his lane.

---

## 9. Decisions made

- **Single player** for v1. Two-player deferred to a possible ESP-NOW link between two
  Beam Boys — no design compromises taken now to enable it, but nothing blocks it either.
- **No sound.** (A haptic motor remains a cheap one-GPIO option if game feel needs it later.)
- **Community cartridges** are a long-term goal → sandboxing is a **hard requirement**
  (Phase 6.4), and games are submitted as PRs to the games repo.
- **Games free, hardware sold.** See §7.
- **Encoder + analog stick + 2 buttons.** Both input types, deliberately.
- **Binary score display** as a signature engine feature.
- **USB-C rechargeable**, via a board with integrated LiPo charging. No consumable batteries.
- **Power switch on Feather `EN` pin to `GND`.** Pulling `EN` low completely disables the 3.3V LDO regulator (<1 µA quiescent current) while the battery charger remains connected to USB-C and the LiPo cell for charging while powered off.
- **Button convention: A = primary/instant, B = hold-to-charge.** Established in Wormfight
  (Phase 2) after a push-back ability on B failed to justify occupying the only spare button.
  A quick stab of B should always do _something_ useful, so B is never a dead button. Games
  should follow this so muscle memory carries across cartridges.
- **Input state is read from button _levels_, not edges**, for anything that persists across
  frames (charging, held-to-view HUDs). An edge-driven hold sticks forever if a release edge
  is lost to debounce — this caused real bugs twice, in Phase 1 and again in Phase 2.
- **Pick saturated hues; the brightness cap eats subtlety.** A colour drawn at low intensity
  is scaled twice (draw intensity, then the global cap), so it can reach the LED as single-digit
  channel values. At that level there are almost no steps left to express a hue, and WS2812
  blue is perceptually stronger per unit than green — Reflex's `(0,180,120)` "green" zone read
  as teal on hardware (Phase 3). Reserve near-primary colours for anything drawn dim.
- **Round in write-once render paths; truncate in feedback paths.** `present()` and
  `addToPixel()` round, because the downward bias of truncation is visible exactly where
  precision is scarcest. `Color::scaled()` must _keep_ truncating: `fade()` applies it to its
  own output, and with rounding a dim pixel never reaches black.
- **Error paths must clean up as thoroughly as success paths.** Phase 4 review found the radio
  left on forever when a _connected_ session dropped, because only the connect-_timeout_ path
  powered it down. Any invariant worth having ("the radio is off unless asked for") has to hold
  on the paths nobody tests — losing signal, a rebooted router, a cancelled operation.
- **Register callbacks once, not per-use.** Arduino `WebServer` frees route handlers only in its
  destructor, so re-registering on each portal open leaks permanently in a long-lived object.
  The symptom appears somewhere unrelated — a TLS handshake failing for want of contiguous heap.
- **An operation that _stages_ a change is not finished until the change is applied.** A
  successful OTA that never reboots reports success, changes nothing, and offers itself again.
- **Engine-level input interception must be opt-in per scene type.** The engine grabs the nav
  button to guarantee a game can never trap the player. But the launcher marks _every_ entry as
  the "current game", so utility scenes had that button stolen too — and since the paused branch
  returns before `scene_->update()`, the Network menu could never be activated at all. It looked
  like broken WiFi; it was a swallowed button. Hence `GameEntry::is_game`: games get
  pause-then-exit, utility scenes get a direct hold-B exit and keep their own controls. Any
  future engine-level gesture needs the same question asked of it.
- **A gesture with no visible feedback is indistinguishable from a hung device.** Utility scenes
  have no pause overlay, so the hold-to-exit progress bar is drawn by the engine over whatever
  the scene rendered.
- **On a 1D display, an element too small to read as deliberate reads as a glitch.** The network
  menu drew unselected entries at a third width, which on a short strip became single dim pixels
  of three unrelated colours — reported, reasonably, as "randomly coloured LEDs". Use brightness
  and motion to signal selection, not size.
- **"How do I do X?" about a feature that exists is a bug report about discoverability.** Forget
  was already implemented, but silent on success and instant on a single press. A destructive
  action on a console with no text needs to arm-then-confirm, and every action needs to visibly
  acknowledge itself — on this display, "nothing happened" and "it worked" look identical unless
  deliberately made not to.
- **Erasing a setting must also stop whatever is using it.** Forget deleted the credentials file
  but left the radio associated to that very network until reboot.
- **A blocking call inside a request handler is worse than one in the main loop.** The rescan
  route originally scanned synchronously. On ESP8266 the scan yields via `esp_suspend()`, which
  resumes the loop continuation, which calls `tick()`, which re-enters `server_.handleClient()`
  _while one of its own handlers is still on the stack_. On ESP32 the same scan runs up to ~6.5 s
  under the task watchdog. Respond first, then do slow work from `tick()`.
- **Don't present a sampled result as an inventory.** A WiFi scan hears an AP only if a beacon
  lands in its dwell window, so a missing network is expected, not a defect. Scanning repeatedly
  and merging is not available either — each scan _replaces_ the driver's buffer. The fix is an
  honest label plus a one-tap retry, never a claim of completeness.
- **Cached counts describing memory you don't own go stale silently.** Starting a rescan frees
  the previous result buffer while `scan_count_` still holds the old value. The drivers
  bounds-check, so it was safe rather than a crash — but it rendered an empty dropdown that read
  as "scan found nothing". Decide on rendered output, not on the stale count.
- **Never call `sinf()` on an ever-growing argument.** Beyond a few hundred, newlib leaves its
  fast path for `__kernel_rem_pio2f`, which allocates a large local array — and the ESP8266's
  cont stack is ~4 KB, so it overflows and resets the device. Every animated scene had a phase
  that grows every frame, so every one was a latent instance. Use `wrappedSin()`/`pulse()` from
  `display.h`. This is also why `millis()/1000.0f * rate` is dangerous: it climbs to millions
  before rollover.
- **"It crashed after a few minutes" points at an accumulator, not at whatever you changed
  last.** The crash arrived while testing WiFi scanning, in a scene whose animation had simply
  been on screen longer than any before it. The scan was innocent. A decoded stack trace names
  the function; guessing from the feature under test does not.
- **On the ESP8266, "async" does not mean "does not suspend".** `WiFi.scanNetworks(async)`
  calls `esp_yield()` before returning — it suspends the loop continuation and hands control
  to the SDK, so the call does not return until arbitrary driver code has run, and it
  reconfigures the radio while it does. Whatever the caller had not finished yet stays
  unfinished across that gap. Called partway through `startPortal()`, it left the device in
  the SDK with a half-built portal (no AP, no DNS, wrong state) and crashed as soon as a phone
  associated. Called from an HTTP handler, it suspends with `server_.handleClient()` beneath
  it. **Rule: any SDK call that might suspend is requested by setting a flag and issued from a
  single known-safe point at the end of `tick()`, never inline.**
- **Freeing a result buffer is not cancelling the operation that fills it.** `WiFi.scanDelete()`
  releases the completed scan buffer but leaves an in-flight scan running, and the SDK's
  completion callback runs in _SDK context_ and writes to driver-owned state. Tearing the
  soft-AP down and switching to STA with a scan outstanding crashed inside `hostap_input`
  (`ctx: sys`) almost every time the user connected. **Rule: an uncancellable SDK callback is a
  constraint on teardown ordering — wait for it before reconfiguring the radio underneath it.**
- **Peak heap matters while a radio is receiving.** The soft-AP allocates a pbuf per frame, and
  it cannot wait. Building an HTML fragment into its own `String` before appending it to the
  page doubled peak usage for the length of the build. Stream long responses with
  `setContentLength(CONTENT_LENGTH_UNKNOWN)` rather than materialising them, and remember that
  growing a `String` holds both buffers at once and fragments the heap behind it.
- **A crash address that moves between unrelated allocating functions means out of memory, not
  a bug at either address.** The ESP8266 core mostly does not check allocation failure, so OOM
  presents as a fault inside whichever function was allocating. **Rule: when the PC wanders,
  stop reading backtraces and measure the heap — the largest contiguous block and
  fragmentation, not just the free total.** ⚠️ In this project that measurement _disproved_ the
  OOM theory (39 KB free, 3 % fragmented at the moment of the crash) and pointed at RF timing
  instead. That is the rule working, not failing.
- **Instrument the resource before changing the code.** Three crashes here were confidently
  attributed to three different causes; only the last was supported by measurement. **A decoded
  address is a hypothesis, not a diagnosis** — especially when it lands in a subsystem you did
  not write. Add the counter, get the number, _then_ edit.
- **Peripherals contend with the radio even when they are "interrupt-safe".** WS2812 DMA output
  every frame starved the ESP8266 PHY into faulting inside its own timing callbacks. Anything
  driven continuously at frame rate should back off while the radio is doing timing-critical
  work, and should skip rather than wait when the peripheral is busy — `NeoPixelBus::Update()`
  spins on `yield()`, which runs SDK work from inside the render path.
- **Test the invariant the fix relies on, not the crash.** None of the three hardware crashes is
  reproducible on a host — a stack overflow in newlib, driver re-entrancy, and an SDK callback
  outliving a radio reconfiguration. But the `sinf` crash had a precondition that _is_ testable: the phase stays
  small. `test/test_soak` asserts exactly that over 6 simulated hours. See `test/README.md`.
- **Float precision, not just stack depth, bounds an animation phase.** Past ~1e5 radians a
  `float`'s steps are coarser than a smooth animation needs, so `wrappedSin()` keeps the device
  alive but the motion stutters regardless. Scenes must wrap their own phase; the helper is a
  safety net, not a licence for an unbounded accumulator.
- **A native-USB board is two different USB devices, and the tools cannot tell them apart for
  you.** The Feather S3 enumerates as `239A:8113` (Adafruit TinyUSB CDC) while the firmware
  runs and as `303A:1001` (Espressif USB-Serial-JTAG) in ROM download mode — different COM
  numbers, only ever one present at a time. Two failures follow, and neither looks like what it
  is. Auto-detection can attach the monitor to the _bootloader's_ port, which prints nothing at
  all, forever; and esptool's closing `Hard resetting via RTS pin...` is a no-op, because the
  USB-C socket goes straight to the S3's USB pins and no RTS line reaches `EN`. The board is
  then left sitting in the ROM bootloader — the launcher is dead and the stick does nothing, so
  it reads as a firmware hang when the firmware is simply not running. **Tap RESET after
  flashing, and pin `monitor_port` by USB id, not by COM number.**
- **Don't "fix" a flashing problem by silencing the lines the USB stack depends on.** Setting
  `monitor_dtr = 0` looks like a way to stop the monitor holding the port; on native-USB CDC it
  means `Serial` never goes true, so the firmware prints into a void. Adding
  `--after=hard_reset` looks like insurance against a missed reset; on a board with no RTS
  wired to `EN` it changes nothing except the log line that misleads you. Both were added here
  against an intermittent upload failure whose real cause was Windows USB re-enumeration
  timing, and both made the symptoms worse while appearing to address them.
- **A board's stock partition table is part of its API, and a data partition's _subtype_ is
  load-bearing.** The Feather's `partitions-8MB-tinyuf2.csv` declares its only data partition
  as `ffat`/`fat`; Arduino's `LittleFS.begin()` defaults to `partitionLabel="spiffs"` and looks
  up a DATA/SPIFFS partition, so the mount failed on every boot. Everything downstream of
  storage then failed in a way that named something else entirely: `Storage: UNAVAILABLE` in
  the banner, and the captive portal answering "Could not save -- check the name is 32
  characters or fewer" for a perfectly valid SSID, because `saveCredentials()` cannot
  distinguish a rejected name from `LittleFS.open()` returning false. Worse, `uploadfs`
  reported success the whole time: PlatformIO's uploader targets the data partition by
  _offset_ and does not care about its subtype, so it wrote a valid LittleFS image into a
  partition the firmware could never open. The DevKitC never showed this only because its env
  pins `default_16MB.csv`, which happens to have a real `spiffs` partition. Fixed with
  `partitions-8MB-littlefs.csv`. **A successful `uploadfs` is not evidence the firmware can
  mount what it wrote**, and the partition table itself is written by a _firmware_ upload, so
  changing it requires flashing firmware before filesystem.

### Testing

Host tests run with `pio test -e native` in about 13 seconds and need no board. They cover the
logic that is a function of time and input — debounce, auto-repeat, fades, colour maths,
long-run accumulators — because `hostshim/` makes the clock and the pins variables the test
sets. They deliberately do **not** cover WiFi, OTA or LED timing; those are hardware behaviour
and are verified with the checklist in `docs/phase-4-wifi.md`.

**When hardware logic resists testing, split the decision out from the driver call.** The
credential-retention rule is the worked example: `Network` cannot be compiled on the host
without shimming the whole WiFi API, but the part worth testing was never the driver — it was
the two-input question "given this outcome, do we keep the credentials?". Moving that into
`net_policy.h` as a free `constexpr` function made the entire truth table testable in
microseconds, while the untestable part shrank to a single `if`. Prefer this over either
shimming a large driver API or leaving the rule uncovered.

### Storing credentials that failed

Discard only on a **definite rejection** (`kBadPassword`) **and** only while the credentials
are **unproven** — never tested successfully. A mistyped password otherwise strands the user
on a Connect entry that can never work; but discarding on _every_ failure would erase a good
configuration whenever the router reboots or the console is carried out of range. `kNotFound`
is deliberately kept, because a typo'd SSID is indistinguishable from being out of range and
out of range is far more common. `unproven_` is never persisted: anything that survived a
reboot is treated as proven.

**⚠️ Never derive an authentication verdict from `wl_status_t`.** Value 6 is `WL_WRONG_PASSWORD`
on ESP8266 and `WL_DISCONNECTED` on ESP32; `WL_CONNECT_FAILED` is not an auth verdict on either
core (on ESP32 it also covers an AP at capacity); and on ESP32 the status is an event-updated
cache that can still describe the _previous_ attempt right after `WiFi.begin()`. Use the
**disconnect reason**, captured in an event handler. This is the fourth silent divergence
between the two cores; **always build both targets after a WiFi change.**

**Correlate driver events to an attempt with a generation counter, not a boolean.** ESP32
queues events to a separate task, so a failure produced while tearing down one attempt can be
delivered _after_ the next has begun. A "attempt in progress" flag attributes it to the new
attempt — which, for credential handling, means erasing the password the user just corrected.
Stamp each event with the generation current when it arrived and accept only matching ones.
Anything shared with an event handler must be `std::atomic`; on ESP32 the handler and `tick()`
run on different tasks.

**Let the driver finish its own retries.** The ESP32 core retries the first disconnect
internally for every reason, so acting on the first failure event cuts short a retry that might
have succeeded. Read the verdict at the deadline instead.

**Only claim state changed if it actually changed.** If `LittleFS.remove()` fails, keep the
credentials in RAM rather than clearing them: a UI that says "forgotten" while the file
survives is worse than one that admits failure, because the next scene entry reloads it and
the network reappears looking proven. Serial logging is not a substitute — it is invisible on
the handheld.

### Still open

1. **Case format** — do you want the tube rigid along a spine (sword-like, TWANG-ish) or
   flexible/coilable with only the grip printed? This changes the Phase 8 model substantially,
   but you can decide after Phase 2 when you know how you hold it while playing.
2. **Brightness vs. runtime** — worth measuring actual draw in Phase 0 and picking a default
   cap you're happy with.
3. **Wormfight difficulty tuning** — deferred until the 50 px tube arrives. The 10 px
   prototype strip is too short to judge pacing; see the "Still to tune" section of
   `docs/phase-2-wormfight.md` for the specific constants involved.

---

### References

- [bdring/TWANG](https://github.com/bdring/TWANG) — open-source 1D LED dungeon crawler; source of the abstract-coordinate-space pattern.
- [tzapu/WiFiManager](https://github.com/tzapu/WiFiManager) — captive-portal provisioning.
- [Makuna/NeoPixelBus](https://github.com/Makuna/NeoPixelBus) — RMT/DMA LED output.
- [ESP-IDF partition tables & OTA](https://docs.espressif.com/projects/esp-idf/en/latest/esp32/api-guides/partition-tables.html)
- [Adafruit NeoPixel Überguide](https://learn.adafruit.com/adafruit-neopixel-uberguide) — 60 mA/pixel power figure, best practices.
- [Berry language](https://github.com/berry-lang/berry) — embedded scripting VM.
- [improv-wifi](https://www.improv-wifi.com/) — BLE provisioning alternative.
