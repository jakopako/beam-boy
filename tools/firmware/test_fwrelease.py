"""Tests for tools/firmware/fwrelease.py.

Run: python -m unittest discover -s tools/firmware -p "test_*.py"

These pin the half of the update contract that runs on a maintainer's PC:
that the tool only ever signs what the device's strict parser accepts, that
it refuses to package the wrong board's image, and that a published pointer
can only move forward.
"""

from __future__ import annotations

import base64
import hashlib
import json
import tempfile
import unittest
from pathlib import Path
from unittest import mock

import fwrelease as fr

BOARDS = fr.load_boards()
FEATHER = "adafruit_feather_esp32s3_nopsram"
DEVKIT = "esp32-s3-devkitc-1-n16r8"


def fake_image(board: str, version: str = "1.2.3", sequence: int = 7, size: int = 4096, chip: int = 9, tags: int = 1) -> bytes:
    header = bytearray(24)
    header[0] = fr.ESP_IMAGE_MAGIC
    header[12:14] = chip.to_bytes(2, "little")
    tag = b"beamboy-fw:%s;%s;%s;%d\0" % (
        board.encode(),
        BOARDS[board]["layout"].encode(),
        version.encode(),
        sequence,
    )
    body = bytes(header) + tag * tags
    return body + b"\xff" * max(0, size - len(body))


def key_entry_source(key_id: str, point: bytes) -> str:
    return "const TrustedKey kTrustedKeys[] = {\n%s\n    {nullptr, {0}},\n};\n" % fr.c_key_entry(key_id, point)


class ImageTests(unittest.TestCase):
    def test_accepts_a_matching_image(self):
        tag = fr.check_image(fake_image(FEATHER), FEATHER, "1.2.3", 7)
        self.assertIn(FEATHER, tag)

    def test_rejects_another_boards_image(self):
        with self.assertRaisesRegex(fr.ReleaseError, "built for"):
            fr.check_image(fake_image(FEATHER), DEVKIT)

    def test_rejects_bad_header_chip_size_and_tags(self):
        bad = bytearray(fake_image(FEATHER))
        bad[0] = 0
        with self.assertRaisesRegex(fr.ReleaseError, "not an ESP"):
            fr.check_image(bytes(bad), FEATHER)
        with self.assertRaisesRegex(fr.ReleaseError, "chip id"):
            fr.check_image(fake_image(FEATHER, chip=0), FEATHER)
        with self.assertRaisesRegex(fr.ReleaseError, "slot holds"):
            fr.check_image(fake_image(FEATHER, size=BOARDS[FEATHER]["slot_size"] + 1), FEATHER)
        with self.assertRaisesRegex(fr.ReleaseError, "exactly one build tag"):
            fr.check_image(fake_image(FEATHER, tags=2), FEATHER)
        with self.assertRaisesRegex(fr.ReleaseError, "exactly one build tag"):
            fr.check_image(fake_image(FEATHER, tags=0), FEATHER)

    def test_rejects_version_or_sequence_mismatch(self):
        with self.assertRaisesRegex(fr.ReleaseError, "version"):
            fr.check_image(fake_image(FEATHER), FEATHER, "1.2.4", 7)
        with self.assertRaisesRegex(fr.ReleaseError, "sequence"):
            fr.check_image(fake_image(FEATHER), FEATHER, "1.2.3", 8)


