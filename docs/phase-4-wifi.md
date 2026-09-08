# Phase 4 — WiFi, opt-in

Networking that stays out of the way. The radio is **off** unless you walk into
the Network scene and ask for it, and everything here is built so that offline
behaviour is not merely the fallback but the default.

> ## ✅ Status: ESP8266 provisioning crash confirmed fixed on ESP32
>
> Connecting to a network reliably crashed the NodeMCU inside the WiFi PHY. The
> cause was WS2812 DMA output contending with the radio. Five ESP8266-side
> fixes did not resolve it — the last, slowing the LED refresh, reduced but did
> not eliminate it — and further work there was deliberately paused pending
> ESP32 hardware.
>
> **The ESP32-S3 DevKitC does not reproduce the crash**, confirming the
> diagnosis. The refresh-divider workaround has been reverted; connection
> animations run at full rate again, as they did before the mitigation existed.
> The ESP8266 remains dev-only and this crash there is not planned to be fixed.
>
> Start at [the confirmed fix](#the-crash-that-is-still-unresolved-on-the-esp8266--confirmed-fixed-by-the-s3).

## Games vs. firmware — two different update paths

Easy to conflate, so stated plainly:

| | **Games** (Phases 6–7) | **Firmware OTA** (this phase) |
| --- | --- | --- |
| What it updates | Cartridge scripts + metadata | The C++ engine: renderer, input, launcher, network code |
| Written to | LittleFS data partition | The **app partition** — the executable itself |
| Reboot needed | No | Yes |
| If it goes wrong | One broken game | **A bricked console** |

Adding *Wormfight 2* is a game download. Fixing a renderer bug, extending the
script API, or patching a security hole on a device already in someone's hands
is firmware OTA. Without it, every engine fix means asking users for a USB cable.

### How the two-slot mechanism works

Flash is divided into two application slots:

1. The device is running from slot A.
2. The new image is downloaded into slot B. **Slot B is not executable** — it is
   just bytes being written to a spare region.
3. The image is verified (length and checksum).
4. Only then is the boot pointer flipped to B, and the device reboots.

If the download stalls, the checksum fails, or power drops halfway, **slot A is
untouched** and the console boots exactly as before. There is no window in which
a half-downloaded image can run. This is why the BOM specifies 8 MB flash: two
complete copies of the firmware must fit simultaneously.

Both current environments already have OTA-capable layouts:

| Board | Firmware size | Slot size | Headroom |
| --- | --- | --- | --- |
| ESP8266 | 485 KB | 1019 KB | 52 % |
| ESP32-S3 | 1016 KB | 2 MB | 51 % |

## Why not WiFiManager

The plan originally named WiFiManager, and it was rejected during implementation.

WiFiManager **owns the main loop** — `autoConnect()` and `startConfigPortal()`
block until they finish. On this device the tube is the only feedback channel,
so a blocked loop means a frozen display for the entire portal session, which is
precisely when the user most needs to be told what is happening.

The portal here is hand-rolled instead: SoftAP + `DNSServer` + a small
`WebServer`, all serviced from `Network::tick()` once per frame. Roughly 200
lines, and the tube keeps animating throughout.

> ✅ **Revisited now that the S3 is in hand — decision unchanged.** WiFiManager's
> blocking behaviour was being reconsidered because it accidentally sidesteps
> the ESP8266 LED/WiFi contention crash. That crash does not reproduce on the
> ESP32-S3 (see
> [the confirmed fix](#the-crash-that-is-still-unresolved-on-the-esp8266--confirmed-fixed-by-the-s3)),
> so the reason for revisiting no longer applies on the board this project is
> shipping on, and the original reasoning above stands: the hand-rolled portal
> keeps the tube animating, which WiFiManager cannot do while blocking.

## How the captive portal works

1. The device brings up an open access point, `BeamBoy-Setup`.
2. A DNS server answers **every** query with the device's own IP.
3. The phone, on joining, probes a known URL to test for internet:
   - Apple → `captive.apple.com/hotspot-detect.html`
   - Android → `connectivitycheck.gstatic.com/generate_204`
   - Windows → `www.msftconnecttest.com/connecttest.txt`
4. DNS sends that probe to us, and `handleNotFound()` answers with a `302`
   redirect rather than the expected success response.
5. The OS concludes it is behind a captive portal and **auto-opens the form**.

No special-casing per OS is needed: any URL that is not `/` or `/save` gets
redirected, which covers all of them and any future probe URLs too.

If the sheet does not appear — it is inconsistent, particularly on Android — the
fallback is to open `http://192.168.4.1` manually. The IP is printed to serial.

The setup page is a single self-contained HTML string in `PROGMEM` with no
external CSS, fonts or scripts. Anything external would hang, since the portal
has no route to the internet.

## Reading the tube

No text, so **every state has a distinct motion as well as a distinct colour**.
Colour alone is not enough: the strip quantises hue badly at low brightness, and
a red/green distinction is invisible to a red-green colourblind player.

| State | Colour | Motion |
| --- | --- | --- |
| Menu: Connect | Blue | Short bar at 20 %, breathing |
| Menu: Setup | Amber | Short bar at 50 %, breathing |
| Menu: Forget | Red | Short bar at 80 %, breathing |
| Menu: Forget, armed | Red | Same bar, blinking hard and fast |
| Credentials erased | Red | Wipe inward from both ends, once |
| Portal open | Amber | Slow full-length breathing |
| Portal open, scanning | Amber + white | Breathing, with a white dot sweeping |
| Connecting | Blue | Dot sweeping with a trail |
| Connected | Green | Wipe outward from centre, then a slow heartbeat |
| Failed: password rejected | Red | **Two** flashes, then a dim hold |
| Failed: network not found | Red | **Four** flashes, then a dim hold |
| Failed: timed out / link lost | Red | **Three** flashes, then a dim hold |
| OTA downloading | Amber | Static progress bar |
| OTA installed | Green | Fills from both ends, then pulses white at centre |
| OTA up to date | Blue | Steady dim |

The selected menu entry breathes while the others sit dim and still — brightness
and motion mark the selection. All entries are drawn the same width: an earlier
version shrank the unselected ones, which made them read as glitches rather than
choices (see below).

Menu entries sit at **fixed positions**, so the same action is always in the same
place. Connect and Forget are hidden entirely until credentials exist.

The failure states differ by **flash count** rather than by colour, because the
user's next move differs completely between them — retype the password, versus
move closer or just try again later. On a 1D display rhythm is the only channel
available for a small number, so this is the same trick the score readout uses.
Two flashes also carries a second meaning: the credentials have been dropped,
which is why the menu behind it has fallen back to Setup only.

## Controls

| Context | Button | Action |
| --- | --- | --- |
| Menu | Nav | Move between entries |
| Menu | A / nav click | Activate |
| Menu, on Forget | A twice | Erase stored credentials (see below) |
| Busy (any state) | B | Cancel, radio off |
| Connected | A | Check for and install firmware update |
| After OTA success | B | **Reboot into the new firmware** |
| After other OTA result | B | Acknowledge, back to connected |

The menu has **three entries once provisioned** and only one before, since there is
nothing to connect to or forget until credentials exist:

| Position | Colour | Entry | Shown when |
| --- | --- | --- | --- |
| Left | Blue | Connect | Provisioned |
| Centre | Amber | Setup — open the portal | Always |
| Right | Red | Forget — erase credentials | Provisioned |

### Forgetting a network

Forget erases `/net.cfg`, after which the device is back to its factory state and
the menu collapses to Setup alone. Three properties it needs, none of which it had
at first:

- **It asks.** One press arms it and the entry starts blinking hard and fast
  instead of breathing; a second press within three seconds commits. Navigating
  away, or waiting, disarms it. Every other menu entry is harmless enough to fire
  on a single press — this one is not, and there is no text to warn with.
- **It says it worked.** A red wipe inward from both ends. Previously the only
  evidence was the menu quietly having one fewer entry afterwards, which does not
  distinguish "erased" from "that button does nothing".
- **It drops a live connection.** Forgetting while connected also takes the radio
  down. Otherwise the device stays associated to the network it claims to have
  forgotten, still drawing power for it, until the next reboot.

Leaving the scene **always** takes the radio down. Wandering back to the launcher
cannot leave WiFi quietly draining the battery — that is the exact failure the
opt-in design exists to prevent.

The reboot is deliberately a button press rather than automatic. A staged update
does nothing until the device restarts, so the success state pulses white at the
centre instead of settling: it must not look like a finished, dismissable screen.

## Bugs caught in review

All four found by a review pass before any hardware testing, and all four were
real.

**1. Leaked HTTP route handlers (high).** `startServer()` re-registered all three
routes on every portal open, but `stop()` only closes the socket — both cores
free handlers solely in the `WebServer` destructor, and this `Network` lives in a
never-destroyed file-static scene. Opening Setup, cancelling, and reopening
permanently leaked three handler objects a time and appended duplicate routes.
The symptom would have been baffling: **OTA starts failing after a handful of
setup attempts**, because the leak erodes the contiguous block the TLS handshake
needs, while nothing about the portal itself looks wrong. Routes are now
registered once.

**2. A dropped connection left the radio on forever (high).** The connect-timeout
path powered the radio down before entering `kFailed`, but the
`kConnected → kFailed` transition did not — and `kFailed` has no timeout and no
exit action. Walking out of range or rebooting the router left the radio powered
in STA mode until the user happened to press B. **This broke the core radio-off
invariant on precisely the path nobody thinks to test.** Now torn down on both
paths.

**3. A successful OTA never took effect (medium).** `rebootOnUpdate(false)` was
set and nothing ever restarted the device. The image was written and the boot
slot flipped, but the console kept running the old firmware, reported success,
still showed the old version, and offered the same update again on the next
check. Now B reboots from the success state.

**4. Stale image URL after "up to date" (medium).** The manifest parser stored the
URL *before* comparing versions and never cleared it, so `image_url_` survived a
no-op check — and, being in a static scene, survived across scene entries too.
Any future path reaching `install()` would have silently **reflashed the running
image**. The URL is now cleared on both the up-to-date and failure branches, and
`install()` refuses unless the state is `kAvailable`.

Two themes worth carrying forward: **error paths need the same cleanup as success
paths** (bugs 1 and 2), and **an operation that stages a change is not finished
until the change is applied** (bug 3).

## The bug found on hardware: "the portal never appeared"

First hardware test of this phase: the Setup entry showed **three yellow LEDs breathing
in the middle of the tube**, and no `BeamBoy-Setup` network was visible from an iPhone.

The symptom named the cause. Portal-active lights the **whole** strip; a short centred
bar is the scene's **menu**. So the scene was running fine — the radio had simply never
been switched on, because the button that selects a menu entry never reached the scene.

`Engine::tick()` gated its pause/exit handling on `current_game_ >= 0`. But
`LauncherScene` calls `setCurrentGame(selected_)` for *every* entry, utility scenes
included. So in Network the nav button was interpreted as **pause**, and the paused
branch `return`s before `scene_->update()` — the menu could never be activated. Nothing
about WiFi was broken at all.

Fixed with `GameEntry::is_game`. Games keep pause-then-hold-B, because they use long
holds during play (Wormfight charges on B for ~1.1 s) and a bare hold-to-exit would
fight their controls. Utility scenes have no state worth freezing, so they get a direct
hold-B exit and keep the nav button for themselves. The engine draws the hold progress
bar for them, since they have no pause overlay to carry that feedback.

Worth generalising: **engine-level input interception has to be opt-in per scene type.**
It was added to guarantee a game can never trap the player — a good rule that quietly
became a bad one when applied to scenes that need the button for their own menu.

## Picking a network from a list

The first version of the form had a plain text field for the network name, which
means typing your SSID exactly right on a phone keyboard with no feedback if you
get it wrong -- the only symptom of a typo is a red tube 20 seconds later.

`startPortal()` now scans and offers the result as a dropdown, with a
signal-strength hint so two similar names can be told apart.

### The list is expected to be incomplete

Reported from hardware: a network was missing from the dropdown. It is worth
being precise that **this is normal, not a defect**, because it changes what the
right fix is.

A WiFi scan is a sample, not an inventory. The radio hops channels and listens;
an access point is heard only if one of its beacons lands inside the dwell window
for its channel. An AP beaconing every 100 ms on a busy channel can simply go
unheard that time round. Phones disguise this by scanning continuously in the
background and merging results over time — the console gets one sweep and shows
it. On top of that, **5 GHz-only networks can never appear**: neither chip has a
5 GHz radio. Nor can hidden ones, which broadcast no name.

So the fix is not "scan harder" but "make retrying trivial and be honest that the
list is partial":

- A **Scan again** link, and text saying a scan can miss networks.
- On ESP32, a 500 ms per-channel dwell instead of the 300 ms default, which gives
  slow-beaconing APs another chance. The ESP8266 API exposes no dwell control.
- `show_hidden = true`, so hidden APs at least count toward the total.
- The free-text field stays, always, for everything a scan cannot reach.

Scanning several times in a row and merging was considered and rejected: each
scan **replaces** the driver's result buffer rather than adding to it, so a
second sweep that hears fewer APs leaves a shorter list — and, if the count from
the longer sweep were kept, an out-of-range index.

### The scan must not block, and especially not inside a request handler

The first version of the rescan route scanned synchronously and then rendered the
page. Review caught this, and it was a genuine defect on both chips:

- On **ESP8266**, a synchronous scan blocks via `esp_suspend()`, which resumes
  the main loop continuation — which calls `tick()`, which calls
  `server_.handleClient()` **again, while one of its own handlers is still on the
  stack**. Unbounded re-entrancy of the web and DNS servers.
- On **ESP32**, the same scan takes up to ~6.5 s (13 channels × 500 ms) with the
  task watchdog running.
- On both, the tube freezes for the entire scan — during the one operation the
  user is most likely to be watching it.

The scan is now asynchronous: `runScan()` starts it and returns, `pollScan()`
collects the result from `tick()`, and `/rescan` **replies before scanning** with
a short "Scanning…" page that reloads itself. `scan_count_` is never read while
`scan_pending_` is true, since the result buffer belongs to the running scan.

This is the file's own non-blocking rule, which the first version broke in the
one place it was least visible. **A blocking call inside a request handler is
worse than a blocking call in the main loop**: it re-enters whatever pumps the
server.

### Other details worth keeping

- **The portal runs in AP+STA, not AP.** The station half never connects to
  anything, but its presence is what allows a scan while the AP stays up.
  Switching to `WIFI_STA` to scan would drop the phone outright. This does not
  make scanning free — one radio means it still leaves the channel and drops
  traffic while sweeping — but the phone stays associated, which is the
  difference between a page that stalls briefly and one that fails.
- **SSIDs are attacker-controlled strings placed into HTML.** A neighbouring
  access point can be named anything, including a quote or a tag. They go into
  both an attribute and element text, so `appendEscaped()` handles both.
- **Scan results must be freed explicitly.** The WiFi driver owns them until
  `scanDelete()`. Skipping it would leak a few hundred bytes per portal session
  in a never-destroyed object — the same shape as the handler leak found in
  review, surfacing the same confusing way, as a TLS handshake failing much later
  for want of contiguous heap.
- **The tube shows the scan too.** A white dot sweeps the amber portal glow while
  a scan is in flight. Without it, the one moment the user may be waiting on the
  console rather than on their phone looks identical to idle.

## Testing the portal

Worth exercising deliberately, since most of these paths only appear when
something goes wrong:

| Try this | Expect |
| --- | --- |
| Open the form while the first scan is still running | "Still scanning", plain text field, white dot sweeping the tube |
| Tap **Scan again** | "Scanning…" page, reloads itself to a fresh list |
| Tap **Scan again** repeatedly | No crash, no duplicate scans — the core ignores a second start |
| Pick **Other** and type a name | Connects using the typed name |
| Submit with both fields empty | Error page, not a hang |
| Rescan, then reload the form immediately | Dropdown or text field — never an empty dropdown |

## The "random colours" that were actually the menu

Second hardware report of this phase: after provisioning, entering Setup showed
**randomly coloured LEDs across the strip**.

Nothing was wrong. Once credentials are stored the menu has three entries --
Connect (blue), Setup (amber), Forget (red) -- where before it had one. Unselected
entries were drawn a third the width of the selected one, so on a short strip they
became single dim pixels of three unrelated colours at three positions: far more
like rendering artefacts than like a row of choices.

All entries are now drawn the same width, with brightness and motion as the only
selection cue. The general point, which applies to every menu on this display:
**on a 1D strip, an element too small to read as deliberate reads as a glitch.**
Size is not a good way to signal selection here; brightness and movement are.

## The crash that looked like a WiFi bug

Reported from hardware: the device reset while the portal was open, shortly after
a scan. The obvious suspect was the new asynchronous scanning. It was innocent —
the serial log showed `[net] scan found 9`, so the scan had already completed
successfully, and the exception followed later.

Decoding the stack named the real culprit:

```
0x402249db: sinf at newlib/libm/math/sf_sin.c:41
0x40225bd7: __kernel_rem_pio2f at newlib/libm/math/kf_rem_pio2.c:95
0x4020bef7: beamboy::NetworkScene::drawStatus(beamboy::Engine&)
```

`sinf()` on the ESP8266 is only cheap for small arguments. Past a few hundred it
falls out of its fast path into `__kernel_rem_pio2f`, the huge-argument
reduction routine, which allocates a large local array. The cont task's stack is
about 4 KB, so that allocation overflows it — the dump shows
`sp: 3ffffbe0 end: 3fffffd0`, nearly exhausted.

`NetworkScene::phase_` grows by `dt` every frame forever. The portal breathing
multiplies it by ~5.65, so a few minutes on that screen is enough. **Nothing was
wrong with the scan; the animation had simply been on screen longer than any
animation before it.** Every scene had the same latent bug, including five places
using `millis() / 1000.0f * rate`, which climbs to millions before rollover.

Fixed with `wrappedSin()` and `pulse()` in `display.h`, which wrap the argument
into one period first. Verified numerically: the wrap agrees with plain `sinf`
to about 2e-6 at realistic phases, so nothing changes visually. `phase_` is also
wrapped at an hour to stop float precision decaying as it grows.

Plain `sinf` is still fine where the argument is bounded by construction — the
failure flash uses it, with a comment saying why, so it does not look like one
that was missed.

Two general lessons, both in `PLAN.md` §9: **never call `sinf()` on an
ever-growing argument**, and **"it crashed after a few minutes" points at an
accumulator, not at whatever was being tested at the time.**

## The second crash: "async" that suspends

A different crash followed, with a different signature — worth separating,
because the reflex is to assume the previous fix regressed.

| | First crash | Second crash |
|---|---|---|
| Reset cause | `rst cause:4` — hardware watchdog | `rst cause:2` — illegal instruction |
| Stack | `sp: 3ffffbe0` — nearly exhausted | `sp: 3ffffe70` — almost empty |
| Timing | minutes into the portal screen | immediately, as the phone associated |

An almost-empty stack rules out a stack overflow, so this was not the same bug.
`epc1=0x40218b92` decoded to `loop_end`, which runs the scheduled-function queue
— consistent with control returning to the main loop at a moment when something
it depended on was not yet built.

The cause is in the core's own scan implementation:

```c
if (ESP8266WiFiScanClass::_scanAsync) {
    esp_yield();          // "time for the OS to trigger the scan"
    return WIFI_SCAN_RUNNING;
}
```

`esp_yield()` suspends the loop continuation and returns to the SDK. So
`scanNetworks(async)` — a call whose entire point is not to block — **does not
return to its caller until the SDK has run.** It also calls `enableSTA()` and
`wifi_station_disconnect()` internally, reconfiguring the radio underneath the
caller during exactly that gap.

(The continuation is resumed in place, not restarted, so this is not re-entrancy
— nothing runs on top of the suspended frame. The hazard is the gap itself:
whatever the caller had not done yet stays undone while arbitrary driver code
runs.)

`startPortal()` called it first, then set up the AP, DNS and web server. The
device therefore sat in the SDK with a half-constructed portal: no soft-AP, no
DNS server, and the state still `kConnecting`. That is why the crash landed
exactly when a phone associated.

This is the same shape as the synchronous-scan re-entrancy bug rejected earlier
in this phase, so the fix is structural rather than a reordering:

- `requestScan()` only sets a flag. It touches nothing.
- `startRequestedScan()` makes the actual driver call, and is invoked from
  **one place**: the very last statement of `tick()`, guarded on
  `kPortalActive`.

Suspending there is harmless, because the gap falls at a point where the portal
is fully built and no HTTP handler is part-way through — there is no
half-finished work for the SDK to trip over. `handleRescan()` likewise now only
requests, so no scan ever suspends with `server_.handleClient()` beneath it.

The general rule, in `PLAN.md` §9: **any SDK call that might suspend gets
requested via a flag and issued from a single known-safe point, never inline.**

## The third crash: a scan callback that outlives the radio

Reported as happening **almost every time the console connected to a WiFi** —
i.e. on the portal-save path, not at random.

```
Exception (0): epc1=0x40246a41
ctx: sys          <- SDK task, not the loop continuation
```

`epc1` decodes to `hostap_input`, and the log showed `[net] scan found 12`
shortly before. Two details narrow it down immediately:

- **`ctx: sys`**, where both earlier crashes were `ctx: cont`. This faulted in
  SDK code with none of ours on the stack.
- It happened right after saving credentials, which is the one moment the
  firmware tears the soft-AP down and reconfigures the radio.

Reading the core's scan implementation explains it:

- `_scanDone()` — the SDK's scan-completion callback — **runs in SDK context**,
  allocates a `bss_info[]` and writes it into the core's static `_scanResult`.
  That is the `ctx: sys` in the trace.
- `WiFi.scanDelete()` frees the *previous, completed* buffer. It does **not**
  cancel a scan still in flight, and it leaves `_scanStarted` set.

So the sequence in `kPortalSaved` was: stop the server, stop DNS,
`softAPdisconnect(true)`, then `connect()` → `WIFI_STA` + `WiFi.begin()`, all
while a portal scan was still owned by the SDK. The pending callback then fired
against a radio that had been reconfigured underneath it.

The fix is one condition — don't tear the radio down while a scan is
outstanding:

```cpp
if (elapsed > 1500 && !scan_pending_) { ... connect(); }
```

`pollScan()` runs at the top of `tick()` and clears `scan_pending_` for failed
scans too (`WIFI_SCAN_FAILED`), so this waits at most one scan period and cannot
deadlock.

`disconnect()` has the same shape but must stay immediate, since it is user
cancellation. It is survivable there because `WIFI_OFF` follows straight away
and the machine settles in `kOff` — it does not go on to bring a station up on
top of the pending callback, which is what made `kPortalSaved` fatal.

The same investigation removed a second, independent heap hazard on that path:
`handleRoot()` used to build the `<option>` list into its own ~1 KB `String`
before appending it to the ~2 KB page `String`, doubling peak heap while the AP
was live and allocating a pbuf per received frame. A counting pre-pass now
decides dropdown-vs-text field, and options append straight into `page`.

> **Correction, added after the next crash.** The teardown-ordering fix above is
> correct and worth keeping, but attributing that crash *solely* to it was wrong.
> The next crash landed at a different address in a different context, which
> showed the underlying cause was heap exhaustion — see the section below. The
> `hostap_input` PC was a symptom of that, not proof of a scan-callback bug.
> Lesson: a single decoded address is a hypothesis, not a diagnosis; it takes a
> second data point to tell "bug here" from "out of memory everywhere".

**Rule, now in `PLAN.md` §9: an SDK callback you cannot cancel is a constraint
on teardown ordering. Freeing a result buffer is not the same as cancelling the
operation that fills it.**

## The fourth crash: the real cause was memory all along

Reported after tapping **Scan again** in the portal:

```
Exception (0): epc1=0x4021abfb   ->  run_scheduled_recurrent_functions()
ctx: cont        <- loop context this time, not the SDK task
```

This is the decisive data point, and it corrected the diagnosis above. The fault
address had **moved** — the previous crash was `hostap_input` in `ctx: sys`, this
one is a different core function in `ctx: cont`. Both are `Exception (0)` with
`excvaddr=0x00000000`.

**A fault location that wanders between unrelated allocating call sites is the
signature of memory exhaustion, not of a bug at either site.** Neither function
is at fault; they are just the ones that happened to be allocating when there was
nothing left to allocate. The ESP8266 core largely does not check allocation
failure on these paths, so out-of-memory presents as a fault rather than an
error return.

The portal is the tightest the heap ever gets, and a rescan is its worst moment:

- the soft-AP allocates a pbuf per received frame, and cannot wait or fail
  gracefully;
- the driver holds a `bss_info` (~72 bytes) per scan result for the whole portal
  session — 30 results was ~2.2 KB pinned;
- `handleRoot()` built the entire page into a 2 KB `String`. Growing a String
  briefly holds the old and new buffers *at once* and leaves a hole behind, so
  the largest contiguous block shrinks faster than the free total suggests.

Three changes, in increasing order of importance:

1. **The page is streamed, not assembled.** `setContentLength(CONTENT_LENGTH_UNKNOWN)`
   makes the server use `Transfer-Encoding: chunked` for HTTP/1.1 clients, which
   keeps the connection open between chunks — the original reason for building it
   in one piece. Peak usage no longer scales with the number of networks found.
   A chunked response must be terminated by a zero-length chunk, or the browser
   waits for more and the page appears to hang.
2. **`kMaxScanResults` cut from 30 to 15**, since that memory stays occupied
   while the soft-AP needs it, and a longer list is not usefully browsable on a
   phone anyway. "Other (type below)" covers anything cut off.
3. **Heap instrumentation** (`logHeap()`) at scan completion, `/`, and `/rescan`.
   It reports the **largest contiguous block and fragmentation percentage**, not
   just the free total — fragmentation can starve a single large allocation while
   the total still looks healthy, which is exactly the failure mode here.

**Rule, now in `PLAN.md` §9: when a crash address moves between unrelated
allocating functions, stop reading the backtrace and measure the heap.**

> **Correction, added after the next crash.** The measurement this rule called
> for was taken — and it **disproved this section's conclusion**. Free heap was
> 39 KB with 3 % fragmentation at the moment of the crash, so memory exhaustion
> was not the cause. The streaming and cap below are still worth keeping on
> their merits, but the actual fault was RF timing contention; see the next
> section. The rule itself stands, and did its job: it is what produced the
> evidence that refuted the theory.

## The fifth crash: it was never memory — LED output vs. the radio

> ⚠️➡️✅ **The mitigation described here (quartering the refresh rate) did not
> fix the ESP8266.** The diagnosis was nonetheless correct: it has since been
> confirmed on ESP32-S3 hardware, where the same contention does not occur at
> all. See
> [the confirmed fix](#the-crash-that-is-still-unresolved-on-the-esp8266--confirmed-fixed-by-the-s3)
> for the current status. Read this section for the analysis that turned out
> to be right, not for a working ESP8266 fix — there isn't one, and one is no
> longer planned.

The heap instrumentation added for the previous crash **refuted its own
hypothesis**, which is exactly what it was for:

```
[net] root   free=39376 max_block=38440 frag=3%
[net] rescan free=39376 max_block=38440 frag=3%
Exception (0): epc1=0x40260dfd    ctx: sys
```

39 KB free, a 38 KB largest block, 3 % fragmentation — the heap was in excellent
shape. Memory was never the problem, and the "streamed page" work of the previous
section, while worth keeping, did not address the real cause.

The decoded stack says what does:

| Address | Symbol |
| --- | --- |
| `0x40260dfd` | `DefFreqCalTimerCB` |
| `0x402604e4` | `ppCheckTxIdle` |
| `0x40260719` | `pp_tx_idle_timeout` |
| `0x4026006f` | `ppPeocessRxPktHdr` |

Every one is the **PHY / RF layer**: RF calibration timing, TX idle timing, RX
packet handling. Not allocation, not the portal, not our code. This is the radio
missing its own deadlines.

### What was starving it

Two things, both of which look innocent:

1. **`Display::present()` drove the WS2812 strip every single frame.** The
   I2S/DMA method was chosen precisely because it is interrupt-safe, and that is
   still the right choice — but it is not free. It drives GPIO3 continuously for
   the length of the strip plus a reset gap, 60 times a second, competing with
   the radio for bus and timing. Worse, NeoPixelBus's `Update()` spins on
   **`yield()`** waiting for the previous transfer to drain — and `yield()` on
   the ESP8266 runs the SDK's scheduled functions, so the *render path* could
   re-enter the network stack.
2. **`net_.tick()` was gated to 60 Hz**, because it was only ever called from
   `update()`. The frame loop's deadline was being honoured; the radio's was
   not.

### The fix — and a sanctioned exception to §2

The user's suggestion was right: *"make an exception for rule 2 for network
discovery and connection."* Rule 2 ("nothing may block the frame loop") assumes
the frame loop is the only thing with a deadline. That holds for games and is
false for the WiFi stack, where a missed deadline is a fault inside the SDK
rather than a dropped frame.

Three changes:

- **`Scene::idle()` + `Engine::setIdleServiced()`.** A scene can opt in to being
  called on frames the engine *skips* — the idle time the 60 Hz gate would
  otherwise spin away. `NetworkScene` services the WiFi stack there. Note this
  does not let a scene block; it lets one be called *more often*. Games are
  untouched and still see a fixed timestep. The flag is cleared on every scene
  change, so it cannot be inherited.
- **`Display::setRefreshDivider()`.** While the radio is up, the network scene
  drops the strip to a quarter refresh rate. Still smooth enough to read the
  status animations, with a quarter as many DMA transfers contending with the
  PHY. `NetworkScene::exit()` restores it — otherwise every game started after
  visiting the scene would silently run at 15 fps.
- **`present()` never waits.** It returns early if `strip_.CanShow()` is false
  rather than letting NeoPixelBus spin on `yield()` inside the render path.
  Dropping a frame is invisible at 60 fps; re-entering the network stack from
  the middle of rendering is the class of bug that caused crash #2.

### Tested, and the tests were checked

Unlike the previous crashes, the *mitigation* here is host-testable even though
the crash is not. `test_display` gained four tests covering the divider: every
frame at 1, one-in-N at N, a zero divider clamped to 1 (a literal reading would
blank the tube permanently), and the reset that `exit()` depends on.

These were then **mutation-tested** — the divider check was deliberately broken,
and two of the four failed with `Expected 3 Was 12` and `Expected 1 Was 4`. A
test that cannot fail is not evidence.

### The lesson worth keeping

Three crashes were attributed to three different causes — scan-callback
lifetime, then heap exhaustion, then RF contention — and only the last is
supported by measurement. **The heap logging earned its place by disproving my
own theory.** The general rule, now in `PLAN.md` §9: when a crash lands in a
subsystem you did not write, instrument the resource you suspect *before*
changing code, and treat a decoded address as a hypothesis rather than a
diagnosis.

And the coda to that: even the measured diagnosis produced a fix that did not
work. Being right about the *cause* is not the same as being right about the
*remedy* — the divider reduced the contention without eliminating it. A
mitigation deserves its own confirmation, separately from the analysis that
motivated it.

## The crash that is still unresolved on the ESP8266 — confirmed fixed by the S3

> ✅ **Update: confirmed on hardware.** The ESP32-S3 DevKitC (see
> `esp32-s3-devkitc-1-n16r8` in `platformio.ini`) does **not** reproduce this
> crash. Rescanning and connecting repeatedly, with the strip refreshing every
> frame and no divider applied, has been stable. The three reasons below were a
> *prediction* when written; they are now a confirmed result, and the code has
> been reverted accordingly — `NetworkScene` no longer touches
> `setRefreshDivider()` at all, so the connection animations run at full rate
> exactly as they did before this mitigation existed. The mechanism itself
> (`Display::setRefreshDivider()`, `Scene::idle()`) is kept, tested, and unused
> on this scene rather than removed, in case contention ever reappears on other
> hardware.
>
> This is still open on the **ESP8266**, which remains a dev-only board going
> forward for anything involving the radio.

**Status on the ESP8266: open, deliberately unfixed.** Provisioning still
crashes there. The decision, taken deliberately, was to **stop working on this
until an ESP32 was in hand** rather than spend more effort on a platform the
project is leaving — and that decision paid off.

That was not giving up — it was a judgement about where the remaining bugs live.

### Why the S3 was expected not to have this problem — and did not

Three independent reasons, all confirmed by reading the driver sources rather
than from memory. Taken together they are the argument for waiting.

**1. The blocking wait is a real wait on the ESP32, not a re-entrancy trap.**
This is the important one. On the ESP8266, `NeoEsp8266DmaMethod.h` waits like
this:

```cpp
while (!IsReadyToUpdate()) { yield(); }
```

`yield()` on the ESP8266 runs the SDK's scheduled work, so *waiting for the LEDs
re-enters the network stack from inside the render path*. The ESP32 RMT method
instead calls:

```cpp
rmt_wait_tx_done(_channel.RmtChannelNumber, 10000 / portTICK_PERIOD_MS)
```

That is a FreeRTOS block. It suspends our task and hands the CPU to the
scheduler; the WiFi task then runs *as a task*, on its own stack, properly
preempted — not as a callback spliced into the middle of our call stack. The
failure mode is structurally absent, not merely less likely.

**2. RMT is a real peripheral for this job.** The ESP8266 has no WS2812
hardware, so NeoPixelBus repurposes **I2S**, which shares clocking and DMA
infrastructure the radio also depends on. RMT was designed to emit exactly this
kind of pulse train and is independent of the RF path. `board_config.h` already
carried this note next to the S3 typedef before the crash was understood.

**3. Two cores, and WiFi gets one.** On the S3 the WiFi stack normally pins to
core 0 while `loop()` runs on core 1, so rendering and the radio's deadlines
stop competing for the same CPU in the literal sense. The ESP8266 has one core,
no OS and cooperative scheduling: every microsecond spent rendering is a
microsecond the PHY does not get.

### ⚠️ Why this conclusion should have been distrusted until measured — and was

"It will be fine on the better hardware" was a *very* comfortable conclusion at
the time, and comfortable conclusions had been wrong three times in a row on
this bug already. It was also unfalsifiable until an S3 was in hand. **It has
now been measured, on the DevKitC, and held up** — but the caution was correct
process regardless of the outcome; treat this as a reminder to keep verifying
predictions rather than as license to skip that step next time because it
worked out here.

### What was decided, and what happened next

- **ESP8266-specific mitigation was abandoned deliberately**, rather than
  tuning divider values or staging DMA around association windows further on a
  platform being left behind.
- **`setRefreshDivider()` was kept** even though `NetworkScene` no longer calls
  it. It is cheap, host-tested, and remains the right escape hatch if
  contention ever reappears on some other board.
- **Provisioning on the ESP8266 is permanently known-broken** and is not
  planned to be fixed; the ESP8266 is now dev-only for anything not touching
  the radio.
- **The WiFiManager question is closed for now.** It was being considered
  specifically because its blocking portal was accidentally protective against
  this contention; with the S3 not needing that protection, there is no longer
  a reason tied to this bug to adopt it. See
  [Why not WiFiManager](#why-not-wifimanager) for the (still valid) reasons it
  was rejected in the first place.

### The experiment that was never needed

A cheap ten-minute test was proposed here — stop the strip completely during
provisioning and see if the crash still happens — as the fast way to tell
whether the RF-contention diagnosis was right before committing to a bigger
fix. It turned out not to be necessary: the S3 hardware arrived and settled the
question directly. It is recorded here as the fallback plan if the ESP8266
crash is ever revisited, or if a similar symptom shows up on different
hardware in the future.

## Why none of the crashes were caught before hardware

Both are worth being honest about, because they set the boundary of what the new
host tests (`pio test -e native`, see `test/README.md`) can do:

- The `sinf` crash was a stack overflow inside newlib. A PC's libm has no such
  limit, so it cannot be reproduced off-target at all.
- The second crash was WiFi driver re-entrancy. There is no driver on the host.
- The third was an SDK callback outliving a radio reconfiguration — again, no
  driver, no SDK task, and no callback on the host.
- The fifth was RF timing contention between the LED DMA and the radio. There is
  no radio and no DMA on the host.

The fifth is the interesting one, because although the *crash* is not
reproducible, its *mitigation* is: the refresh divider is ordinary logic, and
`test_display` now covers it — including a zero divider (which would blank the
tube if taken literally) and the reset that `NetworkScene::exit()` depends on.
Those tests were mutation-tested by deliberately breaking the divider, and two of
them failed as intended. **When a hardware fault cannot be tested, test the
mechanism that prevents it.**

So the tests do not try to reproduce either. They assert the *precondition* the
first fix depends on — that an animation phase stays small over 6 simulated
hours — and the second is prevented by structure and a documented rule rather
than by a test.

## Storage

Credentials live in `/net.cfg`, **separate from the game save**. Two reasons:
resetting highscores must not log you out, and a save-format version bump must
not drop the network config.

Written as two newline-terminated lines. A partial write is deleted rather than
kept, since a file that parses but connects to nothing looks like a hardware
fault to the user.

### When a failed connection *keeps* the credentials — and when it doesn't

The obvious rule is "don't store a network we couldn't reach". It is right about
the case that actually bites and wrong about most of the others.

The case it is right about is a mistyped password. Storing that leaves a Connect
entry that can never work, and the only escape is noticing Forget — the device
looks broken while faithfully retrying a key the router will keep rejecting.

But most failures say nothing about the credentials. A router rebooting, a
console carried out of range, a congested 2.4 GHz band: all transient, and all
would cost the user the entire phone-and-portal dance to recover from something
that fixed itself. So the rule has **two** conditions, in `net_policy.h`:

```
discard  ==  reason is kBadPassword  AND  the credentials are unproven
```

| Outcome        | Kept? | Why |
| -------------- | ----- | --- |
| `kBadPassword` | no\*  | The AP definitively rejected the key. Retrying cannot help. |
| `kNotFound`    | yes   | The AP was never heard. **A typo'd SSID is indistinguishable from being out of range**, and out of range is far more common. |
| `kTimeout`     | yes   | No verdict at all. Nothing to act on. |
| `kLost`        | yes   | We were associated a moment ago — positive proof the key is good. |

\* only while unproven.

#### Detecting a rejected password when the status word can't tell you

`WiFi.status()` cannot answer this, on either core:

- **Value 6 is `WL_WRONG_PASSWORD` on ESP8266 and `WL_DISCONNECTED` on ESP32.**
  Any numeric comparison means opposite things per platform.
- **`WL_CONNECT_FAILED` is not an authentication verdict.** On ESP32 it also
  covers `WIFI_REASON_ASSOC_FAIL` — an access point at capacity, for instance —
  so reading it as "wrong password" erases perfectly good credentials.
- **On ESP32 it is a cached value updated by events**, so straight after
  `WiFi.begin()` it can still describe the *previous* attempt.

So the verdict comes from the **disconnect reason** instead, captured in an
event handler and matched to the attempt by a **generation counter**. `connect()`
opens a new generation immediately before `WiFi.begin()`; the handler stamps
each verdict with the generation current when it arrived; `tick()` accepts only
verdicts stamped with the attempt it is waiting on. Ending an attempt sets the
generation to `kNoAttempt`, so events caused by our own teardown are ignored.

**A simple "attempt in progress" boolean is not enough**, which is why this uses
a counter. ESP32 queues events to a separate task, so a failure produced while
tearing down one attempt can be delivered *after* the next has begun — and with
a boolean it would be attributed to the new attempt. That is exactly the case
where the user has just corrected a password for the same SSID, so the failure
mode is "erases the password they finally got right".

The flags are `std::atomic<uint32_t>` for the same reason: on ESP32 the handler
runs on the WiFi event task and `tick()` on the loop task, so plain `bool`s
would be a data race with no guarantee `tick()` ever saw the write.

Only reasons that mean *the key was refused* count:

| Core | Treated as a rejection |
| --- | --- |
| ESP8266 | `REASON_AUTH_FAIL` (202), `REASON_4WAY_HANDSHAKE_TIMEOUT` (15) |
| ESP32 | `WIFI_REASON_AUTH_FAIL`, `WIFI_REASON_4WAY_HANDSHAKE_TIMEOUT` |

The **4-way handshake** is the important one: on ESP32 a wrong WPA key usually
fails there rather than with an explicit auth failure — the AP accepts
association, then goes quiet once it sees the wrong MIC. Reaching the handshake
at all proves association succeeded, so the key is the only thing left that can
be wrong.

The list is deliberately narrow, because a false positive erases a good
password. Excluded: `ASSOC_FAIL` and neighbours (capacity and compatibility, not
credentials); `AUTH_EXPIRE` (a *previous* authentication lapsed, not this one
refused); `MIC_FAILURE` and the generic `HANDSHAKE_TIMEOUT` (204), both
reachable through interference, packet loss and key rotation.

**The verdict is read at the deadline, not the instant it arrives.** The ESP32
core retries the first disconnect internally regardless of reason, so acting on
the first event would cut short a retry that might have succeeded — and erase
valid credentials over one flaky handshake. Waiting also lets any late-queued
event land before the decision is made.

Everything else stays `kTimeout` and keeps the credentials. A weak or
intermittent AP, one that is full, one that disappears after the scan, or a
successful association that then fails through DHCP all land there — none is
evidence about the password.

`unproven_` is the second axis. It is set when credentials arrive from the
portal and cleared the moment a connection succeeds. Once a network has worked
even once, a later rejection more likely means the user changed the password on
the router than that the console should quietly erase its own copy — Forget
stays the deliberate way out.

**`unproven_` is never persisted.** After a reboot there is no way to tell a
never-tested network from a working one the user is merely away from, so
anything that survived a restart is treated as proven. Guessing the other way
would erase good configurations.

**But `Network::begin()` runs on every scene entry, not once at boot**, so it
guards that reset behind a `began_` flag. Without it, a user who typed a bad
password, backed out to the launcher and walked back in would find the
credentials silently promoted to "proven" — and the discard rule would never
fire again until a power cycle.

Two consequences worth knowing:

- A rejection is **not** acted on the moment it arrives; the decision is made at
  the 20 s deadline. See the note on the ESP32 core's internal retry above.
- When credentials *are* discarded, `NetworkScene` moves the selection to
  Setup, because the Connect entry the user was sitting on no longer exists.

**The erase must succeed before the credentials are cleared from RAM.** If
`LittleFS.remove()` fails and RAM is cleared anyway, the menu drops to Setup
while the file quietly survives — and the next scene entry calls
`loadCredentials()` and brings the same unusable network straight back, now
looking proven. Keeping them on failure is the lesser evil: Forget still works,
and the device is not telling the user something false. Serial logging is not a
substitute here, because it is invisible on the handheld.

### ⚠️ `wl_status_t` value 6 means different things on the two cores

`WL_WRONG_PASSWORD = 6` on the ESP8266; on the ESP32 that same value is
`WL_DISCONNECTED`, and `WL_WRONG_PASSWORD` does not exist at all. This is one of
the reasons `classifyFailure()` derives its verdict from **disconnect events**
rather than from `wl_status_t`; the status word is used only for
`WL_NO_SSID_AVAIL`, which carries no implication about the key.

This is the fourth silent divergence between the two platforms in this project,
after the C++11/17 aggregate initialisation issue, the `DNSServer` by-reference
port, and the async-scan suspend. **Build both targets before believing any WiFi
change.**


## ⚠️ Security: the transport is not authenticated yet

`ota.cpp` calls `setInsecure()`. The connection is encrypted but **the server is
not authenticated**, so anyone able to redirect DNS could serve their own
firmware. Not acceptable for a shipping device, and flagged as the item to fix
before Phase 9.

**Certificate pinning is the wrong fix.** Certificates expire. A console left in
a drawer for two years would be unable to update itself precisely when it most
needs to — a permanent, unfixable brick of the update path.

**Image signing is the right fix.** The Arduino `Update` library supports it
natively: the public key is compiled into the firmware, and the signature is
verified before the boot pointer is flipped. A forged image is rejected even if
it arrives over plain HTTP from a hostile server. Signatures do not expire, so
this also works on a device that has been offline for years.

It would additionally let the ESP8266 **drop TLS entirely** for the download and
reclaim ~20 KB of heap. Deferred to Phase 9 only because it needs a signing key
and a release pipeline, neither of which exists yet.

## Memory

TLS is the expensive part on the ESP8266: BearSSL wants ~16–22 KB of heap for a
handshake against the ~46 KB free. It fits only because OTA runs from the
launcher with no game loaded, and because the RX buffer is reduced to 1 KB.

That 1 KB is below the 16 KB maximum TLS record size and works only because
servers negotiate down via the `max_fragment_length` extension. **A server that
refuses that extension will fail at the handshake** — which from the tube looks
identical to a failed download, so the connected-state log line prints free heap
to make the distinction diagnosable.

### The portal, not TLS, is where the heap actually ran out

TLS is the largest *single* allocation, but the crashes happened in the portal,
which is a harder environment: the soft-AP is receiving frames (a pbuf each),
scan results are pinned in driver memory, and an HTTP response is being built —
all at once, with no ability to wait. The mitigations are listed under "The
fourth crash" above: the setup page is streamed rather than assembled,
`kMaxScanResults` is capped at 15, and `logHeap()` reports the largest
contiguous block so fragmentation is visible before it becomes a fault.

Static cost of this phase:

| Board | Flash before | Flash after |
| --- | --- | --- |
| ESP8266 | 31.1 % | 46.5 % |
| ESP32-S3 | 17.5 % | 48.4 % |

## Build & flash

```powershell
cd beam-boy-hw
& "$env:USERPROFILE\.platformio\penv\Scripts\pio.exe" run -e nodemcuv2-10px -t upload
& "$env:USERPROFILE\.platformio\penv\Scripts\pio.exe" device monitor

# Interim ESP32 target while waiting for the Feather (10 px strip):
& "$env:USERPROFILE\.platformio\penv\Scripts\pio.exe" run -e esp32-s3-devkitc-1-n16r8 -t upload
```

## What to look for

**Offline behaviour is untouched — test this first.** With no credentials stored,
the console must boot straight into the launcher with no delay and no scanning.
If boot got slower, something is wrong.

1. **Portal:** open Network → Setup. Tube breathes amber. Join `BeamBoy-Setup` on
   your phone. The form should auto-open; if not, browse to `192.168.4.1`.
2. Submit real credentials. Expect the confirmation page, then a blue sweep, then
   a green wipe.
3. **Reconnect:** leave the scene and come back. It should now offer Connect, and
   connecting should take a few seconds.
4. **Wrong password:** enter a bad one deliberately. Expect **two** red flashes
   after the 20 s timeout — the wait is intentional, so the ESP32 core's own
   internal retry is not cut short. Then leave the scene and come back: only
   **Setup** should be offered, because the bad credentials were discarded.
   Serial logs `password rejected -- discarding credentials`. **Test this on
   both boards** — the reason codes differ, and ESP32 typically reports a 4-way
   handshake timeout rather than an explicit auth failure.
5. **Wrong SSID:** enter a network name that does not exist. Expect **four**
   flashes after the timeout, and — the opposite of the case above — the
   credentials **kept**, so Connect is still offered. This asymmetry is
   deliberate; see "When a failed connection keeps the credentials".
6. **Out of range, on a proven network:** connect successfully once, then power
   the router off (or walk out of range) and reconnect. Expect three flashes and
   the credentials **kept**. This is the case that "never store a failed
   network" would have broken.
7. **A busy or flaky AP must not count as a rejection.** If you can arrange an
   association failure that is not an auth failure — an AP at its client limit
   is the easiest — confirm the credentials are **kept**. `WIFI_REASON_ASSOC_FAIL`
   is deliberately not in the rejection list; if a valid network gets erased
   here, that list has been widened wrongly.
8. **Correcting a password must not be sabotaged by the previous attempt.** Get
   the password wrong, let it fail, then immediately go through Setup again and
   enter the *correct* password for the same network. It must connect. If it
   fails and erases the good password, the generation correlation in
   `onDisconnected()` has regressed — this is the exact case a plain
   "attempt in progress" boolean gets wrong.
9. **Re-entry does not launder a bad password:** enter a wrong password, but
   instead of waiting, press B to leave the scene and walk straight back in.
   Then Connect. The credentials must **still** be discarded on failure — if
   they survive, the `began_` guard in `begin()` has regressed.
10. **Cancel:** press B mid-connect. Radio must drop immediately.
11. **Exit while connected:** leave the scene with B held to exit. Serial should
    show no further network activity.
12. **Forget:** select it, confirm the entry starts blinking fast, press again.
    Expect a red wipe, and only Setup left in the menu. Then check a **single**
    press followed by navigating away does *not* erase.
13. **Forget while connected:** connect first, then forget. The radio must drop —
    not stay associated to the network just erased.
14. **Scan:** in the portal, use **Scan again** and confirm the list reloads. The
    tube should show a white dot sweeping the amber while it runs. Tap the link
    several times quickly; nothing should crash or hang.
15. **The second-crash repro, specifically:** open the portal and join
    `BeamBoy-Setup` from the phone **as fast as you can**, before the first scan
    has finished. This is what previously crashed with `rst cause:2`. The AP must
    stay up and the page must load. Repeat a few times — the old failure was a
    race and did not fire every attempt.
16. **The third-crash repro (was near-100% reproducible):** open the portal, wait
    for the network dropdown to populate, then submit the form and let it
    connect. Do this several times, including submitting immediately after
    tapping **Scan again** so a scan is still in flight when the credentials are
    saved. The device must connect without an `Exception (0)` in `ctx: sys`.
    A crash here means the `!scan_pending_` guard on the `kPortalSaved`
    transition has regressed.
17. **The fourth-crash repro (rescan under memory pressure):** open the portal
    and tap **Scan again** several times in a row, reloading the form between
    taps. Watch the serial log — every scan, `/` and `/rescan` now prints
    `free=`, `max_block=` and `frag=`. **`max_block` is the number that matters**,
    since a large allocation fails when no single block is big enough even if
    `free` still looks comfortable. If `max_block` trends downward across
    repeated rescans, something on that path is fragmenting the heap and will
    eventually fault in an unrelated core function.
18. ~~**The fifth-crash repro (RF contention):**~~ **✅ Confirmed fixed on ESP32,
    permanently broken on ESP8266.** Opening the portal, rescanning and
    connecting repeatedly on the `esp32-s3-devkitc-1-n16r8` environment, with
    the strip refreshing every frame, produces no `Exception (0)` in `ctx: sys`
    and no PHY symbols in any stack. The ESP8266 still faults here and is not
    expected to be fixed; see
    [the confirmed fix](#the-crash-that-is-still-unresolved-on-the-esp8266--confirmed-fixed-by-the-s3).
19. **The divider is restored on exit:** `NetworkScene` no longer sets a divider
    at all, so this item is now moot for that scene — recorded here in case
    `setRefreshDivider()` is reused elsewhere. If some future scene calls it,
    verify leaving that scene and starting a game runs at full 60 fps, not a
    quarter rate. `test_display` still covers the mechanism itself.

⚠️ **Items 1–17 assume the ESP8266.** On that board they are only meaningful up
to the point where provisioning crashes; everything not involving an active
radio — the menu, forget, offline boot, the tube's state colours — is
unaffected. On the ESP32-S3, all 19 items are expected to pass in full.

Before flashing, run the host tests — they take about 13 seconds and cover the
input and animation logic:

```powershell
& "$env:USERPROFILE\.platformio\penv\Scripts\pio.exe" test -e native
```

**OTA cannot be tested end-to-end yet** — it needs a manifest at a real URL.
`kManifestUrl` in `ota.cpp` is a placeholder. Pressing A while connected will
report a failure, which is the correct behaviour for an unreachable server.

## Still to do in this phase

- [ ] ~~Provisioning crashes on the ESP8266~~ — **confirmed fixed on ESP32-S3,
      permanently unfixed on ESP8266.** No further action; see the status
      banner at the top of this document.
- [ ] Image signing (see the security section) — **before any real release**
- [ ] A real manifest URL and release pipeline
