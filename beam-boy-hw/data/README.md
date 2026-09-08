# Beam Boy — installed cartridges

Everything under this folder is written to the LittleFS data partition and is
**data, not firmware**. Uploading it is a separate step from flashing:

```
pio run -e esp32-s3-devkitc-1-n16r8 -t uploadfs
```

## Layout

One folder per game, under `games/`. The folder name is the game's id:

```
games/
  reflexfs/
    meta.json     metadata the launcher reads at boot
    game.be       the Berry source, loaded when the game is launched
```

The **folder name is the id**, not `meta.json`'s `id` field. The filesystem
already guarantees folder names are unique, so ids are unique for free —
trusting the file instead would let two cartridges claim the same id and
silently share a highscore slot.

Ids are capped at 11 characters, matching the storage key length. A longer one
is refused at scan time rather than failing later the first time someone sets a
record.

## meta.json

A flat object of string values. Nothing nested, no numbers, no arrays — the
parser (`src/core/json_lite.h`) rejects anything else outright, because these
files will eventually arrive from the games repo as community submissions.

| Field | Required | Meaning |
|---|---|---|
| `id` | no | Ignored; the folder name wins. Kept for readability. |
| `title` | no | Launcher name. Falls back to the id, so a missing title never hides a game. |
| `color` | no | Accent as `rrggbb` or `#rrggbb`. Defaults to white. A malformed value rejects the cartridge, rather than showing black and looking like a launcher bug. |

Unknown keys are ignored, so adding a field later (author, version) won't break
consoles running older firmware.

## Notes

- A cartridge with no readable `meta.json`, no `game.be`, or malformed metadata
  is skipped with a line on the serial console. It does not appear in the
  launcher, and it does not stop the other cartridges from loading.
- `game.be` is read on launch and freed on exit, not held resident — an
  installed game that isn't being played costs metadata only, so how many you
  can install is bounded by flash rather than RAM.
- Because the file is re-read on every launch, editing `game.be` and running
  `uploadfs` picks up the change without a firmware flash.
- `games/reflexfs/game.be` is currently a copy of the script embedded in
  `src/scenes/reflex_script.cpp`, kept so the built-in and filesystem paths can
  be compared side by side in the launcher.
