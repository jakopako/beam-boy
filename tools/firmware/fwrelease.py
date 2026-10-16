#!/usr/bin/env python3
"""Beam Boy firmware release tool: build, sign, verify and publish OTA manifests.

The device trusts a firmware update only through a signed manifest (see
beam-boy-hw/src/core/fw_manifest.h for the exact format and
docs/phase-11-firmware-updates.md for the release procedure). This tool is the
other half of that contract and mirrors the device's strict rules, so a
manifest that passes `verify` here is one the device will parse.

Signing is offline by design: the private key never lives in the repository,
in CI or on a device. CI only builds, checks images and verifies published
manifests against the public keys compiled into the firmware.

Commands:
  keygen        Create a P-256 release key (offline) and print its firmware entry.
  pubkey        Print the firmware entry for an existing private key.
  check-image   Check a firmware.bin: ESP32-S3 app header, slot size, build tag.
  prepare       Check images, copy them to release asset names, write payload.json.
  sign          Sign payload.json into a manifest envelope (offline).
  verify        Verify an envelope's signature, schema and (optionally) images.
  promote       Verify a versioned manifest and install it as the stable pointer.
  test-vector   Regenerate or check the device/host signature test vectors.

Requires the `cryptography` package for keygen/pubkey/sign/verify/test-vector.
"""

from __future__ import annotations

import argparse
import base64
import getpass
import hashlib
import json
import re
import shutil
import sys
import urllib.request
from pathlib import Path

REPO_ROOT = Path(__file__).resolve().parents[2]
HW_DIR = REPO_ROOT / "beam-boy-hw"
BOARDS_PATH = HW_DIR / "firmware-boards.json"
KEYS_PATH = HW_DIR / "src" / "core" / "ota_keys.cpp"
STORAGE_H = HW_DIR / "src" / "core" / "storage.h"
IDENTITY_H = HW_DIR / "src" / "core" / "firmware_identity.h"
STABLE_PATH = REPO_ROOT / "website" / "firmware" / "stable.json"
VECTOR_JSON = Path(__file__).resolve().parent / "testdata" / "p256-vector-1.json"
VECTOR_HEADER = HW_DIR / "test" / "ota_vectors" / "ota_test_vector.h"

DEFAULT_REPO = "jakopako/beam-boy"
ENVELOPE_SCHEMA = "beamboy-fw-envelope/1"
PAYLOAD_SCHEMA = "beamboy-fw-manifest/1"
CHANNEL = "stable"

# Limits mirror fw_manifest.h.
MAX_ENVELOPE_BYTES = 16 * 1024
MAX_PAYLOAD_BYTES = MAX_ENVELOPE_BYTES // 4 * 3
MAX_KEY_ID = 32
MAX_CHANNEL = 16
MAX_VERSION = 23
MAX_BOARD_ID = 40
MAX_LAYOUT_ID = 40
MAX_URL = 200
MAX_BOARDS = 4
MAX_SEQUENCE = 2**31 - 1
U16 = 0xFFFF
U32 = 0xFFFFFFFF

SEMVER_RE = re.compile(
    r"^(0|[1-9][0-9]*)\.(0|[1-9][0-9]*)\.(0|[1-9][0-9]*)(-[0-9A-Za-z.-]+)?$"
)
KEY_ID_RE = re.compile(r"^[a-z0-9-]{1,%d}$" % MAX_KEY_ID)
IDENT_RE = re.compile(r"^[a-z0-9_-]+$")
CHANNEL_RE = re.compile(r"^[a-z]{1,%d}$" % MAX_CHANNEL)
SHA_RE = re.compile(r"^[0-9a-f]{64}$")
B64_RE = re.compile(r"^[A-Za-z0-9+/]*={0,2}$")
PRINTABLE_RE = re.compile(r"^[\x20-\x7e]*$")

ESP_IMAGE_MAGIC = 0xE9
ESP32S3_CHIP_ID = 9
BUILD_TAG_PREFIX = b"beamboy-fw:"

# Mirrors kAllowedHosts in beam-boy-hw/src/core/ota_policy.cpp. The authority
# runs to the first '/', as arduino-esp32's HTTPClient parses it.
ALLOWED_HOSTS = (
    "beamboy.ch",
    "github.com",
    "release-assets.githubusercontent.com",
    "objects.githubusercontent.com",
)


def is_allowed_url(url: str) -> bool:
    if not url.startswith("https://") or not PRINTABLE_RE.match(url) or " " in url:
        return False
    authority = url[len("https://") :].split("/", 1)[0]
    if any(c in authority for c in "@:\\?#"):
        return False
    return authority.lower() in ALLOWED_HOSTS

