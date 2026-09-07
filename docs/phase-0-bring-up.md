# Phase 0 — Hardware bring-up

Goal: prove the LED tube works, on battery, before any engine code exists.

The firmware in [`src/main.cpp`](../beam-boy-hw/src/main.cpp) cycles through four test
patterns. Press **button A** to skip ahead. Everything is also logged over serial at
115200 baud.

---

## Wiring (ESP8266 / NodeMCU — available now)

⚠️ **The data pin is not free to choose on the ESP8266.** NeoPixelBus's DMA method drives
the strip from the I2S peripheral, whose output is hard-wired to **GPIO3 (the RX pin)**.
That is a deliberate trade: the DMA method never disables interrupts, so it won't fight the
WiFi stack later — unlike the bit-banged output in `Adafruit_NeoPixel`.

Side effect: GPIO3 is also serial *receive*. Uploading and `Serial.print()` still work
fine; only `Serial.read()` is unavailable. Nothing here needs it.

| Tube wire | Connect to | Notes |
|---|---|---|
| Data (usually green or white) | **GPIO3 / RX**, through a **330 Ω** resistor | The resistor damps ringing on the data line. |
| +5 V (usually red) | External 5 V supply **or** battery | ❌ **Not** the NodeMCU's 3.3 V pin. |
| GND (usually white or black) | Supply GND **and** NodeMCU GND | **The grounds must be joined** or the data signal has no reference. |

Also fit a **470 µF capacitor** across the tube's power and ground, close to the strip. It
absorbs the inrush current when many pixels switch on at once.

| Button | Connect to |
|---|---|
| A | GPIO5 → button → GND (internal pull-up is enabled in firmware) |

**Powering the strip:** with **10 pixels** at the 25/255 brightness cap, draw is low enough
(roughly 60–120 mA) that the NodeMCU's **VIN / 5 V pin** can supply it while the board is on
USB. That keeps the bring-up to a single cable.

⚠️ For the **50 px tube**, use a separate USB power bank or bench supply. It can draw up to
3 A at full white — far beyond what the board can pass through. **Never** power either strip
from the 3.3 V pin.

## Wiring (ESP32-S3 Feather — once it arrives)

Same as above, except the data line moves to a normal GPIO (see `kPinLedData` in
[`board_config.h`](../beam-boy-hw/src/core/board_config.h)), because the ESP32's RMT
peripheral can drive any pin.

⚠️ Confirm the pin numbers in `board_config.h` against Adafruit's published Feather pinout
before wiring — they are provisional. In particular, keep every analog input on **ADC1**;
ADC2 returns garbage whenever WiFi is active.

---

## Results (measured)

Recorded on the 10 px development strip at a brightness cap of 25/255, ESP8266 radio off.
LED current only — the MCU was measured separately.

| Test | Current | Per pixel |
|---|---|---|
| 4 — Full white | **39.0 mA** | 3.9 mA |
| 5 — All dark | **5.7 mA** | 0.57 mA (quiescent) |
| 6 — Game frame | **9.5 mA** | — |

Projected to the 50 px tube: **~195 mA** full white, **~33 mA** in normal play. Adding
~35 mA for the ESP32-S3 gives ~70 mA during play → **30+ hours** from a 2500 mAh cell.

Consequence: the brightness cap was raised from 25 to **64**, since power is not the
limiting factor. Re-measure full white after any further increase.

Other outcomes:

- ✅ All pixels addressable; **index 0 is the green end**
- ✅ Colour order is **GRB** (`NeoGrbFeature` correct)
- ✅ Rainbow smooth, no flicker — DMA output on GPIO3 is clean
- ⏳ LiPo direct-drive and charging checks pending the tube and Feather

---

## Developing on a shorter strip

The pixel count is a build flag, so working on a 10 px strip while the 50 px tube ships
needs no source changes:

```powershell
pio run -e nodemcuv2-10px -t upload      # 10 px development strip
pio run -e nodemcuv2 -t upload           # 50 px production tube
```

For any other length, override it directly:

```powershell
pio run -e nodemcuv2 -t upload --build-flag="-DBEAMBOY_PIXEL_COUNT=8"
```

**This is more than a convenience.** From Phase 1 onward the engine works in normalised
0..1 coordinates, so a game written and tuned on 10 px runs unchanged on 50 px — the
resolution independence the plan calls for is exercised from day one rather than assumed.

