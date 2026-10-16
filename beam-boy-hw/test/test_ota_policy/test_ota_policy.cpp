// Tests for the firmware update policy (core/ota_policy.h).
//
// Every rule here guards against installing the wrong thing or starting a
// write that cannot finish: a downgrade, another board's image, an image that
// would drop the saves or the installed games, an unexpected download host, a
// near-empty battery, a stray button press, or a stream that delivers
// something other than the signed bytes. None of those fail loudly on
// hardware until a console is already broken, so all of them are pinned here.

#include <unity.h>

#include <cmath>
#include <cstring>

#include "core/ota_policy.h"

using namespace beamboy;
using namespace beamboy::ota;

namespace {

const char kFeather[] = "adafruit_feather_esp32s3_nopsram";
const char kFeatherLayout[] = "feather-s3-8mb-ota2048k-v1";

fw::BoardImage makeImage(const char* board, const char* layout) {
  fw::BoardImage image;
  memset(&image, 0, sizeof(image));
  strcpy(image.board, board);
  strcpy(image.layout, layout);
  image.min_updater = 1;
  image.min_save = 3;
  image.min_api = 1;
  strcpy(image.url,
         "https://github.com/jakopako/beam-boy/releases/download/fw-v1.2.3/"
         "beamboy-fw-1.2.3-adafruit_feather_esp32s3_nopsram.bin");
  image.size = 1200000;
  return image;
}

fw::Release makeRelease(uint32_t sequence = 8) {
  fw::Release release;
  memset(&release, 0, sizeof(release));
  strcpy(release.channel, "stable");
  strcpy(release.version, "1.2.3");
  release.sequence = sequence;
  release.boards[0] =
      makeImage("esp32-s3-devkitc-1-n16r8", "devkitc-s3-16mb-ota6400k-v1");
  release.boards[1] = makeImage(kFeather, kFeatherLayout);
  release.board_count = 2;
  return release;
}

DeviceIdentity makeDevice(uint32_t sequence = 7) {
  DeviceIdentity device = {kFeather, kFeatherLayout, sequence, 1, 3, 1,
                           2097152};
  return device;
}

ReleaseVerdict evaluate(const fw::Release& release,
                        const DeviceIdentity& device) {
  const fw::BoardImage* image = nullptr;
  const ReleaseVerdict verdict = evaluateRelease(release, device, image);
  // The image pointer is only ever handed out alongside kAvailable.
  TEST_ASSERT_EQUAL(verdict == ReleaseVerdict::kAvailable, image != nullptr);
  return verdict;
}

}  // namespace

void setUp() {}
void tearDown() {}

// --- Release selection -------------------------------------------------------

void test_offers_a_newer_release_for_this_board() {
  const fw::Release release = makeRelease();
  const fw::BoardImage* image = nullptr;
  TEST_ASSERT_EQUAL(ReleaseVerdict::kAvailable,
                    evaluateRelease(release, makeDevice(), image));
  // The entry for *this* board, not merely the first one.
  TEST_ASSERT_EQUAL_PTR(&release.boards[1], image);
}

void test_a_development_build_is_older_than_any_release() {
  TEST_ASSERT_EQUAL(ReleaseVerdict::kAvailable,
                    evaluate(makeRelease(1), makeDevice(0)));
}

void test_same_sequence_is_up_to_date_and_lower_is_never_a_downgrade() {
  TEST_ASSERT_EQUAL(ReleaseVerdict::kUpToDate,
                    evaluate(makeRelease(7), makeDevice(7)));
  TEST_ASSERT_EQUAL(ReleaseVerdict::kOlder,
                    evaluate(makeRelease(6), makeDevice(7)));
}

void test_up_to_date_wins_over_incompatibility() {
  fw::Release release = makeRelease(7);
  release.board_count = 1;  // Drop the Feather entry.
  TEST_ASSERT_EQUAL(ReleaseVerdict::kUpToDate,
                    evaluate(release, makeDevice(7)));
}

void test_only_stable_non_prerelease_releases_are_offered() {
  fw::Release release = makeRelease();
  strcpy(release.channel, "beta");
  TEST_ASSERT_EQUAL(ReleaseVerdict::kWrongChannel,
                    evaluate(release, makeDevice()));
  release = makeRelease();
  strcpy(release.version, "1.2.3-rc.1");
  TEST_ASSERT_EQUAL(ReleaseVerdict::kPrerelease,
                    evaluate(release, makeDevice()));
}