ENVELOPE_FIELDS = ("schema", "key_id", "payload", "signature")
PAYLOAD_FIELDS = ("schema", "channel", "version", "sequence", "boards")
BOARD_FIELDS = (
    "board",
    "layout",
    "min_updater",
    "min_save",
    "min_api",
    "url",
    "size",
    "sha256",
)


class ReleaseError(Exception):
    pass


# --------------------------------------------------------------------------
# Repository facts


def load_boards() -> dict:
    with BOARDS_PATH.open(encoding="utf-8") as f:
        return json.load(f)["boards"]


def _read_constant(path: Path, pattern: str) -> int:
    match = re.search(pattern, path.read_text(encoding="utf-8"))
    if not match:
        raise ReleaseError("could not find %r in %s" % (pattern, path))
    return int(match.group(1))


def save_format_version() -> int:
    return _read_constant(STORAGE_H, r"static constexpr uint8_t kVersion = (\d+);")


def script_api_version() -> int:
    return _read_constant(IDENTITY_H, r"constexpr uint16_t kScriptApiVersion = (\d+);")


def firmware_trusted_keys() -> dict:
    """Public keys compiled into the firmware, parsed from ota_keys.cpp."""
    text = KEYS_PATH.read_text(encoding="utf-8")
    # Strip comments so the example entry is not mistaken for a real key.
    text = re.sub(r"//[^\n]*", "", text)
    keys = {}
    for match in re.finditer(r'\{\s*"([^"]+)"\s*,\s*\{([^}]*)\}\s*\}', text):
        key_id = match.group(1)
        raw = bytes(int(b, 16) for b in re.findall(r"0x([0-9a-fA-F]{2})", match.group(2)))
        if key_id in keys:
            raise ReleaseError("duplicate key id %r in %s" % (key_id, KEYS_PATH))
        keys[key_id] = raw
    return keys


# --------------------------------------------------------------------------
# Crypto (P-256 ECDSA / SHA-256, raw r||s signatures)


def _crypto():
    try:
        from cryptography.exceptions import InvalidSignature
        from cryptography.hazmat.primitives import hashes, serialization
        from cryptography.hazmat.primitives.asymmetric import ec
        from cryptography.hazmat.primitives.asymmetric.utils import (
            decode_dss_signature,
            encode_dss_signature,
        )
    except ImportError as exc:  # pragma: no cover - environment issue
        raise ReleaseError("install the 'cryptography' package: pip install cryptography") from exc
    return {
        "InvalidSignature": InvalidSignature,
        "hashes": hashes,
        "serialization": serialization,
        "ec": ec,
        "decode": decode_dss_signature,
        "encode": encode_dss_signature,
    }


def generate_key():
    c = _crypto()
    return c["ec"].generate_private_key(c["ec"].SECP256R1())


def public_point(private_or_public) -> bytes:
    c = _crypto()
    public = getattr(private_or_public, "public_key", None)
    public = public() if callable(public) else private_or_public
    return public.public_bytes(
        c["serialization"].Encoding.X962,
        c["serialization"].PublicFormat.UncompressedPoint,
    )


def sign_bytes(private_key, data: bytes) -> bytes:
    c = _crypto()
    der = private_key.sign(data, c["ec"].ECDSA(c["hashes"].SHA256()))
    r, s = c["decode"](der)
    return r.to_bytes(32, "big") + s.to_bytes(32, "big")


def verify_bytes(point: bytes, data: bytes, signature: bytes) -> bool:
    c = _crypto()
    if len(point) != 65 or len(signature) != 64:
        return False
    try:
        public = c["ec"].EllipticCurvePublicKey.from_encoded_point(c["ec"].SECP256R1(), point)
    except ValueError:
        return False
    r = int.from_bytes(signature[:32], "big")
    s = int.from_bytes(signature[32:], "big")
    try:
        public.verify(c["encode"](r, s), data, c["ec"].ECDSA(c["hashes"].SHA256()))
    except c["InvalidSignature"]:
        return False
    return True


def load_private_key(path: Path):
    c = _crypto()
    data = path.read_bytes()
    try:
        key = c["serialization"].load_pem_private_key(data, password=None)
    except TypeError:
        password = getpass.getpass("Passphrase for %s: " % path).encode()
        key = c["serialization"].load_pem_private_key(data, password=password)
    if not isinstance(key, c["ec"].EllipticCurvePrivateKey) or key.curve.name != "secp256r1":
        raise ReleaseError("%s is not a P-256 private key" % path)
    return key