class ReleaseFlowTests(unittest.TestCase):
    def setUp(self):
        self.tmp = tempfile.TemporaryDirectory()
        self.dir = Path(self.tmp.name)
        self.images = {}
        for board in BOARDS:
            path = self.dir / ("%s.bin" % board)
            path.write_bytes(fake_image(board))
            self.images[board] = path
        self.key = fr.generate_key()
        self.point = fr.public_point(self.key)

    def tearDown(self):
        self.tmp.cleanup()

    def payload(self, **overrides):
        args = dict(version="1.2.3", sequence=7, images=self.images, repo=fr.DEFAULT_REPO, min_updater=1, min_save=3, min_api=1)
        args.update(overrides)
        return fr.build_payload(**args)

    def test_payload_entries_match_images(self):
        payload = self.payload()
        self.assertEqual([e["board"] for e in payload["boards"]], sorted(BOARDS))
        for entry in payload["boards"]:
            data = self.images[entry["board"]].read_bytes()
            self.assertEqual(entry["size"], len(data))
            self.assertEqual(entry["sha256"], hashlib.sha256(data).hexdigest())
            self.assertEqual(entry["layout"], BOARDS[entry["board"]]["layout"])
            self.assertTrue(entry["url"].startswith("https://github.com/jakopako/beam-boy/releases/download/fw-v1.2.3/"))

    def test_refuses_prereleases_missing_boards_and_bad_sequences(self):
        with self.assertRaisesRegex(fr.ReleaseError, "plain MAJOR.MINOR.PATCH"):
            self.payload(version="1.2.3-rc.1")
        with self.assertRaisesRegex(fr.ReleaseError, "missing images"):
            self.payload(images={FEATHER: self.images[FEATHER]})
        with self.assertRaises(fr.ReleaseError):
            self.payload(sequence=0)

    def test_sign_and_verify_round_trip(self):
        payload_bytes = fr.canonical_payload(self.payload())
        envelope = fr.build_envelope("bb-test", payload_bytes, fr.sign_bytes(self.key, payload_bytes))
        verified = fr.verify_envelope(envelope, {"bb-test": self.point})
        self.assertEqual(verified["sequence"], 7)

    def test_tampering_and_untrusted_keys_fail(self):
        payload_bytes = fr.canonical_payload(self.payload())
        signature = fr.sign_bytes(self.key, payload_bytes)
        tampered = payload_bytes.replace(b'"sequence":7', b'"sequence":9')
        envelope = fr.build_envelope("bb-test", tampered, signature)
        with self.assertRaisesRegex(fr.ReleaseError, "does not verify"):
            fr.verify_envelope(envelope, {"bb-test": self.point})
        good = fr.build_envelope("bb-test", payload_bytes, signature)
        with self.assertRaisesRegex(fr.ReleaseError, "not trusted"):
            fr.verify_envelope(good, {})
        other = fr.public_point(fr.generate_key())
        with self.assertRaisesRegex(fr.ReleaseError, "does not verify"):
            fr.verify_envelope(good, {"bb-test": other})

    def test_promote_only_moves_forward(self):
        keys = self.dir / "ota_keys.cpp"
        keys.write_text(key_entry_source("bb-test", self.point))
        stable = self.dir / "website" / "firmware" / "stable.json"

        def manifest(sequence):
            payload_bytes = fr.canonical_payload(self.payload(sequence=sequence))
            path = self.dir / ("manifest-%d.json" % sequence)
            path.write_bytes(fr.build_envelope("bb-test", payload_bytes, fr.sign_bytes(self.key, payload_bytes)))
            return path

        for board, path in self.images.items():
            path.write_bytes(fake_image(board, sequence=8))
        eight = manifest(8)
        for board, path in self.images.items():
            path.write_bytes(fake_image(board, sequence=7))
        seven = manifest(7)

        with mock.patch.object(fr, "KEYS_PATH", keys), mock.patch.object(fr, "STABLE_PATH", stable), mock.patch.object(fr, "REPO_ROOT", self.dir):
            self.assertEqual(fr.main(["promote", "--skip-download", str(seven)]), 0)
            self.assertEqual(stable.read_bytes(), seven.read_bytes())
            self.assertEqual(fr.main(["check-published"]), 0)
            self.assertEqual(fr.main(["promote", "--skip-download", str(seven)]), 1)
            self.assertEqual(fr.main(["promote", "--skip-download", str(eight)]), 0)
            self.assertEqual(stable.read_bytes(), eight.read_bytes())
            self.assertEqual(fr.main(["promote", "--skip-download", str(seven)]), 1)
            self.assertEqual(stable.read_bytes(), eight.read_bytes())

    def test_trusted_keys_are_read_from_firmware_source(self):
        keys = self.dir / "ota_keys.cpp"
        keys.write_text("// {\"bb-example\", {0x04}},\n" + key_entry_source("bb-test", self.point))
        with mock.patch.object(fr, "KEYS_PATH", keys):
            self.assertEqual(fr.firmware_trusted_keys(), {"bb-test": self.point})