void test_never_offers_another_boards_image_or_layout() {
  fw::Release release = makeRelease();
  release.board_count = 1;
  TEST_ASSERT_EQUAL(ReleaseVerdict::kNoImageForBoard,
                    evaluate(release, makeDevice()));

  release = makeRelease();
  strcpy(release.boards[1].layout, "feather-s3-8mb-ota3072k-v2");
  TEST_ASSERT_EQUAL(ReleaseVerdict::kWrongLayout,
                    evaluate(release, makeDevice()));
}

void test_minimum_versions_protect_updater_saves_and_games() {
  fw::Release release = makeRelease();
  release.boards[1].min_updater = 2;
  TEST_ASSERT_EQUAL(ReleaseVerdict::kNeedsNewerUpdater,
                    evaluate(release, makeDevice()));

  release = makeRelease();
  release.boards[1].min_save = 4;
  TEST_ASSERT_EQUAL(ReleaseVerdict::kSaveIncompatible,
                    evaluate(release, makeDevice()));

  release = makeRelease();
  release.boards[1].min_api = 2;
  TEST_ASSERT_EQUAL(ReleaseVerdict::kApiIncompatible,
                    evaluate(release, makeDevice()));

  // Older minimums are fine: the new firmware still reads what we have.
  release = makeRelease();
  release.boards[1].min_save = 1;
  release.boards[1].min_api = 0;
  release.boards[1].min_updater = 0;
  TEST_ASSERT_EQUAL(ReleaseVerdict::kAvailable,
                    evaluate(release, makeDevice()));
}

void test_image_must_fit_the_inactive_slot() {
  fw::Release release = makeRelease();
  release.boards[1].size = 2097152;
  TEST_ASSERT_EQUAL(ReleaseVerdict::kAvailable,
                    evaluate(release, makeDevice()));
  release.boards[1].size = 2097153;
  TEST_ASSERT_EQUAL(ReleaseVerdict::kTooLarge,
                    evaluate(release, makeDevice()));

  DeviceIdentity no_slot = makeDevice();
  no_slot.slot_size = 0;
  TEST_ASSERT_EQUAL(ReleaseVerdict::kTooLarge,
                    evaluate(makeRelease(), no_slot));
}

void test_image_url_must_be_an_allowed_host() {
  fw::Release release = makeRelease();
  strcpy(release.boards[1].url, "https://example.com/firmware.bin");
  TEST_ASSERT_EQUAL(ReleaseVerdict::kUntrustedUrl,
                    evaluate(release, makeDevice()));
}

void test_every_verdict_has_a_description() {
  for (int v = 0; v <= static_cast<int>(ReleaseVerdict::kUntrustedUrl); v++) {
    TEST_ASSERT_NOT_EQUAL(
        0, strcmp("unknown verdict", describe(static_cast<ReleaseVerdict>(v))));
  }
}

// --- Transport -----------------------------------------------------------------

void test_allows_only_exact_https_hosts() {
  TEST_ASSERT_TRUE(isAllowedUrl("https://github.com/a/b/releases/download/x"));
  TEST_ASSERT_TRUE(isAllowedUrl(
      "https://release-assets.githubusercontent.com/github-production-release-"
      "asset/1?sp=r&sv=2018&sig=abc%2B&response-content-disposition=x"));
  TEST_ASSERT_TRUE(
      isAllowedUrl("https://objects.githubusercontent.com/github-production"));
  TEST_ASSERT_TRUE(isAllowedUrl("https://beamboy.ch/firmware/stable.json"));
  TEST_ASSERT_TRUE(isAllowedUrl("https://GitHub.COM/a"));
  TEST_ASSERT_TRUE(isAllowedUrl("https://github.com"));
  TEST_ASSERT_TRUE(isAllowedUrl("https://github.com/a?x=1#y"));
}

