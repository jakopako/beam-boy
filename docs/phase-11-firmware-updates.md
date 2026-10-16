# Phase 11 — Firmware updates

> **Status: steps 1–4 of PLAN.md's implementation order are implemented; step 5
> (health-checked first boot and rollback) is not.** Settings > Update firmware
> now checks, installs and restarts, and the firmware trusts release key
> `bb-release-1`. Step 4 is build-checked and host-tested, but has not yet been
> run end-to-end on a Feather: see "Hardware checklist for step 4". Until step 5,
> a new image that crashes is not rolled back automatically.

## What exists

| Piece | Where | Tested by |
|---|---|---|
| Board/layout identities | `beam-boy-hw/firmware-boards.json` | build script, release tool |
| Build identity (board, layout, version, sequence, slot size) | `scripts/firmware_identity.py` → `src/core/firmware_identity.h` | release tool `check-image` |
| Manifest format + strict parser | `src/core/fw_manifest.{h,cpp}` | `test_ota_manifest` (host) |
| Release selection, URL allowlist, power gate, hold-to-confirm, image accounting | `src/core/ota_policy.{h,cpp}` | `test_ota_policy` (host) |
| ECDSA P-256 verification (mbedTLS) + trusted keys | `src/core/ota_signature.{h,cpp}`, `src/core/ota_keys.cpp` | `test_device_ota_signature` (passed on a Feather) |
| Check / stream / verify / select | `src/core/ota.{h,cpp}` | build only; needs hardware |
| Update screen: state machine | `src/scenes/update_flow.{h,cpp}` | `test_update_flow` (host) |
| Update screen: tube rendering | `src/scenes/update_firmware_view.{h,cpp}` | `test_update_flow` (host) |
| Update screen: wiring to Network/Ota/Power/Storage | `src/scenes/update_firmware_scene.{h,cpp}` | build only; needs hardware |
| Engine guard: sleep inhibit, abort before critical shutdown | `Engine::setSleepInhibited()`, `Scene::abortForShutdown()` | build only |
| Release tool | `tools/firmware/fwrelease.py` | `tools/firmware/test_fwrelease.py` |
| CI | `.github/workflows/firmware.yml`, Pages workflow | — |

## Decisions

- **Signature:** ECDSA P-256 over SHA-256 of the exact decoded payload bytes,
  encoded as raw `r||s` (64 bytes). Chosen over the earlier RSA-PSS-3072
  proposal for smaller keys and signatures. Both are in this mbedTLS (2.28.7);
  Ed25519 is not.
- **Keys:** signing is offline. The private key never lives in the repository,
  in GitHub (no Actions secret), or on a device. CI only builds, checks and
  verifies. That means forks and PRs can never reach it.
- **Trust model:** the manifest signature is the root of trust. TLS with the CA
  bundle still authenticates both the manifest host and the image hosts
  (`setInsecure()` is gone), but a forged or swapped image is rejected by the
  signed size and SHA-256 even if TLS were bypassed. This is updater-level
  authenticity, not Secure Boot; no eFuses are touched.
- **No network time:** this mbedTLS build has `MBEDTLS_HAVE_TIME_DATE` off, so
  certificate dates are not checked. A console that has been offline for years
  can still connect.

## Manifest format

Discovery URL: `https://beamboy.ch/firmware/stable.json` (`website/firmware/stable.json`).
It is a copy of a versioned `manifest-X.Y.Z.json` that is also attached to the
GitHub release.

Envelope (≤ 16 KiB):

```json
{"schema":"beamboy-fw-envelope/1","key_id":"bb-release-1",
 "payload":"<base64>","signature":"<base64 of 64-byte r||s>"}
```

Decoded payload:

```json
{"schema":"beamboy-fw-manifest/1","channel":"stable","version":"1.2.3","sequence":7,
 "boards":[{"board":"adafruit_feather_esp32s3_nopsram","layout":"feather-s3-8mb-ota2048k-v1",
            "min_updater":1,"min_save":3,"min_api":1,
            "url":"https://github.com/jakopako/beam-boy/releases/download/fw-v1.2.3/beamboy-fw-1.2.3-adafruit_feather_esp32s3_nopsram.bin",
            "size":1204384,"sha256":"<64 lowercase hex>"}]}
```

Strict rules (device and tool agree; see `fw_manifest.h`):

- Every field is required. Unknown or duplicate fields are rejected; a new
  field means a new schema.
- Strings must be printable ASCII with no escapes. Integers are unsigned with
  no leading zeros, fractions or exponents. Base64 must be canonical.
- Limits:
  - key id ≤ 32 characters
  - channel ≤ 16 characters
  - version ≤ 23 characters (SemVer)
  - board and layout ids ≤ 40 characters
  - URL ≤ 200 characters, `https://` only
  - 1–4 boards, each listed once
- `min_updater`: the oldest update protocol (`fw::kUpdaterVersion`) that may
  install this release.
- `min_save`: the oldest save format (`Storage::formatVersion()`) the new
  firmware can read.
- `min_api`: the oldest script API (`fw::kScriptApiVersion`) whose cartridges
  it still runs.

