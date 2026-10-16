#pragma once

// Beam Boy — firmware over-the-air updates.
//
// This updates the *engine*, not the games. Two different things, easily
// confused:
//
//   Games      -> scripts + metadata written to the LittleFS data partition.
//                 No reboot. A bad one breaks that game only.
//   Firmware   -> this file. Replaces the executable itself: renderer, input,
//                 launcher, network code. Requires a reboot. A bad one bricks
//                 the console.
//
// Flash has two application slots. The console runs from one and streams the
// download into the other, which is not bootable until every byte has been
// checked. Only then is the boot pointer flipped. A stalled, short, long,
// tampered or cancelled download aborts the write and leaves the boot target
// untouched.
//
// Trust does not come from the transport. check() fetches a small envelope
// (https://beamboy.ch/firmware/stable.json), verifies its ECDSA P-256 signature
// against a key compiled into this build (ota_keys.cpp), and only then reads
// the signed payload: which board/layout image exists, its exact size and its
// SHA-256. The image itself may come from GitHub's release hosts; it is
// accepted only if it matches the signed size and hash. TLS (CA bundle, never
// setInsecure()) still authenticates both hosts, but a forged image is caught
// even if TLS were bypassed. Certificate dates are not checked by this
// mbedTLS build (MBEDTLS_HAVE_TIME_DATE is off), so no network time is needed.
//
// Responsiveness: check() blocks for a bounded few seconds (one small HTTPS
// GET), like the Store's index fetch. The install is *pumped*: beginInstall()
// opens the stream, then poll() moves one bounded chunk per call so the caller
// can keep drawing real progress and honour a cancel between chunks.
//
// What this does not do yet (PLAN.md Phase 11 steps 4-5): wire a scene, guard
// engine idle sleep during a write, or defer Arduino's automatic "app valid"
// acknowledgement for health-checked rollback.

#include <Arduino.h>
#include <HTTPClient.h>
#include <WiFiClientSecure.h>
#include <esp_ota_ops.h>
#include <mbedtls/sha256.h>

#include "core/fw_manifest.h"
#include "core/ota_policy.h"

namespace beamboy {

enum class OtaState : uint8_t {
  kIdle,
  kChecking,
  kAvailable,     // A newer compatible signed release exists; nothing written.
  kUpToDate,      // Nothing newer is published.
  kIncompatible,  // Newer release exists but cannot be installed here.
  kStaged,        // An image is already installed and boots on next restart.
  kDownloading,   // Streaming into the inactive slot.
  kVerifying,     // All bytes received; checking hash and image.
  kSuccess,       // Verified and selected; reboot to run it.
  kFailed,
};

// Checked by the caller immediately before confirming an install.
struct InstallPreconditions {
  ota::PowerGate power;
  // Pending saves were committed and storage reports clean.
  bool saves_clean;
};

class Ota {
 public:
  // Forgets everything from a previous visit. Never touches flash or network.
  // Aborts an in-progress install.
  void reset();

  // Fetches and authenticates the manifest. Requires an active WiFi
  // connection. Never writes flash.
  OtaState check();

  // Starts streaming the release found by check() into the inactive slot.
  // Refuses unless the state is kAvailable and the preconditions hold.
  bool beginInstall(const InstallPreconditions& preconditions);

  // Advances an install by at most one bounded step. Call every frame while
  // busy(); returns the new state.
  OtaState poll();

  // Abandons an install between chunks. The boot target is never changed.
  void cancel();

  // A write is in flight; the engine must not sleep, disconnect or reboot.
  bool busy() const {
    return state_ == OtaState::kDownloading || state_ == OtaState::kVerifying;
  }

  OtaState state() const { return state_; }

  // 0..1 of verified bytes written during an install.
  float progress() const { return check_.progress(); }

  // Reason for kFailed/kIncompatible, for serial. The tube cannot spell.
  const char* error() const { return error_; }

  const char* availableVersion() const { return available_version_; }

  // True if a different, already-written slot is selected for the next boot.
  static bool rebootPending();

  static const char* currentVersion();

 private:
  void fail(const char* reason);
  void releaseInstall();
  bool openImageStream();
  void pumpDownload();
  void finishInstall();

  OtaState state_ = OtaState::kIdle;
  const char* error_ = "";
  char available_version_[fw::kMaxVersionLength + 1] = {0};
  fw::BoardImage image_ = {};
  bool image_valid_ = false;

  // Install-time resources, live only between beginInstall() and the end of
  // the install (success, failure or cancel).
  WiFiClientSecure client_;
  HTTPClient http_;
  bool http_open_ = false;
  const esp_partition_t* target_ = nullptr;
  esp_ota_handle_t handle_ = 0;
  bool handle_open_ = false;
  mbedtls_sha256_context sha_;
  bool sha_open_ = false;
  ota::ImageCheck check_;
  uint32_t install_start_ms_ = 0;
  uint32_t last_data_ms_ = 0;
};

}  // namespace beamboy
