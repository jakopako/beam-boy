# Beam Boy — Feasibility & Implementation Plan

A minimalist handheld game console with a **one-dimensional display**: a 1 m addressable
LED neon tube (50 px), two buttons, one scroll wheel, WiFi, and downloadable games.

---

## 1. Verdict: does the idea make sense?

**Yes — and there is proven prior art.** The concept is a handheld descendant of
*Line Wobbler* (Robin Baumgarten) and its open-source homage
[**TWANG**](https://github.com/bdring/TWANG), a 1D dungeon crawler running on an
addressable LED strip. TWANG has been built and played on strips of 60, 144, 288 and
450 LEDs, so **50 px is a proven, playable resolution** — at the short end, which means
game design should favour *timing and reflexes* over *spatial detail*.

The one genuinely novel part of Beam Boy is the **downloadable game framework**. Nothing
off-the-shelf does this for LED-strip games, so it is the part of the project that needs
the most deliberate design — which is exactly what this plan front-loads.

### Key findings from research

| Topic | Finding | Consequence for Beam Boy |
|---|---|---|
| **MCU** | On ESP8266, the standard NeoPixel driver disables interrupts for the whole strip write, which starves the WiFi stack (flicker, dropped packets, watchdog resets). ESP32 has the **RMT peripheral**, which generates WS2812 timing *in hardware* with zero CPU blocking, and a second core for the radio. | **Move off the NodeMCU to an ESP32.** Confirmed with you. |
| **Downloadable games** | Full-firmware OTA replaces the entire ~1 MB image: one game resident at a time, ~1 MB per switch. A scripting VM lets each game be a few-KB file in the flash filesystem, with dozens resident. | **Native engine + script "cartridges."** Confirmed with you. |
| **Which VM** | MicroPython/Espruino are too heavy to drive a 60 fps loop. **Berry** (the Tasmota scripting language) and **Lua** are lightweight bytecode VMs designed for this class of device. wasm3 is fast and sandboxed but is in "minimal maintenance." | Start with **Berry** as the front-runner, but **prototype-benchmark it before committing** (Phase 5 has an explicit bake-off). **Update: research confirms Berry is ESP32-only** — it cannot run on the ESP8266, so the bake-off happens on the S3 Feather. |
| **Frame budget** | 50 px × 24 bits × 1.25 µs ≈ **1.5 ms per frame** on the wire — trivial. At 60 fps that is 9 % of the time budget, all handled by RMT hardware. | Rendering is a non-issue. The VM has ~15 ms/frame of headroom. |
| **WiFi setup** | `WiFiManager` (captive portal) is the most battle-tested and needs no companion app, but phone captive-portal auto-popup is inconsistent and Android may drop an AP it deems internet-less. `improv-wifi` (BLE) is purpose-built for screenless devices but has no iOS web support. | **WiFiManager**, with the fallback "open `192.168.4.1` manually" documented. It also fits your "offline must work" requirement naturally. |
| **Power** | **Measured** (10 px @ cap 25/255): 39 mA full white, 9.5 mA for a realistic game frame → ~3.9 mA/px. Projected to 50 px: **~195 mA full white, ~33 mA in normal play**. | With the MCU at ~35 mA, normal play is **~70 mA** → **30+ hours** from a 2500 mAh cell. Power is a non-issue; the brightness cap was raised from 25 to **64** and can go higher. |
| **Charging** | A bare TP4056 has no load sharing: playing while plugged in draws through the battery, confusing end-of-charge detection and wasting cycles. A DevKitC + TP4056 build also ends up with two USB ports. | **Use a board with integrated LiPo charging** (Adafruit Feather ESP32-S3): one USB-C port for charge *and* flash, correct load sharing, battery sense pre-wired. See §2.1. |
| **LiPo direct drive** | WS2812B tolerates ~3.5–5.3 V. Driving the strip straight off the LiPo (3.7–4.2 V) avoids both a boost converter *and* the 3.3 V→5 V data level shifter, since VCC and logic level then nearly match. | **Skip the boost converter and the level shifter.** The cell's protection cutoff (~3.4 V) keeps the strip in range; firmware shuts down cleanly before it trips. Verify on your specific tube in Phase 0. |
| **Input** | TWANG's spring-doorstop + MPU6050 controller is genre-defining but built for a floor-standing cabinet. Its feel depends on *analog* input — an encoder cannot express "move slowly left". | **EC11 encoder + analog thumbstick + 2 buttons.** Encoder = relative/detented (menus, precise steps); stick = absolute/self-centering (velocity). Both, for €2 extra and much wider game range. |
| **Monetization** | Paid cartridges need a backend, accounts, per-device keys and signed+encrypted code — and DRM on an openly self-flashable device is defeatable by rebuilding the firmware. Comparable projects (TWANG, ESPboy) monetize via hardware. | **Keep games free and open; sell hardware kits.** The free library is what makes the hardware worth buying. Store design leaves the door open for paid games later. |

### Risks, honestly

1. **The scripting VM is the make-or-break unknown.** If Berry can't hold 60 fps, the fallback
   is a "data-driven engine" (games are declarative level/behaviour descriptions interpreted
   by native C++ code) — less flexible but guaranteed fast. Phase 5 decides this with a benchmark, early.
   **Update — measured, and the risk is retired.** On the ESP8266 (the pessimistic board,
   no FPU) at 100 entities the whole frame costs 11 % of budget. ~84 % of that is *draw*
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

| Part | Choice | Notes |
|---|---|---|
| MCU | **Adafruit Feather ESP32-S3 (8 MB flash, 2 MB PSRAM)** — *recommended* | ~€18. **Has LiPo charging and a JST battery connector built in**, sharing the same USB-C port used for flashing: one port for everything, proper load sharing, and a battery voltage divider already wired. Solves the charging design in one part. |
| *MCU alternative* | **ESP32-S3-DevKitC-1 (N16R8)** + separate TP4056 USB-C charger | ~€10 + €2. Cheaper and more flash, but you must solve charging yourself — see §2.1. |
| Display | Your **WS2812B silicone neon tube, 50 px / 1 m, IP67** | Already owned. |
| Wheel | **EC11 rotary encoder with integrated push switch** | The push doubles as a button — this *is* your "reuse buttons" principle. |
| Stick | **2-axis analog thumbstick with push switch** (PS2-style module) | ~€2. Gives absolute + velocity control the encoder can't. Y axis is spare on a 1D display — that's deliberate headroom for future games. |
| Buttons | **2 × 6 mm tactile switches** | Named **A** (action/confirm) and **B** (back/cancel). |
| Battery | **LiPo pouch cell, 2000–2500 mAh, with JST-PH connector and built-in protection** | Rechargeable — the user never buys a battery. A pouch cell fits a flat handheld grip far better than a cylindrical 18650. Must include a protection circuit (most pouch cells with a JST lead do). |
| Power switch | Slide switch in the battery line | Cuts battery to everything. Charging still works with it off. |
| Misc | JST connector for the tube, 470 µF cap across strip power, 330 Ω resistor in the data line | Standard NeoPixel hygiene — the cap absorbs inrush, the resistor tames data ringing. |

**Total: roughly €35–45** on top of what you own (Feather route), or €27–37 with the
DevKitC + separate charger.

### 2.1 Charging & power management

The device must be **rechargeable over USB-C** — no consumable batteries, ever. Three things
have to be right, and the naive build gets all three wrong:

**1. One USB port, not two.** A DevKitC + TP4056 build ends up with *two* USB ports: one to
charge, one to flash. That's confusing for users and awkward to lay out in a case. The Feather
route collapses them into one connector.

**2. Load sharing.** ⚠️ A bare TP4056 wires the load directly across the battery terminals, so
playing while plugged in draws current *through* the battery. This confuses the charger's
end-of-charge detection and causes needless charge cycles that shorten the cell's life. Proper
load sharing powers the system from USB and charges the battery separately when both are
present. The Feather's charger does this correctly; if you go the DevKitC route, buy a
**TP4056 module with load-sharing / "ideal diode" output** (sometimes sold as TP4056 + DW01 +
FS8205 with separate `OUT+`/`OUT-` pads), and power the system from `OUT`, never from the
battery pads directly.

**3. Low-voltage cutoff.** The cell's protection circuit must cut off around **3.0–3.5 V**.
This protects the battery *and* keeps the WS2812 tube inside its ~3.5 V minimum operating
range — below that the LEDs start to misbehave before the battery is actually flat. Firmware
should warn well before this point (see below).

**Battery life.** Measured in Phase 0 on a 10 px strip and projected to the 50 px tube:
a realistic game frame draws **~33 mA** from the LEDs, plus ~35 mA for the ESP32-S3 with the
radio off — about **70 mA** in normal play. A 2500 mAh cell therefore gives **30+ hours**,
with a worst-case full-white frame at ~195 mA still only reaching ~4 hours. Recharge is
~2–3 h at 500 mA.

This is comfortable enough that **brightness is a look-and-feel decision, not a power
constraint** — the cap exists to bound the worst case, not to ration the battery.

**Firmware side (add to Phase 1 / Phase 9):**

- Read battery voltage on an **ADC1** pin via a 2:1 divider. The Feather has this pre-wired
  (check its pinout for the battery-sense pin); otherwise add two 100 kΩ resistors.
- Convert voltage → rough percentage with a LiPo discharge curve, not a linear map — LiPo
  voltage sits near 3.7 V for most of its discharge and then falls off a cliff.
- **Battery meter in the launcher:** hold **B** in the menu to render charge as a bar along
  the tube, green → amber → red. Zero extra hardware, and it's a natural use of a 1D display.
- **Low-battery warning:** below ~3.5 V, pulse the first pixel red once every few seconds
  during play — visible but not disruptive.
- **Critical cutoff:** below ~3.4 V, save state, show a red sweep, and deep-sleep before the
  protection circuit cuts out mid-game.
- **Charging indicator:** while charging, animate a slow filling green sweep along the tube;
  solid green when full. The tube *is* the status LED — no extra indicator needed, which
  suits the minimalist brief.
- **Idle sleep:** after ~2 minutes with no input, fade out and deep-sleep; wake on a button
  press. This is the single biggest real-world battery win.

### Interim: what you can build on the NodeMCU today

Phases 1–3 (≈5 days of work: core engine, first game, launcher) are **fully doable on your
existing ESP8266** while the ESP32 ships. Two rules make the port a config change rather than
a rewrite:

1. **Use `NeoPixelBus` from the very first line of code**, with `NeoEsp8266DmaWs2812xMethod`
   on the ESP8266 and `NeoEsp32RmtNWs2812xMethod` on the S3. Same API, same library — only
   the method typedef differs. Do *not* start on `Adafruit_NeoPixel`; its bit-banged output
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
can't be tested until the S3 arrives (build against the encoder + buttons first, add stick
support in Phase 2 on real hardware), and it is short on usable GPIOs — you may need to drop
a button temporarily. Phases 4+ (WiFi, OTA, scripting VM, store) should wait for the ESP32.