The console refuses a release whose minimum is above its own value.

## What the device does (`Ota`)

`check()` runs once per visit, never writes flash, and is bounded at about 30 s:

1. If a different slot is already selected for the next boot, it reports
   **staged** and stops. It never overwrites a pending image.
2. It requires WiFi, and an inactive slot whose real size equals the build's
   `kSlotSize`.
3. It GETs the envelope over HTTPS with the CA bundle and no redirects. The
   response must have a Content-Length of 16 KiB or less.
4. It parses the envelope, decodes the payload, and verifies the signature
   with the key named by `key_id`. Only then does it parse the payload.
5. It evaluates the release:
   - Only the stable channel, and no prereleases.
   - Equal sequence means up to date; a lower sequence is never a downgrade.
   - Then it checks this board's entry, the layout, the three minimums, that
     the image fits the slot, and that the URL is on the allowlist.

`beginInstall(preconditions)` refuses unless the release is available, the
power gate passes and saves are clean. The power gate requires a fresh (≤ 5 s),
valid gauge reading of at least 30%. USB never bypasses it, and neither does a
missing gauge reading. The rest of the install:

1. It follows at most 5 redirects by hand. Each hop must be `https://` to
   exactly one of the allowed hosts: `beamboy.ch`, `github.com`,
   `release-assets.githubusercontent.com` or
   `objects.githubusercontent.com`.
2. It requires Content-Length to equal the signed size.
3. It opens the inactive slot with sequential erase.

`poll()` writes at most 4 × 4 KiB per call, within about 25 ms. While writing
it:

- hashes as it streams;
- rejects a bad first byte (`0xE9`) and any byte beyond the signed size;
- times out after 15 s without data or 10 min in total;
- treats an early close as truncation.

When the last byte arrives, verification checks that no extra bytes follow and
that the SHA-256 matches, then `esp_ota_end()` validates the image. Only after
all of that does `esp_ota_set_boot_partition()` select the new image.
`cancel()` and every failure call `esp_ota_abort()`, so the boot target never
changes.

## The update screen (Settings > Update firmware)

Entering turns the radio on and checks; it never writes flash on its own.

| Tube | Meaning | Controls |
|---|---|---|
| Two amber bars, double pulse | No WiFi credentials; set them up in Settings > WiFi (the portal is never opened from here) | hold B leaves |
| Blue comet sweeping | Connecting | hold B leaves |
| Dim steady blue | Checking (blocks a few seconds) | — |
| Steady blue after one white sweep | Up to date | hold B leaves |
| Steady turquoise | Update available | hold A 1.2 s to install (white fills from the start; releasing early cancels); hold B leaves |
| Blue bar filling | Downloading: the fill is the real verified byte count | hold B cancels and leaves |
| White points sweeping inward | Verifying | — |
| Green fills from both ends, white centre heartbeat | Installed (or already installed earlier, awaiting restart) | release A, then press A to restart; hold B leaves without restarting |
| Red, 1 / 2 / 3 / 4 pulses | Failed: connection / check (manifest, TLS, signature) / incompatible release / install | A retries; hold B leaves |
| Red, slow breathing | Battery below 30%, stale or unreadable | A retries; hold B leaves |

Serial logs every stage change with the detailed reason (`[update] ...`).

Behaviour that is easy to miss:

- **A press carried into the screen never confirms.** A must be released before
  a hold can start, including after a retry.
- **The confirming hold never also restarts.** After success A must be
  released and pressed again.
- **The radio is on only while working towards a result.** Up to date,
  installed and every failure switch it off; a retry reconnects.
- **An installed image runs at the next restart, whatever happens.** Leaving
  with hold B, sleeping or switching off does not undo it. Re-entering the
  screen reports it (green) instead of downloading again.
- **Before writing,** saves are committed and must be clean, and the battery
  must have a reading of at least 30% that is at most 5 s old.
- **While writing,** the scene holds off idle sleep through
  `Engine::setSleepInhibited()`. The engine clears it on every scene change.
  A critical battery calls `Scene::abortForShutdown()` first, which cancels the
  write and turns the radio off before the shutdown flush.
- **Responsiveness:** `Ota::poll()` runs once per frame (at most 4 × 4 KiB,
  about 25 ms). Expect a lower frame rate while downloading, not a frozen tube.
  The manifest check, opening the image stream and the final
  `esp_ota_end()` validation each block for a moment behind a static frame.

### Hardware checklist for step 4

None of this has run on a Feather yet. To test end-to-end you need a signed
release newer than the firmware on the console:

1. Flash the console over USB with a release-identity build. In PowerShell:
   ```powershell
   cd beam-boy-hw
   $env:BEAMBOY_FW_VERSION="0.9.0"; $env:BEAMBOY_FW_SEQUENCE="1"
   pio run -e adafruit_feather_esp32s3_nopsram -t upload
   ```
2. Build sequence 2, then `prepare`, `sign`, upload the `.bin` and manifest to
   GitHub release `fw-v0.9.1`, and `promote` (see Release procedure).
   Publishing `stable.json` is harmless before this firmware ships: older
   firmware has no updater.
