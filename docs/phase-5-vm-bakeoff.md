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
- [x] Berry integration on ESP32-S3 — **DevKitC used, Feather not required for this**
- [ ] Batched-call measurement (one script call per frame, not per entity)
- [ ] mruby/c comparison
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

## Berry integration on real hardware: `update_headroom` measured, not projected

The Feather had not arrived, but Berry only needs an ESP32 core, not the Feather's
charging circuit — so this ran on the `esp32-s3-devkitc-1-n16r8` env instead of
waiting. See `VmBenchScene` (`src/scenes/vm_bench_scene.*`), which is `BenchScene`
with the update half replaced by a Berry call and nothing else, so the two are
directly comparable per the "keep the boundary tight" rule.

**Vendoring notes** (diverged from the plan written before any code existed):

| Planned | What actually happened |
| --- | --- |
| Ship precompiled `.bec` bytecode from the start | Kept `BE_USE_SCRIPT_COMPILER=1` for the bake-off — loading source via `be_loadstring()` is far faster to iterate on, and the parser's ~96 KB is a shipping-time optimisation, not a bake-off blocker. Revisit before Phase 6. |
| Vendor `src/` only | Also needed `default/be_modtab.c` (the module/class registration table — every embedder needs its own copy) and a custom `be_port_beamboy.cpp` in place of `default/be_port.c`, since the upstream port assumes POSIX stdio and a real filesystem. Routes `print()` to `Serial.write`; file I/O is stubbed to fail cleanly rather than vendor a filesystem cartridges must not have direct access to. |
| — | `be_filelib.c` (the `file` builtin) has **no `#if BE_USE_FILE_SYSTEM` guard at all**, unlike every other optional module — it must be excluded via `library.json`'s `srcFilter`, not just config. One remaining symbol (`be_nfunc_open`, referenced unconditionally from the precompiled builtins table) is stubbed to raise a clean Berry exception instead. |

**Workload shape measured:** one Berry call per entity per frame — `beam_update(pos,
vel, dt)` returning a two-element list — deliberately the *expensive* shape, since a
real cartridge calls into script once per entity, not once for the whole frame over
a script-resident list. Per-call cost (stack setup, argument marshalling, list
alloc for the return, GC bookkeeping) is exactly what native code never pays and
exactly what a synthetic "call the interpreter once and loop internally" benchmark
would hide.

### Results (ESP32-S3, `esp32-s3-devkitc-1-n16r8`, 10 px, single-precision float)

```
entities,mean_us,update_us,draw_us,worst_us,pct_of_budget,naive_slowdown,update_headroom
10,745,715,23,866,4.47,22.4,23.3
25,1479,1436,35,1569,8.87,11.3,11.6
50,2605,2545,52,2677,15.63,6.4,6.5
100,4847,4750,88,4874,29.08,3.4,3.5
```

Repeated across three runs with identical results at every step (the seeding is
deterministic) — this is a stable measurement, not noise.

**This is the first real VM number in the project, and it changes the read from
the native-only projection.** The ESP8266 baseline's "52× headroom at 100
entities" was native-vs-native — how much slower an *interpreted* version could be
and still hit budget, with no actual interpreter in the loop. The real Berry number
at the same entity count is **update_headroom = 3.5**: the scripted update path may
be at most 3.5× slower than equivalent native C++ before frames start dropping.
Comfortably positive, but an order of magnitude tighter than the projection — the
gap is per-call crossing overhead the earlier estimate had no way to see.

Headroom **shrinks** as entity count grows (23.3 → 3.5) because per-call overhead
is roughly constant per entity while the arithmetic each call does is trivial (one
multiply-add and a compare) — so the fixed cost dominates and does not amortise the
way it would if entities shared one call. This is the concrete argument for
**batching**: a single script call over a whole entity list should amortise the
per-call overhead across N entities instead of paying it N times, and is the
natural next experiment.

### Batching: the gap was call overhead, confirmed

`VmBenchScene` now measures both calling conventions from one flash, toggled with
**B** on the results screen: the per-entity variant above (`bench_update`, one call
per entity) versus a batched variant (`bench_update_all`, one call per **frame**
that loops internally over a persistent Berry list rebuilt only when the entity
count changes). Both scripts do identical arithmetic per entity — the only
difference is how many times the native/VM boundary is crossed per frame.

```
entities,mean_us,update_us,draw_us,worst_us,pct_of_budget,naive_slowdown,update_headroom
10,255,230,21,398,1.53,65.4,72.4
25,283,238,41,397,1.70,58.9,69.9
50,306,233,69,425,1.84,54.5,71.2
100,364,238,122,489,2.18,45.8,69.5
```

(ESP32-S3, `esp32-s3-devkitc-1-n16r8`, 10 px, single-precision float.)

**Headroom at 100 entities jumps from 3.5× to 69.5×** — a ~20× improvement from
batching alone — and, notably, no longer shrinks with entity count the way the
per-entity variant did (72.4 → 69.5, essentially flat, versus 23.3 → 3.5). This
confirms the earlier hypothesis exactly: the tight per-entity number was almost
entirely N calls' worth of stack setup/marshalling/GC bookkeeping, not N calls'
worth of interpretation. Once the boundary is crossed once per frame instead of
once per entity, `update_us` stays roughly constant (~230-240 us) regardless of
entity count, and only the native readback loop (copying results back into
`entities_[]`) grows slightly with N.

This also happens to land above the original ESP8266 native-only projection of
52× — expected, since that projection was for a different (slower) chip; it is
not a like-for-like comparison, just a reassuring sanity check that the number is
in a plausible range.

**Practical implication for cartridge API design:** a scripted game should expose
one "update all entities" entry point that owns and loops over its own entity
list, not a per-entity callback the engine invokes N times. This is now a concrete
design constraint for the Phase 6 cartridge API, not just a performance nice-to-have.

### Status: viable

69.5× headroom at Wormfight's busiest case (100 entities, one worm wave) using the
batched calling convention is a comfortable pass — the earlier 3.5× per-entity
number was a worst-case shape that a sane cartridge API will not actually use.
Remaining follow-up, not blocking a language decision:

1. **Re-measure on the 50 px tube once it arrives** — `update_headroom`'s
   denominator (`update_us`) is unaffected by pixel count, but its numerator
   (`kFrameBudgetUs - draw_us`) shrinks as `draw_us` grows with more pixels, so
   headroom on the shipping tube will be lower than measured here at 10 px. Even
   allowing for that, the margin is now large enough that this is a confirmation
   step, not a risk.

With both numbers in hand, mruby/c comparison (`vm-mruby-compare`) is no longer
necessary to justify Berry on performance grounds — Berry clears the bar
comfortably with a sane API shape. It remains useful only if Berry's authoring
ergonomics or footprint become a problem later, so it stays parked rather than
being pursued now.

## Open questions

- No public Berry benchmarks or Xtensa footprint measurements exist; all flash
  figures here are extrapolation and must be confirmed by measurement.
- No upstream statement of *why* Tasmota excludes ESP82xx — the unaligned-access
  reason is inferred, though it fits the NodeMCU precedent well.

