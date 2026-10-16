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
  reflex/
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
| `description` | no | One or two short sentences describing the game and its controls, for a future website game list. Keep it within 96 UTF-8 bytes to fit the metadata parser. The firmware ignores this field; it is not included in the device's store index or saved by Store installs. |
| `color` | no | Accent as `rrggbb` or `#rrggbb`. Defaults to white. A malformed value rejects the cartridge, rather than showing black and looking like a launcher bug. |
| `sha256` | no | The hash the Store verified `game.be` against at install time. Written automatically by the Store; a hand-authored or `uploadfs`-copied cartridge normally omits it. Not re-verified against `game.be` — used only so the Store can tell whether an installed cartridge matches what the index currently offers ("update available"). A malformed (present but not 64 hex digits) value rejects the cartridge, like `color`. |

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
- Fishing is available from the store in `website/games/fishing/`; it is no
  longer bundled in this local filesystem image.
- `games/obstacles/` is a **local-only prototype**, not yet in the store.
  The white player stays near the controller end. Press **A** for a timed
  jump over **orange** obstacles; hold **B** to duck **cyan** obstacles.
  The player stays the same size and position: white at 25% intensity when
  standing, orange at full intensity while jumping, and cyan at 10% while
  ducking. These intensities remain subject to the device's brightness cap.
  Jump takes priority if both buttons are pressed.
  Life indicators use the shared `beam.show_lives()` API: turquoise at 40%
  intensity at the far end. Flash the updated firmware before uploading these
  cartridges; older firmware does not provide this API.
  Three lives, one point per obstacle cleared; speed increases with clears.
  Up to four obstacles can approach together, entering gradually from beyond
  the far end rather than appearing all at once. Orange obstacles vary in length
  but take at most 0.28 seconds to pass, within the 0.65-second jump. Cyan
  obstacles can be longer; hold B until their trailing edge clears the player.
  Points are awarded only after the entire obstacle passes. Gaps allow time
  to land, recover and switch actions even as the game speeds up.
  A restarts after game over. The stick's normal pause/exit controls still apply.

To test Obstacles on the Feather, run from `beam-boy-hw`:

```powershell
pio run -e adafruit_feather_esp32s3_nopsram -t uploadfs
```

**This replaces the whole LittleFS data image**, not just Obstacles: back up
anything you need first. Store-installed games and saved settings/highscores
not included in this folder will be overwritten. Reset the board afterwards
so the launcher rescans the cartridges.

### Obstacles host tests

The tests execute the actual cartridge in the vendored Berry VM, with mocked
input and drawing. They cover both obstacle types, full-length collisions,
jump timing, duck release, scoring, lives, restart, queue spacing and 30,000
frames, including successful play through the full speed ramp.
From the repository root, with GCC on PATH:

```powershell
$sources = Get-ChildItem beam-boy-hw\lib\berry\src\*.c |
    Where-Object { $_.Name -ne 'be_filelib.c' } |
    ForEach-Object { $_.FullName }
$runner = Join-Path $env:TEMP (([System.IO.Path]::GetRandomFileName()) + '.exe')
try {
    gcc -std=c99 -O2 -DCOMPILE_BERRY_LIB `
        -Ibeam-boy-hw\lib\berry\generate -Ibeam-boy-hw\lib\berry\src `
        $sources tools\test_cartridge.c -o $runner
    if ($LASTEXITCODE -ne 0) { throw 'Cartridge test build failed' }
    & $runner tools\obstacles_test.be beam-boy-hw\data\games\obstacles\game.be
    if ($LASTEXITCODE -ne 0) { throw 'Cartridge tests failed' }
} finally {
    if (Test-Path -LiteralPath $runner) { Remove-Item -LiteralPath $runner }
}
```

## Store index

Phase 7 adds a network store. The firmware fetches one `index.json` and writes
the selected entry into the same `games/<id>/` layout above, so downloaded games
and `uploadfs` games are identical after installation.

The first supported index format is deliberately strict and small:

```json
{
  "games": [
    {
      "id": "reflex",
      "title": "Reflex",
      "color": "ff00aa",
      "url": "https://example.test/games/reflex/game.be",
      "sha256": "0123456789abcdef0123456789abcdef0123456789abcdef0123456789abcdef",
      "size": "1234"
    }
  ]
}
```

All values are strings. `id`, `title`, and `color` become the installed
`meta.json`; `url`, `sha256`, and `size` are used only while downloading and
verifying `game.be`. See `../../docs/phase-7-store.md` for limits and controls.