### Pin map (ESP32-S3, starting point)

| Signal | GPIO | Notes |
|---|---|---|
| LED data | 4 | Must be RMT-capable. Via 330 Ω. |
| Encoder A / B | 5 / 6 | Quadrature, interrupt-driven. |
| Encoder push | 7 | `INPUT_PULLUP` |
| Button A | 8 | `INPUT_PULLUP` |
| Button B | 9 | `INPUT_PULLUP` |
| Stick X / Y | 10 / 11 | ADC1 channels — ADC2 is unusable while WiFi is active on ESP32. |
| Stick push | 12 | `INPUT_PULLUP` |
| Battery sense | 13 (ADC1) | Via 2:1 divider — lets you show a battery warning *on the tube*. |

⚠️ Keep every analog input on **ADC1**. ADC2 is shared with the WiFi radio and reads garbage
whenever WiFi is on — a classic ESP32 trap that would silently break the stick in Phase 4.

### The minimalist control scheme

Four physical inputs cover everything by reusing them contextually:

- **Stick** — analog movement / aim / velocity control.
- **Wheel** — precise stepping; scrolls the launcher menu.
- **A** — primary action; in the menu, "select".
- **A** — primary action; in the menu, "select".
- **B** — secondary action; in the menu, "back".
- **Wheel press** — pause. From pause: return to the launcher.
- **Stick press** — free for games to use.
- **Hold B during power-on** — enter WiFi setup (starts the captive portal). This is your
  explicit user-consent gate: **the device never touches the network unless asked**, so
  fully-offline operation is the default.