def c_key_entry(key_id: str, point: bytes) -> str:
    lines = []
    for i in range(0, len(point), 12):
        lines.append("      " + ", ".join("0x%02x" % b for b in point[i : i + 12]) + ",")
    return '    {"%s",\n     {\n%s\n     }},' % (key_id, "\n".join(lines))


# --------------------------------------------------------------------------
# Strict format rules (mirror fw_manifest.cpp)


def _no_duplicates(pairs):
    out = {}
    for key, value in pairs:
        if key in out:
            raise ReleaseError("duplicate field %r" % key)
        out[key] = value
    return out


def _strict_json(data: bytes):
    try:
        text = data.decode("ascii")
    except UnicodeDecodeError as exc:
        raise ReleaseError("not ASCII") from exc
    if "\\" in text:
        raise ReleaseError("escapes are not allowed")
    if not PRINTABLE_RE.match(text.replace("\n", " ").replace("\r", " ").replace("\t", " ")):
        raise ReleaseError("control characters are not allowed")
    try:
        return json.loads(text, object_pairs_hook=_no_duplicates, parse_float=_reject_float)
    except json.JSONDecodeError as exc:
        raise ReleaseError("syntax error: %s" % exc) from exc


def _reject_float(_value):
    raise ReleaseError("fractional numbers are not allowed")


def _exact_fields(obj, fields, what):
    if not isinstance(obj, dict):
        raise ReleaseError("%s must be an object" % what)
    unknown = set(obj) - set(fields)
    missing = set(fields) - set(obj)
    if unknown:
        raise ReleaseError("%s has unknown fields %s" % (what, sorted(unknown)))
    if missing:
        raise ReleaseError("%s is missing fields %s" % (what, sorted(missing)))


def _string(value, what, max_length, pattern=None):
    if not isinstance(value, str) or not value or len(value) > max_length:
        raise ReleaseError("%s must be a non-empty string of at most %d chars" % (what, max_length))
    if not PRINTABLE_RE.match(value) or '"' in value:
        raise ReleaseError("%s has characters the device rejects" % what)
    if pattern is not None and not pattern.match(value):
        raise ReleaseError("%s has an invalid value %r" % (what, value))
    return value


def _uint(value, what, low, high):
    if isinstance(value, bool) or not isinstance(value, int) or not low <= value <= high:
        raise ReleaseError("%s must be an integer in %d..%d" % (what, low, high))
    return value


def validate_payload(payload: dict) -> dict:
    _exact_fields(payload, PAYLOAD_FIELDS, "payload")
    if payload["schema"] != PAYLOAD_SCHEMA:
        raise ReleaseError("unsupported payload schema %r" % payload["schema"])
    _string(payload["channel"], "channel", MAX_CHANNEL, CHANNEL_RE)
    _string(payload["version"], "version", MAX_VERSION, SEMVER_RE)
    _uint(payload["sequence"], "sequence", 1, U32)
    boards = payload["boards"]
    if not isinstance(boards, list) or not boards:
        raise ReleaseError("boards must be a non-empty list")
    if len(boards) > MAX_BOARDS:
        raise ReleaseError("at most %d boards" % MAX_BOARDS)
    seen = set()
    for entry in boards:
        _exact_fields(entry, BOARD_FIELDS, "board entry")
        board = _string(entry["board"], "board", MAX_BOARD_ID, IDENT_RE)
        if board in seen:
            raise ReleaseError("board %r listed twice" % board)
        seen.add(board)
        _string(entry["layout"], "layout", MAX_LAYOUT_ID, IDENT_RE)
        for field in ("min_updater", "min_save", "min_api"):
            _uint(entry[field], field, 0, U16)
        url = _string(entry["url"], "url", MAX_URL)
        if not url.startswith("https://") or " " in url:
            raise ReleaseError("url must be https without spaces")
        if not is_allowed_url(url):
            raise ReleaseError("url host is not in the device allowlist: %r" % url)
        _uint(entry["size"], "size", 1, U32)
        _string(entry["sha256"], "sha256", 64, SHA_RE)
    return payload


def canonical_payload(payload: dict) -> bytes:
    """The exact bytes that get signed. Field order is fixed for readability;
    the device does not depend on it."""
    ordered = {k: payload[k] for k in PAYLOAD_FIELDS}
    ordered["boards"] = [{k: b[k] for k in BOARD_FIELDS} for b in payload["boards"]]
    data = json.dumps(ordered, separators=(",", ":"), ensure_ascii=True).encode("ascii")
    if len(data) > MAX_PAYLOAD_BYTES:
        raise ReleaseError("payload exceeds %d bytes" % MAX_PAYLOAD_BYTES)
    _strict_json(data)
    return data


