# Phase 5 — Scripting VM bake-off

Goal: decide what language downloadable cartridges are written in, backed by
measurements rather than taste.

## Berry is ESP32-only

Researched before writing any integration code, which turned out to be the right
order — the answer changes the plan.

| Evidence | Finding |
| --- | --- |
| Tasmota (the only production Berry embedder) | Documents Berry as **"NOT supported on ESP82xx"** |
| Tasmota `library.json` | Hard-scopes `platforms: espressif32` |
| Prior art on ESP8266 | **None exists** |
| Reported RAM at startup | ~10 KB on ESP32 |
| ESP8266 headroom we actually have | ~45 KB |

The technical blocker is not just RAM. The LX106 core has **no unaligned-access
support**, which conflicts with Berry's flash-resident const-object design.
NodeMCU hit exactly this and needed a custom software exception handler to work
around it. Against ~45 KB of headroom, this ranges from borderline to hopeless.

**Consequence:** the scripted half of the bake-off is blocked on the ESP32-S3
Feather arriving. The native half is not, so that is what runs now.

## What the baseline measures

`BenchScene` sweeps 10 / 25 / 50 / 100 entities, 180 frames each, and prints CSV:

```
entities,mean_us,update_us,draw_us,worst_us,pct_of_budget,naive_slowdown,update_headroom
```

The decisive column is **`update_headroom`** — see "Why `update_headroom`, not
`naive_slowdown`" below. `naive_slowdown` is printed alongside it only to make
the difference between the two visible, since the naive reading is the tempting
one and it is wrong by ~6×.

The **`update_us` / `draw_us`** split is what makes that distinction possible: VM
overhead lands on interpreted arithmetic, while draw calls stay native. Splitting
them predicts scripted cost far better than one combined number.

Running this on the **ESP8266** is deliberate: it is the pessimistic board. A
comfortable slowdown headroom there means the ESP32-S3 has room to spare.

`runWorkload()` is the single function a scripted implementation must replace.
Keeping that boundary tight is what makes the comparison meaningful.

## Berry build recipe (for when the Feather arrives)

Researched in advance so integration isn't blocked on rediscovery.

- Only `src/` is needed from the upstream repo.
- The Python `tools/coc/` generator is **mandatory**: `be_string.h`
  unconditionally includes `../generate/be_const_strtab.h` when
  `BE_USE_PRECOMPILED_OBJECT=1`.
- No PlatformIO package exists — vendor it, mirroring Tasmota's layout.
- Needs `extern "C"` when compiled from Arduino C++.

Key config flags:

| Flag | Value | Why |
| --- | --- | --- |
| `BE_USE_SCRIPT_COMPILER` | `0` | Ship precompiled `.bec`; drops ~96 KB of parser/lexer |
| `BE_USE_SINGLE_FLOAT` | `1` | No FPU benefit from doubles here |
| `BE_INTGER_TYPE` | `1` | 32-bit ints |
| fs / os / debug / solidify / time modules | disabled | Cartridges must not touch these anyway |

Shipping precompiled bytecode also matches the cartridge model: the compiler
lives in the build pipeline, not on the device.

## Alternatives considered

| Candidate | Assessment |
| --- | --- |
| **NodeMCU Lua** | Its >40 KB free heap figure only holds via eLua's LTR technique *and* on firmware doing nothing but Lua. Not our situation. |
| **mruby/c** | Claims <40 KB. Worth benchmarking on ESP32 alongside Berry. |
| **mJS** | <1 KB RAM, but no closures or classes — a poor cartridge language. Rejected on expressiveness, not size. |

Rule of thumb from the research: a register VM on a 240 MHz ESP32 manages
~1–3 M instructions/s, giving ~150–500 bytecode instructions per game operation
at 100 ops/frame. Tight but plausible on ESP32. Note the ESP8266 non-OS SDK also
misbehaves if a task exceeds ~15 ms, which independently constrains VM design
there.

## Status

