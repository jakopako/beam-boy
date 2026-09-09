# Phase 7 — The store

Goal: download script cartridges from the internet into `/games/<id>/` so they
appear in the launcher without a firmware flash.

## First slice implemented

- **Store scene:** `../beam-boy-hw/src/scenes/store_scene.h`/`.cpp`, registered
  in the launcher as **Store**. It is a utility scene, not a game: entering it
  is the deliberate user action that turns WiFi on.
- **Credentials:** Store reuses the existing `Network` state machine. It does
  not open the captive portal itself; if there are no stored credentials it fails
  and the user should visit **Network** first.
- **Index fetch:** the scene fetches `BEAMBOY_STORE_INDEX_URL`, which defaults
  to the GitHub Pages store index:
  `https://jakopako.github.io/beam-boy/games/index.json`. In normal builds the
  index must be HTTPS and is validated against the embedded Mozilla/certifi CA
  bundle in `beam-boy-hw/cert/x509_crt_bundle.bin`, because the index is the
  trust root for every script hash. Override the URL with a PlatformIO build
  flag for local mirrors or a future separate games repo:

  ```ini
  build_flags =
      ${env.build_flags}
      -DBEAMBOY_STORE_INDEX_URL=\"https://example.test/games/index.json\"
  ```

  For local LAN-only experiments, compile with
  `-DBEAMBOY_STORE_ALLOW_INSECURE_INDEX=1`; the firmware logs a warning and may
  then fetch an unauthenticated HTTP index or skip HTTPS certificate checks.
  Do not use that flag for a public store.

- **Strict index parser:** `../beam-boy-hw/src/core/store_index.h`/`.cpp`, with
  native tests in `../beam-boy-hw/test/test_store_index/`. It accepts one small
  JSON shape and rejects malformed, nested, over-long, path-like or over-sized
  entries.
- **Install path:** selecting a store entry with **A** downloads `game.be` to a
  temporary file, checks the exact byte count and SHA-256, writes `meta.json`,
  then renames both into `/games/<id>/`. Existing installs are protected with
  `.bak` files during the final rename so a failed update restores the previous
  copy.
- **Immediate availability:** after a successful install, `CartridgeStore` is
  rescanned and `gameList()` is rebuilt, so the new cartridge is available in
  the launcher without rebooting.
- **Update indicator:** each installed cartridge's `meta.json` now also records
  the `sha256` verified at install time. After every index fetch and every
  install, the Store recomputes, per index entry, whether it is not installed,
  installed and current, or installed with a different hash than the index
  offers ("update available"). Status is shown as brightness/animation on the
  same coloured block, not a new colour, since colour is reserved for game
  identity: dim = up to date, breathing = update available, normal = not
  installed. Pressing **A**/nav on an up-to-date entry is a harmless no-op (a
  quick green flash) rather than re-downloading. A cartridge with no recorded
  hash (hand-authored, or copied in via `uploadfs`) can only ever read as "not
  installed" or "up to date" by id — never "update available", since there is
  no trustworthy prior hash to compare against.
- **Deleting a cartridge:** in the launcher, holding **B** on an installed
  cartridge past the highscore readout (~2.5 s total) deletes it: `game.be`,
  `meta.json` and its `/games/<id>/` folder are removed, cartridges are
  rescanned, and `gameList()` is rebuilt immediately. The readout bleeds from
  the normal highscore display toward solid red as the hold approaches the
  threshold, so the deletion is never a surprise. Built-in games and the
  Store/Network utility entries are not deletable this way — the gesture only
  fires for entries `GameList::build()` populated from `CartridgeStore`.

## Store index format

All values are strings by design, matching the strict `meta.json` approach:

```json
{
  "games": [
    {
      "id": "reflexfs",
      "title": "Reflex",
      "color": "ff00aa",
      "url": "https://example.test/games/reflexfs/game.be",
      "sha256": "0123456789abcdef0123456789abcdef0123456789abcdef0123456789abcdef",
      "size": "1234"
    }
  ]
}
```

Constraints:

- `id`: 1-11 characters, only letters, digits, `-`, `_`. It becomes the folder
  name under `/games/`, so path separators and `..` are rejected.
- `title`: max 23 characters.
- `color`: exactly six hex digits, `rrggbb`.
- `url`: max 159 characters, must begin with `https://` or `http://`.
- `sha256`: exactly 64 hex digits.
- `size`: decimal string, 1 to 32768 bytes.
- At most 12 entries are accepted in one index.

## Publishing a cartridge to the store

`docs/games/index.json` is the trust root for every downloaded script: the
device verifies each `game.be` against the `sha256`/`size` recorded there. That
means the recorded hash/size must match the *exact bytes* GitHub Pages serves —
not whatever a local editor/OS happens to have on disk. Windows checkouts in
particular can silently reintroduce CRLF line endings, which changes the bytes
(and therefore the hash) without changing how the script reads.

To avoid computing/copying hashes by hand, use
[`tools/build_store_index.py`](../tools/build_store_index.py):

1. Add or update a cartridge folder under `docs/games/<id>/`, containing:
   - `game.be` — the Berry script, LF line endings only.
   - `meta.json` — `{"id": "<id>", "title": "...", "color": "rrggbb"}`.
2. Run `python tools/build_store_index.py` from the repo root. It reads each
   `game.be` directly, computes `sha256`/`size` from those bytes, and rewrites
   `docs/games/index.json` deterministically.
3. Commit `docs/games/**` and the regenerated `index.json` together, then push.

The script refuses to run if a `game.be` contains CR bytes, so a CRLF
regression is caught before it's published rather than causing a confusing
"size mismatch" on the device later. Run `python tools/build_store_index.py
--check` to verify the index is already up to date without writing anything
(useful before committing or in CI).

[`.gitattributes`](../.gitattributes) marks `*.be` and cartridge `meta.json`
files as binary (`-text`) so Git never rewrites their line endings on checkout
or commit, regardless of a contributor's `core.autocrlf` setting.

## Controls and tube vocabulary

- Enter **Store** from the launcher.
- Blue sweep: connecting to stored WiFi.
- Amber sweep: fetching the index.
- Store entries: coloured blocks from the index; selected block breathes.
  Status overlays brightness: dim = already installed and current, breathing
  (independent of selection) = update available, normal = not installed.
- **A** / nav press: install selected cartridge, or a quick green flash and
  no-op if it is already up to date.
- White bar: install in progress.
- Green centre-out flash: install succeeded.
- Red pulse: failure; read the serial log for the precise reason.
- Hold **B**: return to the launcher, using the existing utility-scene exit
  gesture.

In the **launcher**, holding **B** on an installed cartridge past the
highscore readout deletes it; see "Deleting a cartridge" above.

## Current limitations

- The default URL points at this repo's GitHub Pages site. It will return 404
  until GitHub Pages is enabled/deployed for the repository.
- Downloads are blocking once started. The scene paints a static "working" frame
  first, the same pattern used by OTA. This is acceptable for the first slice
  because LittleFS writes already make animation unreliable during install.
- The index is authenticated by the embedded standard CA bundle in normal
  builds. Individual script downloads may still use unauthenticated HTTP/HTTPS
  because each script is verified against the already-trusted index's SHA-256
  before installation. A later signed-index step could replace TLS trust and
  make long-term/offline certificate expiry less painful.
- There is no delete/update UI yet. Re-installing an id overwrites that
  cartridge safely; deletion remains a later launcher/store action.
