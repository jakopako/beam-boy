# Host tests

Run them:

```
pio test -e native
```

About 13 seconds, no board attached. Requires a host C++ compiler on `PATH`
(any recent GCC, Clang, or MinGW-w64 on Windows).

## What this is for

Flashing the device and playing takes minutes and only exercises the paths you
happen to walk. These tests take seconds and exercise the ones you would never
sit through — including 6 simulated hours of animation and the 49-day `millis()`
rollover.

The `hostshim/` directory has just enough of `Arduino.h`, `NeoPixelBus.h` and
`LittleFS.h` to compile the engine on a PC. The important thing it provides is
that **time and input are variables the test sets**, not a real clock. `millis()`
returns whatever the test last wrote, so a soak test fast-forwards an hour
instantly and a button test places an edge exactly where it wants one.

## Be clear about what this cannot catch

Both crashes found on hardware during Phase 4 were *not* logic errors, and
neither would have failed a conventional unit test:

1. **`sinf()` stack overflow.** An ever-growing phase eventually reached
   newlib's huge-argument path, whose stack frame does not fit in the ESP8266's
   ~4 KB cont stack. The host's libm has no such limit, so the crash is not
   reproducible here at all.

2. **WiFi scan re-entrancy.** `scanNetworks(async)` calls `esp_yield()`
   internally, suspending the loop continuation in the middle of what looked
   like a non-blocking call. That is driver behaviour; there is no driver here.

So the strategy is not "reproduce the crash". It is:

- Test the **invariant the fix depends on** rather than the failure. `test_soak`
  asserts that an animation phase stays small after 6 simulated hours — the
  precondition that keeps `sinf()` on its fast path.
- Accept that hardware-timing bugs are found on hardware, and keep them from
  recurring through the rules in `PLAN.md` §9 and comments at the call site.

A useful heuristic from this project: **"it crashed after a few minutes" almost
always means an accumulator**, and accumulators are cheap to fast-forward here.

## Suites

| Suite | Covers |
|---|---|
| `test_display` | `wrappedSin`/`pulse`, `Color::scaled` truncation (so `fade()` reaches black), HSV, and the refresh divider that keeps LED DMA off the radio's back during provisioning |
| `test_input` | Debounce latching, fast double-taps, hold durations, stick deadzone and shaping, nav hysteresis and auto-repeat |
| `test_soak` | Long-run invariants: phase growth over 6 simulated hours, the `millis()` rollover, fade termination |
| `test_net_policy` | The credential-retention truth table: which connection failures discard stored WiFi credentials |

## Adding a test

Make a directory `test/test_<name>/` with a single `.cpp` containing `main()`,
`setUp()` and `tearDown()`. Add any `src/` file it needs to `build_src_filter`
in the `[env:native]` section of `platformio.ini` — only listed files are
compiled into the test binary.

Code that touches WiFi, OTA or the LED driver is not currently in that filter,
because pulling it in would mean shimming the whole networking stack for very
little return.

**But that is a reason to move the decision, not to skip the test.** `Network`
is untestable here; the rule deciding whether a failed connection keeps its
credentials is not, and it is the part that can quietly ruin someone's evening.
Extracting it into `src/core/net_policy.h` — a header with no Arduino includes
and one `constexpr` function — made all ten cases testable while leaving a
single `if` behind in the driver code. `test_net_policy` needs no shim at all.

When a piece of hardware logic seems untestable, check whether the interesting
part is really the hardware call or a decision standing next to it.

## Check that a new test can fail

A test that cannot fail is not evidence, and a green suite is a bad place to
discover that. After writing one, break the code it covers on purpose and
confirm it goes red.

This is not hypothetical here. The refresh-divider tests were verified by
inverting the divider's skip condition; two of the four failed with
`Expected 3 Was 12` and `Expected 1 Was 4`, and the other two correctly stayed
green because they cover the divider-of-1 path the mutation did not change. That
is the signal you want: the failures land exactly where the behaviour moved.

Restore the source afterwards and re-run to confirm you are back to a clean
suite.
