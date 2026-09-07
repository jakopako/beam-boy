# Phase 1 — Core engine

The foundation every game stands on: a resolution-independent display with sub-pixel
anti-aliasing, debounced input with an analog stick, a fixed-timestep loop, and the binary
score readout.

Ends with a glowing dot you steer — the first moment the device feels like a console.

---

## Wiring (prototype hardware)

Adds the joystick and a second button to the Phase 0 setup. Pin assignments live in
[`board_config.h`](../beam-boy-hw/src/core/board_config.h).

| Part | Pin | NodeMCU label | Notes |
|---|---|---|---|
| LED strip data | GPIO3 | RX | Through 330 Ω, as in Phase 0 |
| Joystick `VRx` | A0 | A0 | The only ADC the ESP8266 has |
| Joystick `SW` | GPIO14 | D5 | Internal pull-up; no resistor needed |
| Joystick `+5V` | 3V3 | 3V3 | ⚠️ See below |
| Joystick `GND` | GND | GND | |
| Button A | GPIO5 | D1 | Other leg to GND |
| Button B | GPIO4 | D2 | Other leg to GND |

⚠️ **Power the joystick from 3.3 V, not 5 V.** It's a passive potentiometer, so it works
fine at 3.3 V — and it must, because `VRx` feeds the ADC directly. At 5 V the wiper would
present up to 5 V to a pin rated for ~3.3 V (the NodeMCU's A0 divider expects 0–3.3 V), which
risks damaging the chip.

**`VRy` is left unconnected.** A 1D display has no use for a second axis, and skipping it
means the prototype fits the ESP8266's single ADC channel.

**Four-pin buttons:** the pins are two pairs, each pair permanently connected. Use pins from
*opposite* sides (diagonal is safest) or the button will read as always-pressed. If a button
seems stuck on, that's why — rotate it 90°.

---

## Build and flash

```powershell
cd beam-boy-hw
pio run -e nodemcuv2-10px -t upload
pio device monitor -e nodemcuv2-10px
```

⚠️ **Leave the joystick untouched while the board boots.** Its resting position is sampled at
startup to calibrate centre. If the dot drifts on its own, reset the board without touching
the stick.

---

## Controls

| Input | Action |
|---|---|
| **Joystick** (left/right) | Move the dot |
| **A** | Drop a marker, +1 score |
| **B** (hold) | Show the binary score readout |
| **Stick click** | Toggle the motion trail |

**About the markers:** pressing **A** leaves a persistent amber dot at the current position.
Up to **8** are kept; the 9th press pushes the oldest off the front. They are dimmed by age,
brightest first. So amber pixels appearing and later vanishing is expected behaviour, not a
rendering artifact — they are a deliberate demonstration that the display can hold state
between frames.

---

## What to look for

### Sub-pixel anti-aliasing — the main event
Move the dot **very slowly**. It should glide *between* pixels, with light shifting
gradually from one to the next, rather than jumping. On a 10 px strip the effect is
dramatic: it makes a 10-pixel display behave like a much finer one.

Click the stick to toggle the trail off and on. Trail on = motion is obvious; trail off =
the anti-aliasing itself is easier to see.

### Analog feel
Push the stick a little — the dot should creep. Push it fully — it should dash. That
velocity control is exactly what a stepped encoder cannot give you, and why the plan
includes both.

The dot bounces gently off each end, so it can't be lost off the edge.

### Binary score
Press **A** a few times, then hold **B**. The score appears in binary, **anchored at pixel 0**
(bit 0 is always the first pixel), one bit revealed at a time. Bits are coloured by nibble
(blue = bits 0–3, green = 4–7) so place values can be read without counting.

Press A five times → hold B → pixels 0 and 2 light (binary 101 = 5).

The readout always starts at the same end, so a given bit is always in the same place —
values can be read by position rather than counted.

### Frame timing
Every 5 s the serial monitor prints:

```
[PERF] fps=60.0  worst frame=1234us / 16667us budget  stick=0.00 (raw 0.512)
```

- **fps** should sit at ~60.
- **worst frame** is the number that matters. It's the headroom the Phase 5 scripting VM
  will have to fit inside — if native rendering already uses most of 16667 µs, a script
  engine can't work.
- **raw** shows the uncalibrated stick reading; ~0.5 at rest is healthy.

---

## Troubleshooting

| Symptom | Cause |
|---|---|
| Dot drifts without input | Stick was moved during boot. Reset without touching it. |
| Dot moves the wrong way | Call `input.setStickInverted(true)`, or swap the stick's orientation. |
| A button seems always pressed | 4-pin button wired across an internally-connected pair — rotate it 90°. |
| Dot jumps between pixels | Anti-aliasing not applied — check `point()` is used rather than `rawPixel()`. |
| Colours wrong at the strip's start | Some strips reserve pixel 0; adjust `kPixelCount`. |

---

## Architecture notes

**Everything is normalised.** Games work in `0.0`–`1.0`, never pixel indices. A game written
and tuned on the 10 px strip runs unchanged on the 50 px tube — and on whatever a future
Beam Boy uses. This is what makes downloadable cartridges portable, so it matters more than
it looks.

**The engine owns the loop.** Scenes implement `update(dt)` and `render()`; they never call
`delay()` or pace themselves. The timestep is *fixed* rather than measured, so game logic
behaves identically whether or not a frame ran long — important once scripted cartridges
with variable cost are running.

**Blending is additive.** Overlapping sprites brighten instead of overwriting, which reads
much better than last-write-wins when entities cross on a single line.

**The brightness cap is applied at `present()`**, so no game can exceed it however it draws.
It bounds worst-case current as well as setting the look.

---

## Done when

- [x] Dot moves smoothly, gliding between pixels
- [x] Slow stick deflection creeps; full deflection dashes
- [x] Trail toggles with the stick click
- [ ] Binary score reads correctly after pressing A
- [ ] `worst frame` comfortably under 16667 µs

Then Phase 2: Wormfight, the first real game.