def strict_b64decode(text, what: str) -> bytes:
    if not isinstance(text, str) or len(text) % 4 != 0 or not B64_RE.match(text):
        raise ReleaseError("%s is not strict base64" % what)
    data = base64.b64decode(text, validate=True)
    if base64.b64encode(data).decode("ascii") != text:
        raise ReleaseError("%s is not canonical base64" % what)
    return data


def build_envelope(key_id: str, payload_bytes: bytes, signature: bytes) -> bytes:
    _string(key_id, "key_id", MAX_KEY_ID, KEY_ID_RE)
    envelope = {
        "schema": ENVELOPE_SCHEMA,
        "key_id": key_id,
        "payload": base64.b64encode(payload_bytes).decode("ascii"),
        "signature": base64.b64encode(signature).decode("ascii"),
    }
    data = (json.dumps(envelope, separators=(",", ":")) + "\n").encode("ascii")
    if len(data) > MAX_ENVELOPE_BYTES:
        raise ReleaseError("envelope exceeds %d bytes" % MAX_ENVELOPE_BYTES)
    return data


def parse_envelope(data: bytes):
    """Returns (key_id, payload_bytes, signature, payload_dict)."""
    if len(data) > MAX_ENVELOPE_BYTES:
        raise ReleaseError("envelope exceeds %d bytes" % MAX_ENVELOPE_BYTES)
    envelope = _strict_json(data)
    _exact_fields(envelope, ENVELOPE_FIELDS, "envelope")
    if envelope["schema"] != ENVELOPE_SCHEMA:
        raise ReleaseError("unsupported envelope schema %r" % envelope["schema"])
    key_id = _string(envelope["key_id"], "key_id", MAX_KEY_ID, KEY_ID_RE)
    payload_bytes = strict_b64decode(envelope["payload"], "payload")
    if not payload_bytes:
        raise ReleaseError("payload is empty")
    signature = strict_b64decode(envelope["signature"], "signature")
    if len(signature) != 64:
        raise ReleaseError("signature must be 64 bytes (raw r||s)")
    payload = validate_payload(_strict_json(payload_bytes))
    return key_id, payload_bytes, signature, payload


def verify_envelope(data: bytes, trusted: dict):
    key_id, payload_bytes, signature, payload = parse_envelope(data)
    if key_id not in trusted:
        raise ReleaseError("key id %r is not trusted (known: %s)" % (key_id, sorted(trusted) or "none"))
    if not verify_bytes(trusted[key_id], payload_bytes, signature):
        raise ReleaseError("signature does not verify with key %r" % key_id)
    return payload


# --------------------------------------------------------------------------
# Images


def check_image(data: bytes, board: str, version: str | None = None, sequence: int | None = None) -> str:
    boards = load_boards()
    if board not in boards:
        raise ReleaseError("unknown board %r (known: %s)" % (board, ", ".join(sorted(boards))))
    info = boards[board]
    if len(data) < 24 or data[0] != ESP_IMAGE_MAGIC:
        raise ReleaseError("%s: not an ESP application image" % board)
    chip_id = int.from_bytes(data[12:14], "little")
    if chip_id != ESP32S3_CHIP_ID:
        raise ReleaseError("%s: image is for chip id %d, not ESP32-S3" % (board, chip_id))
    if len(data) > info["slot_size"]:
        raise ReleaseError(
            "%s: image is %d bytes, slot holds %d" % (board, len(data), info["slot_size"])
        )

    tags = []
    start = 0
    while True:
        index = data.find(BUILD_TAG_PREFIX, start)
        if index < 0:
            break
        end = data.find(b"\0", index)
        tags.append(data[index:end].decode("ascii", "replace"))
        start = index + 1
    if len(tags) != 1:
        raise ReleaseError("%s: expected exactly one build tag, found %d" % (board, len(tags)))
    try:
        tag_board, tag_layout, tag_version, tag_sequence = tags[0][len(BUILD_TAG_PREFIX) :].split(";")
    except ValueError as exc:
        raise ReleaseError("%s: malformed build tag %r" % (board, tags[0])) from exc
    if tag_board != board:
        raise ReleaseError("image was built for %r, not %r" % (tag_board, board))
    if tag_layout != info["layout"]:
        raise ReleaseError("%s: image layout %r, expected %r" % (board, tag_layout, info["layout"]))
    if version is not None and tag_version != version:
        raise ReleaseError("%s: image version %r, expected %r" % (board, tag_version, version))
    if sequence is not None and tag_sequence != str(sequence):
        raise ReleaseError("%s: image sequence %s, expected %d" % (board, tag_sequence, sequence))
    return tags[0]