class StrictFormatTests(unittest.TestCase):
    def setUp(self):
        self.payload = fr._sample_payload()

    def mutated(self, **changes):
        payload = json.loads(json.dumps(self.payload))
        payload.update(changes)
        return payload

    def test_rejects_unknown_missing_and_mistyped_fields(self):
        with self.assertRaisesRegex(fr.ReleaseError, "unknown"):
            fr.validate_payload(self.mutated(notes="x"))
        payload = self.mutated()
        del payload["version"]
        with self.assertRaisesRegex(fr.ReleaseError, "missing"):
            fr.validate_payload(payload)
        with self.assertRaises(fr.ReleaseError):
            fr.validate_payload(self.mutated(sequence=True))
        with self.assertRaises(fr.ReleaseError):
            fr.validate_payload(self.mutated(sequence="7"))
        with self.assertRaises(fr.ReleaseError):
            fr.validate_payload(self.mutated(sequence=2**32))

    def test_rejects_duplicate_keys_escapes_and_floats(self):
        with self.assertRaisesRegex(fr.ReleaseError, "duplicate"):
            fr._strict_json(b'{"a":1,"a":2}')
        with self.assertRaisesRegex(fr.ReleaseError, "escapes"):
            fr._strict_json(b'{"a":"\\u0041"}')
        with self.assertRaisesRegex(fr.ReleaseError, "fractional"):
            fr._strict_json(b'{"a":1.5}')
        with self.assertRaisesRegex(fr.ReleaseError, "ASCII"):
            fr._strict_json('{"a":"\u00e9"}'.encode("utf-8"))

    def test_rejects_bad_board_entries(self):
        boards = json.loads(json.dumps(self.payload["boards"]))
        boards.append(dict(boards[0]))
        with self.assertRaisesRegex(fr.ReleaseError, "twice"):
            fr.validate_payload(self.mutated(boards=boards))
        boards = json.loads(json.dumps(self.payload["boards"]))
        boards[0]["url"] = "http://github.com/x"
        with self.assertRaisesRegex(fr.ReleaseError, "https"):
            fr.validate_payload(self.mutated(boards=boards))
        boards[0]["url"] = "https://github.com/x"
        boards[0]["sha256"] = boards[0]["sha256"].upper()
        with self.assertRaises(fr.ReleaseError):
            fr.validate_payload(self.mutated(boards=boards))
        with self.assertRaisesRegex(fr.ReleaseError, "non-empty"):
            fr.validate_payload(self.mutated(boards=[]))

    def test_urls_must_match_the_device_allowlist(self):
        self.assertTrue(fr.is_allowed_url("https://github.com/a/b.bin"))
        self.assertTrue(fr.is_allowed_url("https://GitHub.com/a?x#y"))
        for bad in (
            "https://example.com/a",
            "https://github.com?@evil.example/x",
            "https://github.com#@evil.example/x",
            "https://github.com:443/a",
            "https://user@github.com/a",
            "https://github.co/a",
            "http://github.com/a",
        ):
            self.assertFalse(fr.is_allowed_url(bad), bad)
        boards = json.loads(json.dumps(self.payload["boards"]))
        boards[0]["url"] = "https://github.com?@evil.example/x"
        with self.assertRaisesRegex(fr.ReleaseError, "allowlist"):
            fr.validate_payload(self.mutated(boards=boards))

    def test_base64_must_be_canonical(self):
        self.assertEqual(fr.strict_b64decode("Zm8=", "x"), b"fo")
        for bad in ("Zm8", "Zm9=", "Zm8=\n", "Zm-_", "Z==="):
            with self.assertRaises(fr.ReleaseError, msg=bad):
                fr.strict_b64decode(bad, "x")

    def test_envelope_rejects_wrong_signature_length(self):
        payload_bytes = fr.canonical_payload(self.payload)
        envelope = json.loads(fr.build_envelope("bb-test", payload_bytes, b"\0" * 64))
        envelope["signature"] = base64.b64encode(b"\0" * 63).decode()
        with self.assertRaisesRegex(fr.ReleaseError, "64 bytes"):
            fr.parse_envelope(json.dumps(envelope).encode())


class RepositoryTests(unittest.TestCase):
    def test_committed_vector_verifies_and_header_is_current(self):
        self.assertEqual(fr.main(["test-vector"]), 0)

    def test_test_vector_key_is_never_trusted(self):
        vector = json.loads(fr.VECTOR_JSON.read_text())
        self.assertNotIn(vector["key_id"], fr.firmware_trusted_keys())
        self.assertNotIn(bytes.fromhex(vector["public_key"]), fr.firmware_trusted_keys().values())

    def test_published_manifest_if_any_is_valid(self):
        self.assertEqual(fr.main(["check-published"]), 0)

    def test_firmware_constants_are_readable(self):
        self.assertGreaterEqual(fr.save_format_version(), 1)
        self.assertGreaterEqual(fr.script_api_version(), 1)

    def test_keygen_refuses_to_write_inside_the_repository(self):
        target = fr.REPO_ROOT / "never-a-key.pem"
        self.assertEqual(fr.main(["keygen", "--key-id", "bb-x", "--out", str(target), "--no-passphrase"]), 1)
        self.assertFalse(target.exists())

    def test_board_table_matches_partition_tables(self):
        feather = (fr.HW_DIR / BOARDS[FEATHER]["partitions"]).read_text()
        self.assertIn("ota_0,    app,  ota_0,    0x10000,  2048K", feather)
        self.assertEqual(BOARDS[FEATHER]["slot_size"], 2048 * 1024)
        self.assertEqual(BOARDS[DEVKIT]["slot_size"], 0x640000)


if __name__ == "__main__":
    unittest.main()