3. On the console, check each of these:
   - Settings > Update firmware shows available (turquoise) and serial names
     `0.9.0 -> 0.9.1`.
   - The download fill moves smoothly enough, and the LEDs do not glitch while
     flash is written. Note the frame rate in the `[PERF]` log.
   - Hold B mid-download cancels, the radio goes off, and re-entering offers
     the update again.
   - A full install shows the green result; A restarts into 0.9.1 (check the
     boot banner `beamboy-fw:...;0.9.1;2`).
   - Re-entering after an install without restarting shows the staged (green)
     result rather than downloading again.
   - Brightness, highscores, WiFi credentials and installed games are
     unchanged after a successful and after a cancelled update.
   - Below 30% battery (or with the gauge unplugged), the install is refused
     with the slow red breath.
   - With no credentials, the two amber bars appear and nothing connects.

Keep a USB cable ready. Until step 5, an image that boots but crashes is not
rolled back on its own; reflash over USB.

## Release procedure

Version and sequence are inputs to the build, not edits to a source file.
`sequence` must be higher than the published one; dev builds are
`0.0.0-dev`/0.

```sh
# 1. Build the Feather firmware with the release identity.
cd beam-boy-hw
BEAMBOY_FW_VERSION=1.2.3 BEAMBOY_FW_SEQUENCE=7 pio run -e adafruit_feather_esp32s3_nopsram
cd ..

# 2. Check the image (header, chip, slot size, embedded build tag) and stage the asset + payload.json.
python tools/firmware/fwrelease.py prepare --version 1.2.3 --sequence 7 \
  --image adafruit_feather_esp32s3_nopsram=beam-boy-hw/.pio/build/adafruit_feather_esp32s3_nopsram/firmware.bin \
  --out dist/fw-v1.2.3

# 3. Offline: sign. Writes dist/fw-v1.2.3/manifest-1.2.3.json and refuses to overwrite.
python tools/firmware/fwrelease.py sign --key /offline/bb-release-1.pem --key-id bb-release-1 dist/fw-v1.2.3/payload.json

# 4. Create GitHub release fw-v1.2.3 and upload the .bin and manifest-1.2.3.json. Never replace assets.
# 5. Check the published asset, then smoke-test on a Feather.
python tools/firmware/fwrelease.py verify --manifest dist/fw-v1.2.3/manifest-1.2.3.json --download

# 6. Move the stable pointer (re-downloads and checks assets; refuses a non-increasing sequence), commit, push.
python tools/firmware/fwrelease.py promote dist/fw-v1.2.3/manifest-1.2.3.json
```

`prepare` defaults `min_save` and `min_api` to the current firmware values,
which is the conservative choice. Lower them only when the release really
migrates older saves or still runs older cartridges.

### Keys

```sh
python tools/firmware/fwrelease.py keygen --key-id bb-release-1 --out /offline/bb-release-1.pem
```

`keygen` refuses to write inside the repository and asks for a passphrase. It
prints the public-key entry to paste into `src/core/ota_keys.cpp`.

To rotate keys:

1. Ship a release that trusts both the old and new ids.
2. Sign the following releases with the new key.
3. Then drop the old id.

Trusted ids live in `src/core/ota_keys.cpp`. The firmware currently trusts
`bb-release-1` (created 2026-10-16; private key held offline by the
maintainer).

## Testing

```sh
cd beam-boy-hw && pio test -e native -f "test_ota_*" -f test_update_flow   # parser, policy, update screen
python -m unittest discover -s tools/firmware -p "test_*.py"   # release tool
python tools/firmware/fwrelease.py test-vector                 # vector + header in sync
pio test -e adafruit_feather_esp32s3_nopsram -f test_device_ota_signature   # on a Feather
```

The signature test vector (`tools/firmware/testdata/p256-vector-1.json`) was
signed by a throwaway key whose private half was discarded. Its generated
header (`test/ota_vectors/ota_test_vector.h`) is shared by the host parser
tests and the on-device test. The on-device test also logs verification time
and heap, which are the measurements PLAN.md asks for before adopting the
format.

Measured on the Feather (2026-10-16, test build without WiFi or engine):

| Measurement | Value |
|---|---|
| All signature tests | 7/7 pass |
| One P-256 verify | ~292 ms |
| Peak extra heap during verify | ~5.2 KB (min free heap 337,588 → 332,276 bytes) |
| Heap kept after the first verify | 444 bytes, kept once: 10 further verifies left free heap unchanged (342,340 → 342,340), so there is no per-call leak |

The verify cost is paid once per `check()`, which already blocks for a TLS
handshake, so it needs no special handling.

## Not done yet

- **Run the step 4 hardware checklist** (above), including the LED behaviour
  during flash writes and the frame rate while downloading.
- **First boot and rollback (step 5):**
  - Override Arduino's `verifyRollbackLater()` and acknowledge the new image
    only after a healthy launcher has run for 5 s.
  - Prove rollback on the Feather: interrupted downloads, a crashing signed
    image, and power loss at every stage.
- **Ship the first updater-capable firmware over USB** before publishing a
  real `stable.json`.