void test_rejects_lookalike_and_tricky_urls() {
  TEST_ASSERT_FALSE(isAllowedUrl(nullptr));
  TEST_ASSERT_FALSE(isAllowedUrl(""));
  TEST_ASSERT_FALSE(isAllowedUrl("http://github.com/a"));
  TEST_ASSERT_FALSE(isAllowedUrl("HTTPS://github.com/a"));
  TEST_ASSERT_FALSE(isAllowedUrl("/relative/path"));
  TEST_ASSERT_FALSE(isAllowedUrl("https://"));
  TEST_ASSERT_FALSE(isAllowedUrl("https:///github.com"));
  TEST_ASSERT_FALSE(isAllowedUrl("https://evilgithub.com/a"));
  // Prefixes of allowed hosts are different hosts.
  TEST_ASSERT_FALSE(isAllowedUrl("https://github.co/a"));
  TEST_ASSERT_FALSE(isAllowedUrl("https://beamboy.c/a"));
  TEST_ASSERT_FALSE(isAllowedUrl("https://g/a"));
  TEST_ASSERT_FALSE(isAllowedUrl("https://github.com.evil.example/a"));
  TEST_ASSERT_FALSE(isAllowedUrl("https://api.github.com/a"));
  TEST_ASSERT_FALSE(isAllowedUrl("https://github.com@evil.example/a"));
  TEST_ASSERT_FALSE(isAllowedUrl("https://evil.example@github.com/a"));
  TEST_ASSERT_FALSE(isAllowedUrl("https://github.com:443/a"));
  TEST_ASSERT_FALSE(isAllowedUrl("https://github.com:8443/a"));
  TEST_ASSERT_FALSE(isAllowedUrl("https://github.com\\@evil.example/a"));
  TEST_ASSERT_FALSE(isAllowedUrl("https://github.com /a"));
  TEST_ASSERT_FALSE(isAllowedUrl("https://git\nhub.com/a"));
  TEST_ASSERT_FALSE(isAllowedUrl("https://evil.example/?u=https://github.com"));
  // HTTPClient takes the authority up to the first '/', so a '?' or '#'
  // before it would turn the allowed host into userinfo for another host.
  TEST_ASSERT_FALSE(isAllowedUrl("https://github.com?@evil.example/x"));
  TEST_ASSERT_FALSE(isAllowedUrl("https://github.com#@evil.example:8443/x"));
  TEST_ASSERT_FALSE(isAllowedUrl("https://github.com?x=1"));
}

void test_redirect_statuses() {
  TEST_ASSERT_TRUE(isRedirectStatus(301));
  TEST_ASSERT_TRUE(isRedirectStatus(302));
  TEST_ASSERT_TRUE(isRedirectStatus(303));
  TEST_ASSERT_TRUE(isRedirectStatus(307));
  TEST_ASSERT_TRUE(isRedirectStatus(308));
  TEST_ASSERT_FALSE(isRedirectStatus(200));
  TEST_ASSERT_FALSE(isRedirectStatus(304));
  TEST_ASSERT_FALSE(isRedirectStatus(404));
  TEST_ASSERT_FALSE(isRedirectStatus(-1));
}

// --- Power -----------------------------------------------------------------------

void test_usb_only_boards_need_no_battery() {
  TEST_ASSERT_EQUAL(PowerGate::kOk,
                    installPowerGate(false, false, 999999, 0.0f,
                                     PowerLevel::kCritical));
}

void test_battery_boards_need_a_fresh_valid_reading() {
  TEST_ASSERT_EQUAL(PowerGate::kUnavailable,
                    installPowerGate(true, false, 0, 90.0f,
                                     PowerLevel::kNormal));
  TEST_ASSERT_EQUAL(PowerGate::kUnavailable,
                    installPowerGate(true, true, 0, NAN, PowerLevel::kNormal));
  TEST_ASSERT_EQUAL(PowerGate::kUnavailable,
                    installPowerGate(true, true, 0, 101.0f,
                                     PowerLevel::kNormal));
  TEST_ASSERT_EQUAL(PowerGate::kOk,
                    installPowerGate(true, true, kMaxBatteryReadingAgeMs,
                                     90.0f, PowerLevel::kNormal));
  TEST_ASSERT_EQUAL(PowerGate::kStale,
                    installPowerGate(true, true, kMaxBatteryReadingAgeMs + 1,
                                     90.0f, PowerLevel::kNormal));
}