def asset_name(version: str, board: str) -> str:
    return "beamboy-fw-%s-%s.bin" % (version, board)


def release_tag(version: str) -> str:
    return "fw-v%s" % version


def asset_url(repo: str, version: str, board: str) -> str:
    return "https://github.com/%s/releases/download/%s/%s" % (
        repo,
        release_tag(version),
        asset_name(version, board),
    )


def parse_image_args(values) -> dict:
    images = {}
    for value in values or []:
        board, sep, path = value.partition("=")
        if not sep or not board or not path:
            raise ReleaseError("--image expects BOARD=PATH, got %r" % value)
        if board in images:
            raise ReleaseError("board %r given twice" % board)
        images[board] = Path(path)
    return images


def build_payload(version, sequence, images: dict, repo, min_updater, min_save, min_api) -> dict:
    if not SEMVER_RE.match(version) or len(version) > MAX_VERSION or "-" in version:
        raise ReleaseError("stable releases need a plain MAJOR.MINOR.PATCH version")
    _uint(sequence, "sequence", 1, MAX_SEQUENCE)
    boards = load_boards()
    entries = []
    for board, path in sorted(images.items()):
        data = path.read_bytes()
        check_image(data, board, version, sequence)
        entries.append(
            {
                "board": board,
                "layout": boards[board]["layout"],
                "min_updater": min_updater,
                "min_save": min_save,
                "min_api": min_api,
                "url": asset_url(repo, version, board),
                "size": len(data),
                "sha256": hashlib.sha256(data).hexdigest(),
            }
        )
    missing = set(boards) - set(images)
    if missing:
        raise ReleaseError("missing images for %s" % ", ".join(sorted(missing)))
    return validate_payload(
        {
            "schema": PAYLOAD_SCHEMA,
            "channel": CHANNEL,
            "version": version,
            "sequence": sequence,
            "boards": entries,
        }
    )


# --------------------------------------------------------------------------
# Commands


def cmd_keygen(args):
    _string(args.key_id, "key id", MAX_KEY_ID, KEY_ID_RE)
    out = Path(args.out)
    if out.exists():
        raise ReleaseError("%s exists; refusing to overwrite a key" % out)
    try:
        out.resolve().relative_to(REPO_ROOT)
    except ValueError:
        pass
    else:
        raise ReleaseError("refusing to write a private key inside the repository")
    c = _crypto()
    key = generate_key()
    if args.no_passphrase:
        encryption = c["serialization"].NoEncryption()
    else:
        password = getpass.getpass("New passphrase: ")
        if not password or password != getpass.getpass("Repeat passphrase: "):
            raise ReleaseError("passphrases empty or different")
        encryption = c["serialization"].BestAvailableEncryption(password.encode())
    pem = key.private_bytes(
        c["serialization"].Encoding.PEM, c["serialization"].PrivateFormat.PKCS8, encryption
    )
    out.write_bytes(pem)
    print("Wrote private key to %s. Keep it offline and backed up." % out)
    print("Add this entry to %s:\n" % KEYS_PATH.relative_to(REPO_ROOT))
    print(c_key_entry(args.key_id, public_point(key)))


def cmd_pubkey(args):
    key = load_private_key(Path(args.key))
    print(c_key_entry(args.key_id, public_point(key)))


def cmd_check_image(args):
    data = Path(args.image).read_bytes()
    tag = check_image(data, args.board, args.version, args.sequence)
    slot = load_boards()[args.board]["slot_size"]
    print("%s: %d / %d bytes (%.1f%% of slot), %s" % (args.board, len(data), slot, 100.0 * len(data) / slot, tag))


def cmd_prepare(args):
    images = parse_image_args(args.image)
    min_save = args.min_save if args.min_save is not None else save_format_version()
    min_api = args.min_api if args.min_api is not None else script_api_version()
    payload = build_payload(args.version, args.sequence, images, args.repo, args.min_updater, min_save, min_api)
    out = Path(args.out)
    out.mkdir(parents=True, exist_ok=True)
    for board, path in images.items():
        shutil.copyfile(path, out / asset_name(args.version, board))
    (out / "payload.json").write_bytes(canonical_payload(payload))
    print("Prepared %s (sequence %d) in %s:" % (args.version, args.sequence, out))
    for entry in payload["boards"]:
        print("  %s  %8d bytes  %s" % (entry["board"], entry["size"], entry["sha256"]))
    print("Next: sign payload.json offline, then upload assets to release %s." % release_tag(args.version))


