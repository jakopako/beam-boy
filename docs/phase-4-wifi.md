# Phase 4 — WiFi, opt-in

Networking that stays out of the way. The radio is **off** unless you walk into
the Network scene and ask for it, and everything here is built so that offline
behaviour is not merely the fallback but the default.

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
| Failed | Red | Three sharp flashes, then a dim hold |
| OTA downloading | Amber | Static progress bar |
| OTA installed | Green | Fills from both ends, then pulses white at centre |
| OTA up to date | Blue | Steady dim |

The selected menu entry breathes while the others sit dim and still — brightness
and motion mark the selection. All entries are drawn the same width: an earlier
version shrank the unselected ones, which made them read as glitches rather than
choices (see below).

Menu entries sit at **fixed positions**, so the same action is always in the same
place. Connect and Forget are hidden entirely until credentials exist.

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

## Storage

Credentials live in `/net.cfg`, **separate from the game save**. Two reasons:
resetting highscores must not log you out, and a save-format version bump must
not drop the network config.

Written as two newline-terminated lines. A partial write is deleted rather than
kept, since a file that parses but connects to nothing looks like a hardware
fault to the user.

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
4. **Wrong password:** enter a bad one deliberately. Expect three red flashes
   after the 20 s timeout — not a hang.
5. **Cancel:** press B mid-connect. Radio must drop immediately.
6. **Exit while connected:** leave the scene with B held to exit. Serial should
   show no further network activity.
7. **Forget:** select it, confirm the entry starts blinking fast, press again.
   Expect a red wipe, and only Setup left in the menu. Then check a **single**
   press followed by navigating away does *not* erase.
8. **Forget while connected:** connect first, then forget. The radio must drop —
   not stay associated to the network just erased.
9. **Scan:** in the portal, use **Scan again** and confirm the list reloads. The
   tube should show a white dot sweeping the amber while it runs. Tap the link
   several times quickly; nothing should crash or hang.

**OTA cannot be tested end-to-end yet** — it needs a manifest at a real URL.
`kManifestUrl` in `ota.cpp` is a placeholder. Pressing A while connected will
report a failure, which is the correct behaviour for an unreachable server.

## Still to do in this phase

- [ ] Image signing (see the security section) — **before any real release**
- [ ] A real manifest URL and release pipeline
