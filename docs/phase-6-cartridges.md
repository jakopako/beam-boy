# Phase 6 — Games as scripts

Goal: prove the cartridge model end to end -- a game written in Berry, running
through a bound `beam` API, indistinguishable in play from its native
equivalent. Reflex was the target: small enough to port completely in one
sitting and reason about at a glance (see the "natural first cartridge" note
in docs/phase-5-vm-bakeoff.md).

## What exists now

- **`src/vm/beam_api.h`/`.cpp`** -- the entire surface a script can see. One
  `beam` module, bound once per `ScriptScene::enter()`. No cartridge touches
  `Engine`/`Display`/`Input`/`Storage` directly; everything goes through a
  bound function here. That is deliberate: policing this one file is what
  makes sandboxing (Phase 6 item 4, not yet done) tractable later, instead of
  auditing every cartridge that ever ships.
- **`src/vm/script_scene.h`/`.cpp`** -- a generic `Scene` that owns a Berry VM,
  loads one script's source, and calls `init()` / `update(dt)` / `render()` on
  it exactly the way `Engine` calls a native `Scene`. One scene class, reused
  for every scripted game -- adding a second cartridge means adding a second
  `ScriptScene` instance with different source, not a new C++ class.
- **`src/scenes/reflex_script.cpp`** -- Reflex, ported line-for-line from
  `scenes/reflex_scene.cpp`. Registered in the launcher as **"Reflex (VM)"**,
  a separate entry (and highscore id) from the native "Reflex", so the two can
  be played side by side rather than one replacing the other.
- **`src/core/cartridge_store.h`/`.cpp`** -- scans `/games/` on LittleFS at
  boot and merges what it finds with the built-in registry into one list
  (`gameList()`). This is what makes an installed game *data*: the launcher,
  the engine's score filing and `beam.highscore()` all read the merged list and
  none of them know which entries came from flash.
- **`src/core/json_lite.h`/`.cpp`** -- a deliberately strict reader for
  `meta.json`. See "Parsing metadata" below for why it is hand-rolled.
- **`data/games/reflexfs/`** -- the same Reflex script as an actual installed
  cartridge, to exercise the filesystem path alongside the baked-in one.

## Cartridges on the filesystem

Layout, one folder per game (full format notes in `beam-boy-hw/data/README.md`):

```
/games/<id>/meta.json    { "id": "...", "title": "...", "color": "ff8000" }
/games/<id>/game.be      Berry source, loaded on launch
```

Decisions worth recording, because each one is a trade rather than an obvious
choice:

- **The folder name is the id, not `meta.json`'s `id` field.** The filesystem
  already makes folder names unique, so ids are unique for free. Trusting the
  file would let two cartridges claim one id and silently share a highscore
  slot. The `id` field is kept in the file for readability and ignored.
- **A cartridge may not take a built-in's id.** Folder uniqueness only stops
  two *cartridges* colliding -- nothing stopped a downloaded folder called
  `reflex` from shadowing the built-in and reading/overwriting its highscore.
  `loadMeta()` rejects those explicitly. This mattered enough to catch in
  review: once `/games/` is fed from the internet, it is an untrusted-content
  bug, not a hypothetical.
- **Built-ins come first in the launcher, cartridges after.** `Storage` keeps
  the last-played *index*, so a stable ordering keeps the console reopening on
  the game you actually last played after an install.
- **Script source is not held resident.** `game.be` is read on launch and freed
  on exit; only metadata stays in RAM. So the number of installed games is
  bounded by flash, not RAM, and editing a script + `uploadfs` picks up the
  change with no firmware flash.
- **`Storage::kMaxScores` had to grow from 12 to 24.** Built-ins and cartridges
  compete for one score table; 6 built-ins + 12 cartridges could exceed 12
  distinct ids, and the 13th game's first record would have been silently
  dropped. This bumps the save version, so existing highscores reset once.

### Parsing metadata

`meta.json` is read by a ~100-line hand-rolled parser rather than ArduinoJson.
It accepts *only* a flat object of string values -- no nesting, numbers,
arrays, bare literals, or `\u` escapes -- and rejects anything else outright.

Strictness is the feature. These files will arrive from the games repo as
community submissions, and the safest parser is one that cannot be talked into
doing something interesting: no recursion (so no stack to blow), every write
bounded by an explicit buffer size, and over-long values *rejected* rather than
truncated, so a bad cartridge is absent from the launcher instead of present
and subtly wrong. `test/test_cartridge_meta/` covers the malformed inputs
specifically, which is where a hand-rolled parser earns or loses its keep.

