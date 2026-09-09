#!/usr/bin/env python3
"""Regenerate docs/games/index.json from the published cartridges.

Beam Boy's Store scene trusts docs/games/index.json as the root of trust for
every downloaded script: it verifies each game.be against the sha256/size
recorded there (see docs/phase-7-store.md and
beam-boy-hw/src/core/store_index.h). That means the hash/size MUST match the
exact bytes GitHub Pages serves -- not whatever a local editor/OS happens to
have on disk (Windows checkouts can silently reintroduce CRLF line endings).

This script removes that manual, error-prone step: it reads each
docs/games/<id>/game.be directly, computes size + sha256 from those bytes, and
writes index.json deterministically. Run it after adding or updating a
published cartridge, then commit both the cartridge files and the
regenerated index.json together.

Usage:
    python tools/build_store_index.py [--base-url URL] [--check]

--check verifies the existing index.json is already up to date (used in CI/
pre-commit) without writing anything; exits non-zero if it would change.
"""

from __future__ import annotations

import argparse
import hashlib
import json
import re
import sys
from pathlib import Path

REPO_ROOT = Path(__file__).resolve().parent.parent
GAMES_DIR = REPO_ROOT / "docs" / "games"
INDEX_PATH = GAMES_DIR / "index.json"
DEFAULT_BASE_URL = "https://jakopako.github.io/beam-boy/games"

ID_RE = re.compile(r"^[a-z0-9][a-z0-9_-]{0,10}$")
COLOR_RE = re.compile(r"^[0-9a-fA-F]{6}$")


def discover_games() -> list[Path]:
    if not GAMES_DIR.is_dir():
        return []
    return sorted(
        p for p in GAMES_DIR.iterdir() if p.is_dir() and (p / "game.be").is_file()
    )


def build_entry(game_dir: Path, base_url: str) -> dict:
    game_id = game_dir.name
    if not ID_RE.match(game_id):
        raise ValueError(
            f"{game_dir}: folder name {game_id!r} is not a valid store id "
            "(lowercase alnum/underscore/hyphen, <=11 chars)"
        )

    meta_path = game_dir / "meta.json"
    if not meta_path.is_file():
        raise ValueError(f"{game_dir}: missing meta.json")
    meta = json.loads(meta_path.read_text(encoding="utf-8"))

    meta_id = meta.get("id")
    if meta_id != game_id:
        raise ValueError(
            f"{meta_path}: meta id {meta_id!r} does not match folder name {game_id!r}"
        )

    title = meta.get("title")
    if not isinstance(title, str) or not title:
        raise ValueError(f"{meta_path}: missing/invalid title")

    color = meta.get("color")
    if not isinstance(color, str) or not COLOR_RE.match(color):
        raise ValueError(f"{meta_path}: missing/invalid color {color!r}")

    script_path = game_dir / "game.be"
    script_bytes = script_path.read_bytes()
    if b"\r" in script_bytes:
        raise ValueError(
            f"{script_path}: contains CR bytes (CRLF line endings). "
            "Re-save with LF line endings before publishing -- see "
            ".gitattributes for *.be files."
        )

    return {
        "id": game_id,
        "title": title,
        "color": color.lower(),
        "url": f"{base_url}/{game_id}/game.be",
        "sha256": hashlib.sha256(script_bytes).hexdigest(),
        "size": str(len(script_bytes)),
    }


def build_index(base_url: str) -> dict:
    entries = [build_entry(d, base_url) for d in discover_games()]
    return {"games": entries}


def render(index: dict) -> str:
    return json.dumps(index, indent=2) + "\n"


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument(
        "--base-url",
        default=DEFAULT_BASE_URL,
        help=f"base URL published games live under (default: {DEFAULT_BASE_URL})",
    )
    parser.add_argument(
        "--check",
        action="store_true",
        help="verify index.json is up to date; exit 1 if it would change",
    )
    args = parser.parse_args()

    try:
        index = build_index(args.base_url)
    except ValueError as exc:
        print(f"error: {exc}", file=sys.stderr)
        return 1

    rendered = render(index)

    if args.check:
        existing = INDEX_PATH.read_text(encoding="utf-8") if INDEX_PATH.is_file() else ""
        if existing != rendered:
            print(
                f"error: {INDEX_PATH} is out of date. "
                "Run `python tools/build_store_index.py` to regenerate it.",
                file=sys.stderr,
            )
            return 1
        print(f"{INDEX_PATH} is up to date ({len(index['games'])} game(s)).")
        return 0

    with open(INDEX_PATH, "w", encoding="utf-8", newline="\n") as f:
        f.write(rendered)
    print(f"wrote {INDEX_PATH} ({len(index['games'])} game(s)):")
    for entry in index["games"]:
        print(f"  - {entry['title']} ({entry['id']}): size={entry['size']} sha256={entry['sha256']}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