Games declare which controls they use in `meta.json`, so the launcher can hint at the scheme
before starting — and a game that only needs the wheel still works if you later build a
stick-less variant.

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
beam.present()

beam.wheel()                             -- accumulated detents since last call (+/-)
beam.stick()                             -- analog X, deadzoned + calibrated, -1..1
beam.stick_y()                           -- spare axis: charge, weapon select, dodge...
beam.pressed(BTN_A)                      -- edge
beam.held(BTN_B)                         -- level

beam.time()                              -- ms since game start
beam.random(n)
beam.save(key, value) / beam.load(key)   -- highscores, persisted per game
beam.show_score(n)                       -- engine-drawn binary score animation
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
{ "api_version": 1,
  "games": [ { "id": "wormfight", "name": "Wormfight", "version": "1.2",
               "url": "https://.../wormfight/game.be", "sha256": "..." } ] }
```

The device fetches the index, shows the list on the tube, downloads the chosen script,
verifies the hash, writes it to LittleFS. **No server to run, no backend to maintain**, and
publishing a new game is a `git push`. Firmware updates use ordinary HTTPS OTA on top of this.

---

## 4. Step-by-step implementation plan

Every phase ends with **something you can see or play**.

### Phase 0 — Hardware bring-up *(½ day — start today on the ESP8266)*
> *Goal: the tube lights up, on battery.*

1. Order the MCU (**Adafruit Feather ESP32-S3** recommended — see §2.1 for why charging drives
   this choice), EC11 encoder, thumbstick module, buttons, and a **2000–2500 mAh LiPo with
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

✅ *Visible result: a glowing 1 m tube running off a rechargeable battery, charging over USB-C.*

### Phase 1 — Core engine *(1–2 days — ESP8266 is fine)*
> *Goal: the foundation everything else stands on.*

1. `Display` class: float 0..1 coordinate space, **sub-pixel anti-aliased** `pixel()`,
   `fade()`, global brightness cap (start at 25/255), `present()`.
2. `Input` class: interrupt-driven encoder with detent decoding, debounced buttons,
   `pressed()` / `held()` / `released()` edges. Stub the stick behind the same interface —
   the ESP8266's single ADC can't drive it, so wire it up in Phase 2 on the S3.
3. Fixed-timestep game loop at 60 fps with a frame-time budget assert.

   **Sanctioned exception:** the frame gate assumes the frame loop is the only thing with a
   deadline. That is true for games and false for the WiFi stack, whose deadlines are enforced
   in the SDK — a missed one is a fault in the PHY, not a dropped frame. `Scene::idle()` +
   `Engine::setIdleServiced()` let a scene be serviced on the frames the engine *skips*. This
   permits being called **more often**, never blocking, and is opt-in per scene (cleared on
   every scene change). Games are unaffected and still see a fixed timestep.
4. `beam.show_score()`: the binary score readout, animated bit-by-bit.
5. A `demo` scene: a wheel-controlled anti-aliased dot with a fading trail.

✅ *Visible result: a smooth, glowing dot you steer with the wheel. This is the first moment the device feels real — the anti-aliasing is the "wow".*

### Phase 2 — First real game, native *(2–3 days)*
> *Goal: a genuinely fun game, written in C++ against the engine API.*

Port your `main.cpp` monster-shooter into the engine as **"Wormfight"** and deepen it:

- **Stick** moves the shooter (analog — you can creep or dash); **A** fires; **B** is a
  short cooldown-limited "push back". Wheel adjusts aim/power.
- Waves of monsters with rising speed; multiple monsters at once.
- Life system rendered as a few pixels at your end; screen-shake / white-flash on hit.
- Death animation, then `beam.show_score()` in binary, restart with **A**.

Once the S3 arrives, wire up the thumbstick here and tune the deadzone and response curve —
analog feel is worth spending real time on, since it defines how the console plays.

Write it against the *exact* API shape you intend to expose to scripts — so the port to
a script in Phase 6 is mechanical.

✅ *Visible result: a game you actually want to hand to someone. Get feedback here before building any infrastructure.*

### Phase 3 — Launcher & persistence *(1–2 days)*
> *Goal: more than one thing on the device.*

1. ✅ Mount **LittleFS**; store settings and per-game highscores in NVS/LittleFS.
2. ✅ A **launcher scene**: each installed game is a colored block on the tube; the wheel scrolls,
   the selected one pulses, **A** launches. Its accent color comes from `meta.json`.
3. ✅ Wheel-press during a game → pause → hold **B** → back to launcher.
4. ✅ Add a second, tiny native game (e.g. a reflex "stop the dot in the zone" game) so the
   launcher has something to choose *between*.
5. ⏸ **Power management** (needs the ESP32; see §2.1): battery voltage sensing on ADC1 with a
   LiPo discharge curve, a hold-**B** battery meter in the launcher, the low-battery pulse,
   the critical-voltage safe shutdown, the charging sweep animation, and idle deep-sleep with
   button wake. *Deferred until the Feather arrives.*

**Navigation without the wheel.** The encoder had not arrived, so `Input` gained `navDelta()`:
discrete steps, synthesized from the joystick today (threshold + hysteresis + auto-repeat),
read from quadrature once the encoder is fitted. Only `Input::updateNav()` changes — menus never
talk to a specific input device. The missing hardware forced an abstraction worth having anyway.

**Exit is gated behind pause**, not a bare hold-B: Wormfight already holds B for up to 1.1 s to
charge, and future cartridges will collide the same way. The engine handles pause/exit *before*
the scene updates, so no game — including a future community cartridge — can trap the player.

✅ *Visible result: switch between two games without reflashing, and see your battery level. It's a console now.*

### Phase 4 — WiFi, opt-in *(1–2 days)*
> *Goal: online, but only when you say so.*

**Two independent update paths — don't confuse them:**

| | Games (Phases 6–7) | **Firmware OTA** (step 4 below) |
|---|---|---|
| Updates | Cartridge scripts + metadata | The C++ engine: renderer, input, launcher, network code |
| Written to | LittleFS data partition | The **app partition** (executable) |
| Reboot | No | Yes |
| Failure risk | One broken game | **A bricked console** |

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
2. ✅ Show provisioning state *on the tube*: portal-active = slow amber pulse; connecting =
   blue sweep; connected = green flash; failed = red flash. Each state has a distinct
   *motion* as well as a colour, since hue quantises badly when dim and red/green alone
   excludes colourblind players.
3. ✅ Verify offline behaviour is untouched: no saved credentials ⇒ never scans, never blocks,
   boots straight into the launcher.
4. ✅ Add **firmware OTA** ("Update" from the Network scene) with a progress bar drawn on the
   tube. ⚠️ Uses `setInsecure()` — **image signing is required before any real release**; see
   [`docs/phase-4-wifi.md`](docs/phase-4-wifi.md).

✅ *Visible result: configure WiFi from your phone with no display, and push firmware updates over the air.*

*Four bugs were caught in review; five more crashes were found on hardware. The last of
those was confirmed fixed on ESP32-S3 hardware and left unfixed on the ESP8266 by
design. See [`docs/phase-4-wifi.md`](docs/phase-4-wifi.md).*

### Phase 5 — VM bake-off ⚠️ *(1–2 days — do this before Phase 6)*
> *Goal: prove the scripting model before betting the architecture on it.*

> **Update:** Berry only needs an ESP32 core, not the Feather's charging
> circuit, so this ran on the `esp32-s3-devkitc-1-n16r8` dev board rather than
> waiting for the Feather. See
> [`docs/phase-5-vm-bakeoff.md`](docs/phase-5-vm-bakeoff.md) for the vendoring
> notes, the real hardware measurements (per-entity and batched), and the
> resulting cartridge-API design constraint.

1. ✅ Build the **native baseline** (`BenchScene`) — runs on the ESP8266 today, no
   Feather needed. Sweeps 10/25/50/100 entities and prints, as CSV, the frame
   cost split into update vs. draw plus **`update_headroom`**: how many times
   slower than native the *interpreted* half may be and still hold 60 fps. That
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
3. ✅ Reimplement `runWorkload()` — and *only* that function — as a script. The tight
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
   waves and rules, interpreted natively). *mJS was rejected on expressiveness —
   no closures or classes makes for a poor cartridge language.*

✅ *Visible result: a hard number that de-risks the whole rest of the project. Don't skip it.*

### Phase 6 — Games as scripts *(2–3 days)*
> *Goal: the cartridge model, working locally.*

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
3. ✅ Launcher enumerates `/games/*/meta.json` — installed games are now *data*, not code.
   `src/core/cartridge_store.*` scans the filesystem at boot and merges what it
   finds with the built-in registry into one list, so the launcher, score filing
   and `beam.highscore()` are unchanged. Format is documented in
   `beam-boy-hw/data/README.md`; `data/games/reflexfs/` is a working example.
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

✅ *Visible result: write a game, push it, play it — no reflash.* **(Met via
`uploadfs`: a game can be added or edited without rebuilding firmware. Pushing
it over the air is item 5.)*

### Phase 7 — The store *(2 days)*
> *Goal: download games from the internet.*

> **Update:** first firmware slice implemented. A **Store** utility scene connects
> using stored credentials, fetches a strict `index.json`, shows remote games as
> coloured blocks, downloads the selected `game.be`, checks size + SHA-256, writes
> `/games/<id>/{meta.json,game.be}`, rescans cartridges, and makes the install
> playable without rebooting. The store URL defaults to this repo's GitHub Pages
> index (`https://jakopako.github.io/beam-boy/games/index.json`) and remains
> configurable via `BEAMBOY_STORE_INDEX_URL`. See
> [`docs/phase-7-store.md`](docs/phase-7-store.md).

1. ✅ Publish a first `games/` index to GitHub Pages: `docs/games/index.json`
   plus `docs/games/reflexfs/game.be`. GitHub Pages still has to be enabled for
   the repo if `https://jakopako.github.io/beam-boy/` returns 404.
2. ✅ Firmware "Store" scene: fetch index → show available games as blocks → **A** downloads →
   SHA-256 verify → install → appears in the launcher. First slice uses a static working
   animation during the blocking download/write; this is acceptable while LittleFS writes
   make smooth animation unreliable anyway.
3. ⬜ Show updates for installed games; allow deleting a game (hold **B** on it in the launcher).
4. ✅ Handle failure gracefully: no credentials/network, bad index, bad length, bad hash,
   full/unwritable flash all land in a red failure state with the exact reason on serial.

✅ *Visible result: your friend picks a game on the device and plays it 20 seconds later.*

### Phase 8 — Enclosure *(2–4 days, iterative)*
> *Goal: a real object.*

1. Design a handle/grip that holds the MCU, LiPo and controls, with the tube exiting
   through a strain-relieved gland. Consider a spine or channel supporting the 1 m tube.
2. **The USB-C port must be reachable without opening the case** — it is both the charge port
   and the recovery/flashing port. Model a chamfered cutout aligned to the board's connector;
   this is the single most common thing to get wrong on the first print.
3. Give the LiPo a snug pocket with no sharp edges or screw bosses against the pouch, and
   route wires so the cell is never compressed or flexed. Do not glue it in — it should be
   replaceable after a few hundred cycles.
4. Place the power slide switch so it can't be knocked accidentally in a bag.
5. Print, test the feel, iterate. Expect **three revisions** — mainly on button placement,
   wheel reachability and USB port alignment.

✅ *Visible result: Beam Boy v1.*

### Phase 9 — Polish
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

| Phase | Effort | Cumulative |
|---|---|---|
| 0 Hardware bring-up | ½ d | ½ d |
| 1 Core engine | 1–2 d | 2½ d |
| 2 First game | 2–3 d | 5½ d |
| 3 Launcher | 1–2 d | 7½ d |
| 4 WiFi + OTA | 1–2 d | 9½ d |
| 5 VM bake-off | 1–2 d | 11½ d |
| 6 Script games | 2–3 d | 14½ d |
| 7 Store | 2 d | 16½ d |
| 8 Enclosure | 2–4 d | 20½ d |

**≈ 3 weeks of focused work**, with something playable from day 5.

---

## 7. Monetization

The static-index store handles **free** games perfectly. Paid games are where it gets
expensive, because payment fundamentally requires a server that knows who bought what.

| Option | Effort | Assessment |
|---|---|---|
| **Sell hardware kits / assembled units** | Low | **Best ROI by far.** How TWANG, ESPboy and comparable projects actually monetize. The value you're selling is the *object*. |
| **Donations** (Ko-fi / GitHub Sponsors, credit in the boot animation) | Very low | Fits the DIY audience genuinely well. |
| **Paid cartridges** with signed licenses | High | Needs store backend, accounts, per-device keypairs, signed+encrypted cartridges, and firmware that refuses unsigned code. |
| Ads | — | No. |

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
- **Tug of War** — *(later)* 2-player via ESP-NOW between two Beam Boys, one tube each.
- **Sonar** — a hidden target; the tube shows only "hotter/colder" as a glow intensity. A game
  the 1D format uniquely enables.

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
- **Button convention: A = primary/instant, B = hold-to-charge.** Established in Wormfight
  (Phase 2) after a push-back ability on B failed to justify occupying the only spare button.
  A quick stab of B should always do *something* useful, so B is never a dead button. Games
  should follow this so muscle memory carries across cartridges.
- **Input state is read from button *levels*, not edges**, for anything that persists across
  frames (charging, held-to-view HUDs). An edge-driven hold sticks forever if a release edge
  is lost to debounce — this caused real bugs twice, in Phase 1 and again in Phase 2.
- **Pick saturated hues; the brightness cap eats subtlety.** A colour drawn at low intensity
  is scaled twice (draw intensity, then the global cap), so it can reach the LED as single-digit
  channel values. At that level there are almost no steps left to express a hue, and WS2812
  blue is perceptually stronger per unit than green — Reflex's `(0,180,120)` "green" zone read
  as teal on hardware (Phase 3). Reserve near-primary colours for anything drawn dim.
- **Round in write-once render paths; truncate in feedback paths.** `present()` and
  `addToPixel()` round, because the downward bias of truncation is visible exactly where
  precision is scarcest. `Color::scaled()` must *keep* truncating: `fade()` applies it to its
  own output, and with rounding a dim pixel never reaches black.
- **Error paths must clean up as thoroughly as success paths.** Phase 4 review found the radio
  left on forever when a *connected* session dropped, because only the connect-*timeout* path
  powered it down. Any invariant worth having ("the radio is off unless asked for") has to hold
  on the paths nobody tests — losing signal, a rebooted router, a cancelled operation.
- **Register callbacks once, not per-use.** Arduino `WebServer` frees route handlers only in its
  destructor, so re-registering on each portal open leaks permanently in a long-lived object.
  The symptom appears somewhere unrelated — a TLS handshake failing for want of contiguous heap.
- **An operation that *stages* a change is not finished until the change is applied.** A
  successful OTA that never reboots reports success, changes nothing, and offers itself again.
- **Engine-level input interception must be opt-in per scene type.** The engine grabs the nav
  button to guarantee a game can never trap the player. But the launcher marks *every* entry as
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
  *while one of its own handlers is still on the stack*. On ESP32 the same scan runs up to ~6.5 s
  under the task watchdog. Respond first, then do slow work from `tick()`.
- **Don't present a sampled result as an inventory.** A WiFi scan hears an AP only if a beacon
  lands in its dwell window, so a missing network is expected, not a defect. Scanning repeatedly
  and merging is not available either — each scan *replaces* the driver's buffer. The fix is an
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
  completion callback runs in *SDK context* and writes to driver-owned state. Tearing the
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
  fragmentation, not just the free total.** ⚠️ In this project that measurement *disproved* the
  OOM theory (39 KB free, 3 % fragmented at the moment of the crash) and pointed at RF timing
  instead. That is the rule working, not failing.
- **Instrument the resource before changing the code.** Three crashes here were confidently
  attributed to three different causes; only the last was supported by measurement. **A decoded
  address is a hypothesis, not a diagnosis** — especially when it lands in a subsystem you did
  not write. Add the counter, get the number, *then* edit.
- **Peripherals contend with the radio even when they are "interrupt-safe".** WS2812 DMA output
  every frame starved the ESP8266 PHY into faulting inside its own timing callbacks. Anything
  driven continuously at frame rate should back off while the radio is doing timing-critical
  work, and should skip rather than wait when the peripheral is busy — `NeoPixelBus::Update()`
  spins on `yield()`, which runs SDK work from inside the render path.
- **Test the invariant the fix relies on, not the crash.** None of the three hardware crashes is
  reproducible on a host — a stack overflow in newlib, driver re-entrancy, and an SDK callback
  outliving a radio reconfiguration. But the `sinf` crash had a precondition that *is* testable: the phase stays
  small. `test/test_soak` asserts exactly that over 6 simulated hours. See `test/README.md`.
- **Float precision, not just stack depth, bounds an animation phase.** Past ~1e5 radians a
  `float`'s steps are coarser than a smooth animation needs, so `wrappedSin()` keeps the device
  alive but the motion stutters regardless. Scenes must wrap their own phase; the helper is a
  safety net, not a licence for an unbounded accumulator.

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
on a Connect entry that can never work; but discarding on *every* failure would erase a good
configuration whenever the router reboots or the console is carried out of range. `kNotFound`
is deliberately kept, because a typo'd SSID is indistinguishable from being out of range and
out of range is far more common. `unproven_` is never persisted: anything that survived a
reboot is treated as proven.

**⚠️ Never derive an authentication verdict from `wl_status_t`.** Value 6 is `WL_WRONG_PASSWORD`
on ESP8266 and `WL_DISCONNECTED` on ESP32; `WL_CONNECT_FAILED` is not an auth verdict on either
core (on ESP32 it also covers an AP at capacity); and on ESP32 the status is an event-updated
cache that can still describe the *previous* attempt right after `WiFi.begin()`. Use the
**disconnect reason**, captured in an event handler. This is the fourth silent divergence
between the two cores; **always build both targets after a WiFi change.**

**Correlate driver events to an attempt with a generation counter, not a boolean.** ESP32
queues events to a separate task, so a failure produced while tearing down one attempt can be
delivered *after* the next has begun. A "attempt in progress" flag attributes it to the new
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
