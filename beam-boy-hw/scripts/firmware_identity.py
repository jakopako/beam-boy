# PlatformIO pre-build script: bakes the firmware update identity into a build.
#
# The board id, partition-layout id and inactive-slot size come from
# firmware-boards.json, keyed by the PlatformIO environment name, so they can
# never be edited independently of the release tooling that checks them.
#
# The version and release sequence come from the environment of a release
# build:
#
#   BEAMBOY_FW_VERSION=1.2.3 BEAMBOY_FW_SEQUENCE=7 pio run -e <env>
#
# Without them this is a development build: version "0.0.0-dev", sequence 0.
# Sequence 0 is never published, so a dev build is always older than any
# release. Setting only one of the two is an error rather than a guess.

import json
import os
import re

Import("env")  # noqa: F821 -- provided by SCons

SEMVER = re.compile(
    r"^(0|[1-9][0-9]*)\.(0|[1-9][0-9]*)\.(0|[1-9][0-9]*)(-[0-9A-Za-z.-]+)?$"
)
MAX_VERSION_LENGTH = 23
MAX_SEQUENCE = 2**31 - 1


def fail(message):
    raise SystemExit("firmware_identity: " + message)


project_dir = env.subst("$PROJECT_DIR")  # noqa: F821
pioenv = env.subst("$PIOENV")  # noqa: F821

with open(os.path.join(project_dir, "firmware-boards.json"), encoding="utf-8") as f:
    boards = json.load(f)["boards"]

if pioenv not in boards:
    fail("environment %r has no entry in firmware-boards.json" % pioenv)
board = boards[pioenv]

configured_partitions = env.GetProjectOption("board_build.partitions", "")  # noqa: F821
if os.path.basename(configured_partitions) != board["partitions"]:
    fail(
        "%s uses partitions %r but firmware-boards.json says %r; a layout change "
        "needs a new layout id" % (pioenv, configured_partitions, board["partitions"])
    )

version = os.environ.get("BEAMBOY_FW_VERSION", "")
sequence_text = os.environ.get("BEAMBOY_FW_SEQUENCE", "")
if bool(version) != bool(sequence_text):
    fail("set both BEAMBOY_FW_VERSION and BEAMBOY_FW_SEQUENCE, or neither")

if version:
    if len(version) > MAX_VERSION_LENGTH or not SEMVER.match(version):
        fail("BEAMBOY_FW_VERSION %r is not SemVer (max %d chars)" % (version, MAX_VERSION_LENGTH))
    if not re.match(r"^[1-9][0-9]*$", sequence_text) or int(sequence_text) > MAX_SEQUENCE:
        fail("BEAMBOY_FW_SEQUENCE must be an integer in 1..%d" % MAX_SEQUENCE)
    sequence = int(sequence_text)
else:
    version = "0.0.0-dev"
    sequence = 0

env.Append(  # noqa: F821
    CPPDEFINES=[
        ("BEAMBOY_FW_BOARD", env.StringifyMacro(pioenv)),  # noqa: F821
        ("BEAMBOY_FW_LAYOUT", env.StringifyMacro(board["layout"])),  # noqa: F821
        ("BEAMBOY_FW_VERSION", env.StringifyMacro(version)),  # noqa: F821
        ("BEAMBOY_FW_SEQUENCE", sequence),
        ("BEAMBOY_FW_SLOT_SIZE", board["slot_size"]),
    ]
)

print("Beam Boy firmware identity: %s %s seq %d (%s)" % (pioenv, version, sequence, board["layout"]))