- [x] Berry viability research
- [x] Native baseline harness written, builds on all three envs
- [x] **Native baseline measured on ESP8266 @ 10 px**
- [x] Renderer optimised to fixed-point after the baseline exposed soft-float cost
- [x] Baseline re-measured — **verdict: scripting is viable**
- [ ] Berry integration on ESP32-S3 — **blocked on Feather**
- [ ] mruby/c comparison — blocked on same
- [ ] Final language decision

## Verdict: the scripting model is viable

On the ESP8266 — the *pessimistic* board, with no FPU and a fraction of the S3's
clock — a 100-entity frame costs **11 % of budget**, leaving the interpreted half
**~52× slower than native** before frames start dropping. Against the rule of
thumb of ~150–500 bytecode instructions per game op, that is comfortable rather
than marginal.

The architectural bet in this plan — native engine plus scripted cartridges — is
sound. Proceed to Phase 6 once the Feather arrives.

## Baseline results (ESP8266 @ 160 MHz, 10 px, free heap ~46.8 KB)

After the fixed-point renderer change:

```
entities,mean_us,update_us,draw_us,worst_us,pct_of_budget,naive_slowdown,update_headroom
10,197,29,161,246,1.18,84.6,569.2
25,474,72,395,495,2.84,35.2,226.0
50,937,144,785,952,5.62,17.8,110.3
100,1861,289,1565,1882,11.17,9.0,52.3
```

Effect of the optimisation:

| entities | draw_us before | draw_us after | change |
| --- | --- | --- | --- |
| 10 | 247 | 161 | −35 % |
| 25 | 609 | 395 | −35 % |
| 50 | 1216 | 785 | −35 % |
| 100 | 2423 | 1565 | −35 % |

`update_us` was **identical** across both runs (29 / 72 / 144 / 289). That path
was not touched, so this is a useful check that the harness measures what it
claims to and that the gain is real rather than noise.

### Why `update_headroom`, not `naive_slowdown`

`naive_slowdown` (budget ÷ total native cost) assumes a VM slows the draw calls
too. It does not: a script calls `beam.point()` and the pixel maths inside still
runs as compiled C++. Only the update half is interpreted.

```
update_headroom = (budget - draw_us) / update_us
```

Draw is still ~84 % of the frame after optimisation, so the two figures differ by
nearly 6×. Reading the naive column would have wrongly suggested that most VMs
were out of reach.

### Finding: the renderer was soft-float bound

24.7 µs for a single `point()` call was far too slow for two multiply-add blends.
The **LX106 has no FPU**, so every float operation is a soft-float library call.
`Color::scaled()` did three float multiplies plus three float→int casts, twice
per point.

Fixed by converting the blend path to **8.8 fixed-point**: the weight becomes a
0..256 integer once, then channels scale by integer multiply and shift.
`addToPixel()` fuses the scale and the blend, dropping a temporary `Color`.

This is a floor under every game, not just the benchmark. The ESP32-S3 has an FPU
so the win will be smaller there — but the ESP8266 is the prototyping board, and
this also buys back budget that the VM will later want.

**Further optimisation is available but not currently warranted.** `point()` still
does soft-float work outside the blend: the shake offset, the position-to-pixel
multiply, and the fractional split. Draw remains 5.4× update. Chasing it would buy
headroom the project does not need — revisit only if the 50 px tube or a real VM
makes frames tight.

## Remaining caveats

- Measured at **10 px**. `point()` cost is length-independent, but `present()` and
  `fade()` are O(pixels), so the 50 px tube adds a fixed per-frame cost that comes
  straight out of the VM's share. Re-run this benchmark when the tube arrives.
- **Per-call VM→native crossing overhead is not captured here.** At 100 entities a
  frame makes ~200 draw calls; a costly binding could hurt more than interpretation
  itself. Measure crossing cost specifically during integration.
- ESP32-S3 numbers will differ in *both* directions: faster clock and an FPU, but a
  VM's allocator and GC behaviour are new costs not modelled here at all.

## Open questions

- No public Berry benchmarks or Xtensa footprint measurements exist; all flash
  figures here are extrapolation and must be confirmed by measurement.
- No upstream statement of *why* Tasmota excludes ESP82xx — the unaligned-access
  reason is inferred, though it fits the NodeMCU precedent well.
