# Phase 11 — Firmware updates

> **Status: steps 1–3 of PLAN.md's implementation order are implemented; steps 4–5 are not.**
> The release contract, offline signing tool, strict manifest parser, update
> policy and the `Ota` streaming/verifying service exist and are tested on the
> host. The Settings > Update firmware scene is still the unavailable
> placeholder: nothing calls `Ota`, so no console can update yet. No release key
> has been created, so even a wired-up `Ota` would refuse every manifest.

## What exists

| Piece | Where | Tested by |
|---|---|---|
| Board/layout identities | `beam-boy-hw/firmware-boards.json` | build script, release tool |
| Build identity (board, layout, version, sequence, slot size) | `scripts/firmware_identity.py` → `src/core/firmware_identity.h` | release tool `check-image` |
| Manifest format + strict parser | `src/core/fw_manifest.{h,cpp}` | `test_ota_manifest` (host) |
| Release selection, URL allowlist, power gate, hold-to-confirm, image accounting | `src/core/ota_policy.{h,cpp}` | `test_ota_policy` (host) |
| ECDSA P-256 verification (mbedTLS) + trusted keys | `src/core/ota_signature.{h,cpp}`, `src/core/ota_keys.cpp` | `test_device_ota_signature` (on target; build-checked in CI) |
| Check / stream / verify / select | `src/core/ota.{h,cpp}` | build only; needs hardware |
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

A firmware that trusts no key refuses every manifest. That is the current
state, deliberately.

## Testing

```sh
cd beam-boy-hw && pio test -e native -f "test_ota_*"           # parser + policy
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

## Not done yet (PLAN.md steps 4–5)

- **Scene and engine guard (step 4):**
  - Wire `UpdateFirmwareScene` to `Network` + `Ota`, with the confirm overlay,
    real progress, distinct pulse rhythms and a deliberate reboot after
    success.
  - Suspend idle sleep while `Ota::busy()`, and abort on critical power.
  - Measure flash-write latency against RMT LED output.
  - `Power` also needs to expose the age of its last good reading for the power
    gate.
- **First boot and rollback (step 5):**
  - Override Arduino's `verifyRollbackLater()` and acknowledge the new image
    only after a healthy launcher has run for 5 s.
  - Prove rollback on the Feather: interrupted downloads, a crashing signed
    image, and power loss at every stage.
- **Run `test_device_ota_signature` on the Feather and record the numbers**,
  then create the first release key.
- **Ship the first updater-capable firmware over USB** before publishing
  `stable.json`.