What a short strip *can't* tell you: current draw at full length (scale by pixel count —
roughly 5× for 10 px → 50 px) and how the game actually *feels* over a metre. Both need the
real tube, so re-run tests 1 and 4 when it arrives.

---

## Build and flash

```powershell
cd beam-boy-hw

# ESP8266 (bring-up board)
pio run -e nodemcuv2 -t upload
pio device monitor -e nodemcuv2

# ESP32-S3 Feather (target board)
pio run -e adafruit_feather_esp32s3_nopsram -t upload
```

If `pio` isn't on your PATH, use
`& "$env:USERPROFILE\.platformio\penv\Scripts\pio.exe"` instead, or run the tasks from the
PlatformIO sidebar in VS Code.

---

## What to check

### Test 1 — Pixel walk
A single white dot travels the length of the strip, taking ~3 s for a full pass regardless
of length. The **first** pixel glows green and the **last** glows blue.

- ✅ **All pixels light in sequence** → count and wiring are correct.
- ❌ **Only some of the strip responds** → likely insufficient power, or a break in the run.
- ❌ **Nothing lights** → check the shared ground first; it's the most common cause.
- 📝 **Note which physical end is green.** That's index 0, and it determines which way
  round you hold the device. If it's the wrong end, either flip the strip or set a
  "reversed" flag in the Display class in Phase 1 — don't rewire.

### Test 2 — Primaries
The tube goes fully red, then green, then blue, announced on serial.

- ✅ **Colours match the serial log** → `NeoGrbFeature` is correct.
- ❌ **Red and green swapped** → change `LedFeature` to `NeoRgbFeature`.
- ❌ **Some other permutation** → try `NeoBrgFeature` / `NeoRbgFeature` until it matches.

### Test 3 — Rainbow sweep
The payoff shot: a smooth rainbow scrolling along a metre of tube.

- Watch for **flicker or random pixels**, which indicate a data signal problem — usually a
  missing ground, a too-long data wire, or a missing 330 Ω resistor.

### Test 4 — Full white ⚡
Every pixel white at the brightness cap. **Measure current draw here**, with a USB power
meter or a multimeter in series with the supply.

Record the number — it validates the `kBrightnessCap` value and turns the plan's
"7–15 hours" estimate into a real figure.

| Measurement (50 px) | Meaning |
|---|---|
| ~0.3–0.6 A | Expected at a cap of 25/255. Battery life is comfortable. |
| > 1 A | Cap is too high, or it isn't being applied — check `kBrightnessCap`. |

On a 10 px strip expect roughly a fifth of those figures; it's a useful sanity check, but
the real number has to wait for the full tube.

Then compare against a realistic game frame (a few lit pixels), which is what actually
determines runtime.

---

## The critical experiment: LiPo direct drive

The plan assumes the tube can run **straight off the LiPo** (3.7–4.2 V), skipping both a
5 V boost converter and a level shifter. Verify that before committing to the case design.

1. Power the tube from the battery (or a bench supply) instead of 5 V.
2. Run the rainbow test at **4.2 V** (full), **3.7 V** (nominal), and **3.5 V** (nearly flat).
3. Look for flicker, wrong colours, or dead pixels at the low end.

| Result | Action |
|---|---|
| ✅ Clean at all three voltages | Proceed as planned. No boost, no level shifter. |
| ⚠️ Slight dimming/colour shift at 3.5 V | Acceptable — the protection cutoff sits near there anyway. |
| ❌ Flicker or dropouts | Add a **5 V boost converter** plus a **74AHCT125** level shifter, and update the BOM. |

Colours *will* dim somewhat as the battery falls. That's normal for direct drive and is
part of the trade.

---

## Charging check (once the Feather arrives)

1. Plug in USB-C and confirm the charge LED lights, then changes state when full.
2. **Play while charging** — the tube should run normally and the charge state shouldn't
   glitch. This is what proper load sharing buys you.
3. Confirm you can still flash firmware with the battery connected.

---

## Done when

- [ ] All 50 pixels addressable, orientation noted
- [ ] Colour order confirmed in `board_config.h`
- [ ] Rainbow runs clean, no flicker
- [ ] Current draw measured and recorded
- [ ] Tube verified at 4.2 V / 3.7 V / 3.5 V
- [ ] Charging verified (Feather only)

Then Phase 1: the core engine, sub-pixel anti-aliasing, and a dot you can steer.
