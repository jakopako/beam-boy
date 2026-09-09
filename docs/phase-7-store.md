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

## Controls and tube vocabulary

- Enter **Store** from the launcher.
- Blue sweep: connecting to stored WiFi.
- Amber sweep: fetching the index.
- Store entries: coloured blocks from the index; selected block breathes.
- **A** / nav press: install selected cartridge.
- White bar: install in progress.
- Green centre-out flash: install succeeded.
- Red pulse: failure; read the serial log for the precise reason.
- Hold **B**: return to the launcher, using the existing utility-scene exit
  gesture.

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