void test_battery_threshold_is_thirty_percent() {
  TEST_ASSERT_EQUAL(PowerGate::kOk,
                    installPowerGate(true, true, 0, 30.0f,
                                     PowerLevel::kNormal));
  TEST_ASSERT_EQUAL(PowerGate::kTooLow,
                    installPowerGate(true, true, 0, 29.9f,
                                     PowerLevel::kNormal));
  TEST_ASSERT_EQUAL(PowerGate::kTooLow,
                    installPowerGate(true, true, 0, 50.0f, PowerLevel::kLow));
  TEST_ASSERT_EQUAL(PowerGate::kCritical,
                    installPowerGate(true, true, 0, 50.0f,
                                     PowerLevel::kCritical));
}

// --- Confirmation ---------------------------------------------------------------

void test_a_press_carried_into_the_scene_never_confirms() {
  ConfirmHold hold;
  hold.reset();
  for (uint32_t t = 0; t <= 10000; t += 16) {
    TEST_ASSERT_EQUAL(ConfirmHold::Event::kNone, hold.update(true, t));
  }
  TEST_ASSERT_FALSE(hold.confirmed());
  TEST_ASSERT_EQUAL_FLOAT(0.0f, hold.progress(10000));
}

void test_a_deliberate_hold_confirms_at_exactly_the_threshold() {
  ConfirmHold hold;
  hold.reset();
  hold.update(false, 0);
  TEST_ASSERT_EQUAL(ConfirmHold::Event::kStarted, hold.update(true, 100));
  TEST_ASSERT_TRUE(hold.holding());
  TEST_ASSERT_FLOAT_WITHIN(0.001f, 0.5f, hold.progress(700));
  TEST_ASSERT_EQUAL(ConfirmHold::Event::kNone,
                    hold.update(true, 100 + kConfirmHoldMs - 1));
  TEST_ASSERT_FALSE(hold.confirmed());
  TEST_ASSERT_EQUAL(ConfirmHold::Event::kConfirmed,
                    hold.update(true, 100 + kConfirmHoldMs));
  TEST_ASSERT_TRUE(hold.confirmed());
  // Latched: holding on, releasing or pressing again does nothing more.
  TEST_ASSERT_EQUAL(ConfirmHold::Event::kNone, hold.update(true, 5000));
  TEST_ASSERT_EQUAL(ConfirmHold::Event::kNone, hold.update(false, 5016));
  TEST_ASSERT_EQUAL(ConfirmHold::Event::kNone, hold.update(true, 5032));
}

void test_releasing_early_cancels_and_restarts_from_zero() {
  ConfirmHold hold;
  hold.reset();
  hold.update(false, 0);
  hold.update(true, 10);
  hold.update(true, 1000);
  TEST_ASSERT_EQUAL(ConfirmHold::Event::kCancelled, hold.update(false, 1100));
  TEST_ASSERT_FALSE(hold.holding());
  TEST_ASSERT_EQUAL_FLOAT(0.0f, hold.progress(1100));

  // A second press starts a full new hold; time already held does not carry.
  TEST_ASSERT_EQUAL(ConfirmHold::Event::kStarted, hold.update(true, 2000));
  TEST_ASSERT_EQUAL(ConfirmHold::Event::kNone, hold.update(true, 3000));
  TEST_ASSERT_EQUAL(ConfirmHold::Event::kConfirmed, hold.update(true, 3200));
}

void test_reset_requires_a_fresh_release() {
  ConfirmHold hold;
  hold.reset();
  hold.update(false, 0);
  hold.update(true, 10);
  hold.update(true, 10 + kConfirmHoldMs);
  TEST_ASSERT_TRUE(hold.confirmed());

  hold.reset();
  TEST_ASSERT_FALSE(hold.confirmed());
  TEST_ASSERT_EQUAL(ConfirmHold::Event::kNone, hold.update(true, 5000));
  TEST_ASSERT_EQUAL(ConfirmHold::Event::kNone, hold.update(true, 9000));
  TEST_ASSERT_FALSE(hold.confirmed());
}

void test_hold_survives_millis_rollover() {
  ConfirmHold hold;
  hold.reset();
  const uint32_t start = 0xFFFFFF00u;
  hold.update(false, start);
  hold.update(true, start + 1);
  TEST_ASSERT_EQUAL(ConfirmHold::Event::kNone, hold.update(true, 500));
  TEST_ASSERT_EQUAL(ConfirmHold::Event::kConfirmed,
                    hold.update(true, start + 1 + kConfirmHoldMs));
}