A malformed cartridge is skipped with a serial log line; it never prevents the
other cartridges, or the console, from working.

## The `beam` API surface

Matches PLAN.md's cartridge API sketch, with two small, deliberate additions
found necessary while actually porting a game (`highscore`, `raw_pixel`):

```
beam.clear()
beam.pixel(pos, color, intensity)        -- color is a packed 0xRRGGBB int
beam.line(from, to, color, intensity)
beam.fade(amount)
beam.pixel_count() / beam.pixel_width()

beam.wheel()                              -- navDelta(), accumulated steps
beam.stick()                              -- stickX(), -1..1
beam.pressed("a"|"b"|"stick") / beam.held(...)

beam.time()                               -- ms since the scene was entered
beam.random(n) / beam.random(lo, hi)

beam.score(add)                           -- accumulate; engine files it on exit
beam.highscore()                          -- NEW: read this cartridge's own highscore
beam.show_score(elapsed_ms)               -- the engine-drawn binary reveal
beam.raw_pixel(index, color, intensity)   -- NEW: bypass normalised coords, for UI chrome (life dots)
beam.exit()                               -- return to launcher

beam.log(msg)                             -- Serial, for cartridge development
```

**Colours cross the boundary as one packed `0xRRGGBB` int**, not three
separate r/g/b arguments. This matches how a script author would actually
write a colour literal, and avoids the temptation to build a lightweight
Berry object per colour (Berry has no value-type struct; anything richer than
an int is a heap allocation).

**Why `raw_pixel` and `highscore` weren't anticipated:** the plan's sketch
assumed a game only ever draws in normalised space and only ever writes its
own score. Reflex's life indicators use `Display::rawPixel` (fixed-index UI,
not a game-space draw) and nothing in the original sketch let a script read
its own highscore back for e.g. showing "beat your best" text later. Both
gaps were found by attempting a real, complete port rather than assuming the
sketch was final -- exactly the value of doing Reflex first.

## Findings from the port

- **The API boundary held.** No script needed anything from `Engine` that
  wasn't already exposed, and nothing had to reach back into C++ beyond the
  bound functions. That is the actual pass/fail signal for this phase, not
  just "does it play the same."
- **Berry comment syntax is `#`, not Lua's `--`.** An easy mistake when
  sketching scripts by analogy to Lua (which Berry otherwise resembles
  closely) -- caught immediately as a parse error, not a subtle bug.
- **No per-scene instance state in Berry.** A script's `var`s at file scope
  are its only persistent state across `update`/`render` calls -- matching
  how the native scene's member fields work, just without a `this`.
- **Played identically on hardware.** User confirmed: no distinguishable
  difference in feel from the native Reflex, no bugs found in manual testing.

## Open items (not yet done)

Phase 6 listed five items; three are complete:

1. ✅ Expose the full Beam API to the VM.
2. ✅ Port Reflex from C++ to a script. (Wormfight, the more complex game, not
   yet ported -- Reflex was chosen deliberately as the smaller first cut.)
3. ✅ Launcher enumerates `/games/*/meta.json` from LittleFS -- installed games
   are *data*, merged with the built-in registry into one list. Installing a
   game is now a filesystem write, which is exactly the hook Phase 7's
   downloader needs.
4. ⬜ **Sandboxing:** cap script memory and enforce a per-tick instruction
   limit so a buggy cartridge cannot hang the console. Nothing in
   `ScriptScene` currently prevents an infinite loop in `update()` from
   freezing the frame loop -- this is a real gap, not a nice-to-have, and it
   matters *more* now that cartridges can come from the filesystem rather than
   only from code in this repo.
5. ⬜ Dev quality-of-life: push a script over serial/HTTP and hot-reload it,
   so iterating on a cartridge takes seconds rather than a flash cycle.
   Partially eased already: `uploadfs` reloads a script without a firmware
   flash, and the source is re-read on every launch.

## Status: cartridges load from storage; sandboxing is the remaining gap

The hard question Phase 5 and this first port were meant to answer --
*can a script cartridge actually replace a native game without the player
noticing* -- is answered yes, and games now load from the filesystem rather
than from firmware.

The honest remaining risk is **item 4**. A cartridge is now data that can
arrive from outside this repo, but nothing yet stops a bad one from hanging the
console. That should be closed before anything is installed from the network.