def cmd_sign(args):
    payload_path = Path(args.payload)
    payload = validate_payload(_strict_json(payload_path.read_bytes()))
    payload_bytes = canonical_payload(payload)
    key = load_private_key(Path(args.key))
    trusted = firmware_trusted_keys()
    point = public_point(key)
    if args.key_id in trusted and trusted[args.key_id] != point:
        raise ReleaseError("key %r in ota_keys.cpp does not match this private key" % args.key_id)
    if args.key_id not in trusted:
        print("warning: key %r is not in ota_keys.cpp; devices will refuse this manifest" % args.key_id, file=sys.stderr)
    envelope = build_envelope(args.key_id, payload_bytes, sign_bytes(key, payload_bytes))
    # Round-trip through the strict reader before anything is written.
    key_id, signed, signature, _ = parse_envelope(envelope)
    if not verify_bytes(point, signed, signature):
        raise ReleaseError("internal error: fresh signature does not verify")
    out = Path(args.out) if args.out else payload_path.parent / ("manifest-%s.json" % payload["version"])
    if out.exists():
        raise ReleaseError("%s exists; published manifests are never overwritten" % out)
    out.write_bytes(envelope)
    print("Wrote %s (%d bytes) signed by %s" % (out, len(envelope), key_id))


def _download(url: str, limit: int) -> bytes:
    with urllib.request.urlopen(url, timeout=60) as response:  # noqa: S310 - https enforced by validation
        data = response.read(limit + 1)
    if len(data) > limit:
        raise ReleaseError("%s is larger than %d bytes" % (url, limit))
    return data


def cmd_verify(args):
    data = Path(args.manifest).read_bytes()
    trusted = firmware_trusted_keys()
    if args.public_key:
        c = _crypto()
        public = c["serialization"].load_pem_public_key(Path(args.public_key).read_bytes())
        key_id = parse_envelope(data)[0]
        trusted = {key_id: public_point(public)}
    payload = verify_envelope(data, trusted)
    print("Signature OK: %s sequence %d (%s)" % (payload["version"], payload["sequence"], payload["channel"]))
    images = parse_image_args(args.image)
    boards = load_boards()
    for entry in payload["boards"]:
        board = entry["board"]
        if board in boards and boards[board]["layout"] != entry["layout"]:
            raise ReleaseError("%s: manifest layout %r, repository says %r" % (board, entry["layout"], boards[board]["layout"]))
        image = None
        if board in images:
            image = images[board].read_bytes()
        elif args.download:
            image = _download(entry["url"], entry["size"])
        if image is None:
            print("  %s: not checked (no image)" % board)
            continue
        if len(image) != entry["size"] or hashlib.sha256(image).hexdigest() != entry["sha256"]:
            raise ReleaseError("%s: image does not match the signed size/hash" % board)
        check_image(image, board, payload["version"], payload["sequence"])
        print("  %s: image matches (%d bytes)" % (board, len(image)))


def cmd_promote(args):
    data = Path(args.manifest).read_bytes()
    payload = verify_envelope(data, firmware_trusted_keys())
    if payload["channel"] != CHANNEL or "-" in payload["version"]:
        raise ReleaseError("only stable releases can be promoted")
    if set(e["board"] for e in payload["boards"]) != set(load_boards()):
        raise ReleaseError("a stable release must cover every board in firmware-boards.json")
    if STABLE_PATH.exists():
        current = parse_envelope(STABLE_PATH.read_bytes())[3]
        if payload["sequence"] <= current["sequence"]:
            raise ReleaseError(
                "sequence %d is not newer than the published %d" % (payload["sequence"], current["sequence"])
            )
    for entry in payload["boards"]:
        if not args.skip_download:
            image = _download(entry["url"], entry["size"])
            if len(image) != entry["size"] or hashlib.sha256(image).hexdigest() != entry["sha256"]:
                raise ReleaseError("%s: published asset does not match the manifest" % entry["board"])
    STABLE_PATH.parent.mkdir(parents=True, exist_ok=True)
    STABLE_PATH.write_bytes(data)
    print("Promoted %s (sequence %d) to %s. Commit and push to publish." % (payload["version"], payload["sequence"], STABLE_PATH.relative_to(REPO_ROOT)))