// --- Image stream -------------------------------------------------------------

void test_accepts_exactly_the_signed_image() {
  uint8_t data[100];
  memset(data, 0x5a, sizeof(data));
  data[0] = kEspImageMagic;
  uint8_t digest[fw::kSha256Bytes];
  memset(digest, 0x11, sizeof(digest));

  ImageCheck check;
  check.begin(100);
  TEST_ASSERT_EQUAL(ImageError::kNone, check.accept(data, 40));
  TEST_ASSERT_FLOAT_WITHIN(0.001f, 0.4f, check.progress());
  TEST_ASSERT_EQUAL(ImageError::kNone, check.accept(data + 40, 0));
  // Only the very first byte is the header; later chunks may start with
  // anything.
  TEST_ASSERT_EQUAL(ImageError::kNone, check.accept(data + 40, 60));
  TEST_ASSERT_EQUAL_UINT32(100, check.received());
  TEST_ASSERT_EQUAL(ImageError::kNone, check.finish(digest, digest));
  TEST_ASSERT_FLOAT_WITHIN(0.001f, 1.0f, check.progress());
}

void test_rejects_bad_header_extra_bytes_truncation_and_bad_hash() {
  uint8_t data[64];
  memset(data, 0, sizeof(data));
  uint8_t digest[fw::kSha256Bytes];
  uint8_t other[fw::kSha256Bytes];
  memset(digest, 0x22, sizeof(digest));
  memcpy(other, digest, sizeof(other));
  other[31] ^= 0x01;

  ImageCheck check;
  check.begin(64);
  TEST_ASSERT_EQUAL(ImageError::kBadHeader, check.accept(data, 64));

  data[0] = kEspImageMagic;
  check.begin(32);
  TEST_ASSERT_EQUAL(ImageError::kTooLong, check.accept(data, 33));
  TEST_ASSERT_EQUAL(ImageError::kNone, check.accept(data, 32));
  TEST_ASSERT_EQUAL(ImageError::kTooLong, check.accept(data, 1));

  check.begin(64);
  TEST_ASSERT_EQUAL(ImageError::kNone, check.accept(data, 63));
  TEST_ASSERT_EQUAL(ImageError::kTruncated, check.finish(digest, digest));

  check.begin(64);
  TEST_ASSERT_EQUAL(ImageError::kNone, check.accept(data, 64));
  TEST_ASSERT_EQUAL(ImageError::kHashMismatch, check.finish(digest, other));
}

int main(int, char**) {
  UNITY_BEGIN();
  RUN_TEST(test_offers_a_newer_release_for_this_board);
  RUN_TEST(test_a_development_build_is_older_than_any_release);
  RUN_TEST(test_same_sequence_is_up_to_date_and_lower_is_never_a_downgrade);
  RUN_TEST(test_up_to_date_wins_over_incompatibility);
  RUN_TEST(test_only_stable_non_prerelease_releases_are_offered);
  RUN_TEST(test_never_offers_another_boards_image_or_layout);
  RUN_TEST(test_minimum_versions_protect_updater_saves_and_games);
  RUN_TEST(test_image_must_fit_the_inactive_slot);
  RUN_TEST(test_image_url_must_be_an_allowed_host);
  RUN_TEST(test_every_verdict_has_a_description);
  RUN_TEST(test_allows_only_exact_https_hosts);
  RUN_TEST(test_rejects_lookalike_and_tricky_urls);
  RUN_TEST(test_redirect_statuses);
  RUN_TEST(test_usb_only_boards_need_no_battery);
  RUN_TEST(test_battery_boards_need_a_fresh_valid_reading);
  RUN_TEST(test_battery_threshold_is_thirty_percent);
  RUN_TEST(test_a_press_carried_into_the_scene_never_confirms);
  RUN_TEST(test_a_deliberate_hold_confirms_at_exactly_the_threshold);
  RUN_TEST(test_releasing_early_cancels_and_restarts_from_zero);
  RUN_TEST(test_reset_requires_a_fresh_release);
  RUN_TEST(test_hold_survives_millis_rollover);
  RUN_TEST(test_accepts_exactly_the_signed_image);
  RUN_TEST(test_rejects_bad_header_extra_bytes_truncation_and_bad_hash);
  return UNITY_END();
}
