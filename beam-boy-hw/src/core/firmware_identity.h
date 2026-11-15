#pragma once

// Beam Boy — what this firmware build *is*, for the updater.
//
// Every value here is derived from the build, never typed into a source file:
// scripts/firmware_identity.py reads firmware-boards.json and the release
// environment (BEAMBOY_FW_VERSION / BEAMBOY_FW_SEQUENCE) and passes them in as
// defines. A development build is "0.0.0-dev" with sequence 0, which is older
// than every published release.
//
// The updater compares the signed manifest against these values, so a mistake
// here is a mistake in which image a console will accept. That is why an
// ESP32 build without them is a compile error rather than a silent default.

#include <stdint.h>

#if defined(ARDUINO_ARCH_ESP32) &&                                \
    (!defined(BEAMBOY_FW_BOARD) || !defined(BEAMBOY_FW_LAYOUT) || \
     !defined(BEAMBOY_FW_VERSION) || !defined(BEAMBOY_FW_SEQUENCE) || \
     !defined(BEAMBOY_FW_SLOT_SIZE))
#error "Firmware identity missing: build through platformio.ini, whose extra_scripts runs scripts/firmware_identity.py"
#endif

// Host tests have no partition table; these values exist only so shared code
// compiles there.
#ifndef BEAMBOY_FW_BOARD
#define BEAMBOY_FW_BOARD "native"
#endif
#ifndef BEAMBOY_FW_LAYOUT
#define BEAMBOY_FW_LAYOUT "native"
#endif
#ifndef BEAMBOY_FW_VERSION
#define BEAMBOY_FW_VERSION "0.0.0-dev"
#endif
#ifndef BEAMBOY_FW_SEQUENCE
#define BEAMBOY_FW_SEQUENCE 0
#endif
#ifndef BEAMBOY_FW_SLOT_SIZE
#define BEAMBOY_FW_SLOT_SIZE 0
#endif

#define BEAMBOY_FW_STRINGIFY2(x) #x
#define BEAMBOY_FW_STRINGIFY(x) BEAMBOY_FW_STRINGIFY2(x)

namespace beamboy {
namespace fw {

// PlatformIO environment name, e.g. "adafruit_feather_esp32s3_nopsram".
constexpr char kBoardId[] = BEAMBOY_FW_BOARD;

// Names the exact partition table. An OTA can only target the same layout;
// changing the table is a USB migration.
constexpr char kLayoutId[] = BEAMBOY_FW_LAYOUT;

// Displayed SemVer. Never compared for ordering -- that is kSequence's job.
constexpr char kVersion[] = BEAMBOY_FW_VERSION;

// Monotonic release number. Only a strictly higher sequence is offered.
constexpr uint32_t kSequence = BEAMBOY_FW_SEQUENCE;

// Expected size of each application slot in this layout, in bytes. The
// updater checks the real inactive partition against it before writing.
constexpr uint32_t kSlotSize = BEAMBOY_FW_SLOT_SIZE;

// Version of the update protocol this firmware implements (manifest schema,
// signature format, transport rules). A release can demand a minimum via
// min_updater, forcing an older console through a stepping-stone release.
constexpr uint16_t kUpdaterVersion = 1;

// Version of the Beam script API this firmware gives cartridges. A release
// whose min_api is higher could not run the games already installed here, so
// it is not offered. Bump when the script API changes incompatibly.
constexpr uint16_t kScriptApiVersion = 1;

// One searchable line identifying the build. Printed at boot, and located in
// the .bin by tools/firmware/fwrelease.py to prove an image really is the
// board/layout/version a manifest entry claims. Use it only in main.cpp: each
// file that references it embeds its own copy, and the release tool requires
// exactly one.
constexpr char kBuildTag[] = "beamboy-fw:" BEAMBOY_FW_BOARD ";" BEAMBOY_FW_LAYOUT
                             ";" BEAMBOY_FW_VERSION ";" BEAMBOY_FW_STRINGIFY(
                                 BEAMBOY_FW_SEQUENCE);

}  // namespace fw
}  // namespace beamboy