def cmd_check_published(_args):
    """CI: a published stable pointer must verify against the firmware's keys."""
    if not STABLE_PATH.exists():
        print("No published firmware manifest (%s)." % STABLE_PATH.relative_to(REPO_ROOT))
        return
    payload = verify_envelope(STABLE_PATH.read_bytes(), firmware_trusted_keys())
    if payload["channel"] != CHANNEL or "-" in payload["version"]:
        raise ReleaseError("stable.json must hold a stable release")
    boards = load_boards()
    for entry in payload["boards"]:
        if entry["board"] not in boards or boards[entry["board"]]["layout"] != entry["layout"]:
            raise ReleaseError("stable.json entry %r does not match firmware-boards.json" % entry["board"])
    print("stable.json OK: %s sequence %d" % (payload["version"], payload["sequence"]))


# --- Test vectors ----------------------------------------------------------


def _sample_payload() -> dict:
    return validate_payload(
        {
            "schema": PAYLOAD_SCHEMA,
            "channel": CHANNEL,
            "version": "1.2.3",
            "sequence": 7,
            "boards": [
                {
                    "board": "adafruit_feather_esp32s3_nopsram",
                    "layout": "feather-s3-8mb-ota2048k-v1",
                    "min_updater": 1,
                    "min_save": 3,
                    "min_api": 1,
                    "url": asset_url(DEFAULT_REPO, "1.2.3", "adafruit_feather_esp32s3_nopsram"),
                    "size": 1234567,
                    "sha256": hashlib.sha256(b"beam boy feather test image").hexdigest(),
                },
                {
                    "board": "esp32-s3-devkitc-1-n16r8",
                    "layout": "devkitc-s3-16mb-ota6400k-v1",
                    "min_updater": 1,
                    "min_save": 3,
                    "min_api": 1,
                    "url": asset_url(DEFAULT_REPO, "1.2.3", "esp32-s3-devkitc-1-n16r8"),
                    "size": 1300000,
                    "sha256": hashlib.sha256(b"beam boy devkitc test image").hexdigest(),
                },
            ],
        }
    )


def _c_bytes(data: bytes, indent: str = "    ") -> str:
    lines = []
    for i in range(0, len(data), 12):
        lines.append(indent + ", ".join("0x%02x" % b for b in data[i : i + 12]) + ",")
    return "\n".join(lines)


def _c_string(text: str) -> str:
    # Envelopes never contain backslashes (the format forbids escapes), so
    # quotes are the only characters that need escaping in a C literal.
    if "\\" in text:
        raise ReleaseError("unexpected backslash in envelope")
    chunks = [text[i : i + 70] for i in range(0, len(text), 70)]
    return "\n".join('    "%s"' % c.replace('"', '\\"') for c in chunks)


def render_vector_header(vector: dict) -> str:
    point = bytes.fromhex(vector["public_key"])
    envelope = vector["envelope"]
    return (
        "// Generated by tools/firmware/fwrelease.py test-vector -- do not edit.\n"
        "//\n"
        "// A manifest signed by a throwaway key whose private half was discarded.\n"
        "// Shared by the host parser tests and the on-device signature test, and\n"
        "// verified by tools/firmware/test_fwrelease.py, so all three agree on the\n"
        "// exact bytes. This key is NOT trusted by any firmware build.\n"
        "\n"
        "#pragma once\n"
        "\n"
        "#include <stdint.h>\n"
        "\n"
        "namespace ota_test_vector {\n"
        "\n"
        'constexpr char kKeyId[] = "%s";\n'
        "\n"
        "// As a macro too, so a test can place it in a const TrustedKey table.\n"
        "#define OTA_TEST_VECTOR_PUBLIC_KEY \\\n%s\n"
        "\n"
        "constexpr uint8_t kPublicKey[65] = {OTA_TEST_VECTOR_PUBLIC_KEY};\n"
        "\n"
        "constexpr char kEnvelope[] =\n%s;\n"
        "\n"
        "}  // namespace ota_test_vector\n"
    ) % (
        vector["key_id"],
        " \\\n".join(_c_bytes(point).rstrip(",").split("\n")),
        _c_string(envelope),
    )


def cmd_test_vector(args):
    if args.regenerate:
        key = generate_key()
        payload_bytes = canonical_payload(_sample_payload())
        envelope = build_envelope("bb-test-vector", payload_bytes, sign_bytes(key, payload_bytes))
        vector = {
            "comment": "Throwaway P-256 key; private half discarded. Never trust it in firmware.",
            "key_id": "bb-test-vector",
            "public_key": public_point(key).hex(),
            "envelope": envelope.decode("ascii").rstrip("\n"),
        }
        VECTOR_JSON.parent.mkdir(parents=True, exist_ok=True)
        VECTOR_JSON.write_bytes((json.dumps(vector, indent=2) + "\n").encode("ascii"))
    vector = json.loads(VECTOR_JSON.read_text(encoding="ascii"))
    point = bytes.fromhex(vector["public_key"])
    key_id, payload_bytes, signature, _ = parse_envelope(vector["envelope"].encode("ascii"))
    if key_id != vector["key_id"] or not verify_bytes(point, payload_bytes, signature):
        raise ReleaseError("test vector does not verify")
    if vector["key_id"] in firmware_trusted_keys():
        raise ReleaseError("the test-vector key must never be trusted by firmware")
    header = render_vector_header(vector).encode("ascii")
    if args.regenerate:
        VECTOR_HEADER.parent.mkdir(parents=True, exist_ok=True)
        VECTOR_HEADER.write_bytes(header)
        print("Regenerated %s and %s" % (VECTOR_JSON.relative_to(REPO_ROOT), VECTOR_HEADER.relative_to(REPO_ROOT)))
    elif not VECTOR_HEADER.exists() or VECTOR_HEADER.read_bytes().replace(b"\r\n", b"\n") != header:
        raise ReleaseError("%s is out of date; run test-vector --regenerate" % VECTOR_HEADER.relative_to(REPO_ROOT))
    else:
        print("Test vector OK")


def main(argv=None) -> int:
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    sub = parser.add_subparsers(dest="command", required=True)

    p = sub.add_parser("keygen", help="create an offline P-256 release key")
    p.add_argument("--key-id", required=True)
    p.add_argument("--out", required=True, help="private key path, outside the repository")
    p.add_argument("--no-passphrase", action="store_true", help="store unencrypted (not recommended)")
    p.set_defaults(func=cmd_keygen)

    p = sub.add_parser("pubkey", help="print the ota_keys.cpp entry for a private key")
    p.add_argument("--key", required=True)
    p.add_argument("--key-id", required=True)
    p.set_defaults(func=cmd_pubkey)

    p = sub.add_parser("check-image", help="check a firmware.bin for a board")
    p.add_argument("--board", required=True)
    p.add_argument("--version")
    p.add_argument("--sequence", type=int)
    p.add_argument("image")
    p.set_defaults(func=cmd_check_image)

    p = sub.add_parser("prepare", help="check images and write release assets + payload.json")
    p.add_argument("--version", required=True)
    p.add_argument("--sequence", required=True, type=int)
    p.add_argument("--image", action="append", required=True, metavar="BOARD=PATH")
    p.add_argument("--repo", default=DEFAULT_REPO)
    p.add_argument("--min-updater", type=int, default=1)
    p.add_argument("--min-save", type=int, help="default: current save format version")
    p.add_argument("--min-api", type=int, help="default: current script API version")
    p.add_argument("--out", required=True)
    p.set_defaults(func=cmd_prepare)

    p = sub.add_parser("sign", help="sign payload.json into a manifest envelope (offline)")
    p.add_argument("--key", required=True)
    p.add_argument("--key-id", required=True)
    p.add_argument("--out")
    p.add_argument("payload")
    p.set_defaults(func=cmd_sign)

    p = sub.add_parser("verify", help="verify a manifest and optionally its images")
    p.add_argument("--manifest", required=True)
    p.add_argument("--public-key", help="PEM public key instead of ota_keys.cpp")
    p.add_argument("--image", action="append", metavar="BOARD=PATH")
    p.add_argument("--download", action="store_true", help="download and check published assets")
    p.set_defaults(func=cmd_verify)

    p = sub.add_parser("promote", help="install a verified manifest as website/firmware/stable.json")
    p.add_argument("manifest")
    p.add_argument("--skip-download", action="store_true", help="do not re-check published assets")
    p.set_defaults(func=cmd_promote)

    p = sub.add_parser("check-published", help="CI: verify website/firmware/stable.json if present")
    p.set_defaults(func=cmd_check_published)

    p = sub.add_parser("test-vector", help="check (or --regenerate) signature test vectors")
    p.add_argument("--regenerate", action="store_true")
    p.set_defaults(func=cmd_test_vector)

    args = parser.parse_args(argv)
    try:
        args.func(args)
    except ReleaseError as exc:
        print("error: %s" % exc, file=sys.stderr)
        return 1
    return 0


if __name__ == "__main__":
    sys.exit(main())
