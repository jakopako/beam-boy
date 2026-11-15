// Tests for the firmware-update screen: its state machine (scenes/update_flow.h)
// and what each state looks like on the tube (scenes/update_firmware_view.h).
//
// The scene that wires these to WiFi and flash only runs on the device, so
// every decision it makes is taken here instead: when the radio is wanted,
// that a press carried into the screen never confirms an install, that the
// confirming hold can never also restart the console, that every failure can
// be retried, and that each state is visibly different.

#include <unity.h>

#include <cstring>
#include <set>
#include <string>

#include "scenes/update_firmware_view.h"
#include "scenes/update_flow.h"

using namespace beamboy;
using namespace beamboy::update;

namespace {

Frame frame(uint32_t now, bool a_down = false, bool a_pressed = false,
            bool connected = false, bool failed = false) {
  Frame f = {now, a_down, a_pressed, connected, failed};
  return f;
}

// Walks a fresh flow to kAvailable at t=1000 with A up.
Flow available() {
  Flow flow;
  flow.start(true, 0);
  TEST_ASSERT_EQUAL(Action::kCheck,
                    flow.update(frame(500, false, false, true)));
  flow.onCheck(CheckOutcome::kAvailable, 1000);
  TEST_ASSERT_EQUAL(Stage::kAvailable, flow.stage());
  return flow;
}

// Holds A from `start` for the full confirmation and returns the action.
Action holdToConfirm(Flow& flow, uint32_t start) {
  flow.update(frame(start - 16, false, false, true));
  Action action = flow.update(frame(start, true, true, true));
  for (uint32_t t = start + 16; t <= start + ota::kConfirmHoldMs + 16;
       t += 16) {
    const Action a = flow.update(frame(t, true, false, true));
    if (a != Action::kNone) action = a;
  }
  return action;
}

}  // namespace

void setUp() {}
void tearDown() {}

// --- Flow ------------------------------------------------------------------

void test_without_credentials_nothing_connects() {
  Flow flow;
  flow.start(false, 0);
  TEST_ASSERT_EQUAL(Stage::kNoCredentials, flow.stage());
  TEST_ASSERT_FALSE(flow.radioNeeded());
  TEST_ASSERT_EQUAL(Action::kNone, flow.update(frame(100, true, true)));
  TEST_ASSERT_EQUAL(Action::kNone, flow.update(frame(5000, false)));
  TEST_ASSERT_EQUAL(Stage::kNoCredentials, flow.stage());
}

void test_happy_path_from_entry_to_reboot() {
  Flow flow;
  flow.start(true, 0);
  TEST_ASSERT_EQUAL(Stage::kConnecting, flow.stage());
  TEST_ASSERT_TRUE(flow.radioNeeded());
  TEST_ASSERT_EQUAL(Action::kNone, flow.update(frame(100)));

  TEST_ASSERT_EQUAL(Action::kCheck,
                    flow.update(frame(200, false, false, true)));
  TEST_ASSERT_EQUAL(Stage::kChecking, flow.stage());
  flow.onCheck(CheckOutcome::kAvailable, 300);
  TEST_ASSERT_EQUAL(Stage::kAvailable, flow.stage());
  TEST_ASSERT_TRUE(flow.radioNeeded());

  TEST_ASSERT_EQUAL(Action::kInstall, holdToConfirm(flow, 400));
  flow.onInstallStarted(true, Failure::kNone, 1700);
  TEST_ASSERT_EQUAL(Stage::kDownloading, flow.stage());
  TEST_ASSERT_TRUE(flow.installing());
  TEST_ASSERT_TRUE(flow.radioNeeded());

  flow.onInstall(InstallOutcome::kDownloading, 1800);
  TEST_ASSERT_EQUAL(Stage::kDownloading, flow.stage());
  flow.onInstall(InstallOutcome::kVerifying, 9000);
  TEST_ASSERT_EQUAL(Stage::kVerifying, flow.stage());
  TEST_ASSERT_TRUE(flow.installing());
  flow.onInstall(InstallOutcome::kSuccess, 9500);
  TEST_ASSERT_EQUAL(Stage::kSuccess, flow.stage());
  TEST_ASSERT_FALSE(flow.installing());
  TEST_ASSERT_FALSE(flow.radioNeeded());

  // A is still down from confirming: holding on must not restart.
  TEST_ASSERT_EQUAL(Action::kNone, flow.update(frame(9600, true)));
  TEST_ASSERT_EQUAL(Action::kNone, flow.update(frame(9700, false)));
  TEST_ASSERT_EQUAL(Action::kReboot, flow.update(frame(9800, true, true)));
}

void test_a_press_carried_into_available_never_confirms() {
  Flow flow;
  flow.start(true, 0);
  flow.update(frame(100, false, false, true));
  // The retry/entry press is still held when the check comes back.
  flow.onCheck(CheckOutcome::kAvailable, 200);
  for (uint32_t t = 216; t < 10000; t += 16) {
    TEST_ASSERT_EQUAL(Action::kNone, flow.update(frame(t, true, false, true)));
  }
  TEST_ASSERT_EQUAL_FLOAT(0.0f, flow.confirmProgress(10000));
}

void test_releasing_early_cancels_the_confirmation() {
  Flow flow = available();
  flow.update(frame(1016, false, false, true));
  flow.update(frame(1032, true, true, true));
  flow.update(frame(1632, true, false, true));
  TEST_ASSERT_TRUE(flow.confirmProgress(1632) > 0.4f);
  TEST_ASSERT_EQUAL(Action::kNone,
                    flow.update(frame(1648, false, false, true)));
  TEST_ASSERT_EQUAL_FLOAT(0.0f, flow.confirmProgress(1648));
  TEST_ASSERT_EQUAL(Stage::kAvailable, flow.stage());
}

void test_failures_record_their_reason_and_retry_restarts() {
  Flow flow;
  flow.start(true, 0);
  flow.update(frame(100, false, false, false, true));
  TEST_ASSERT_EQUAL(Stage::kFailed, flow.stage());
  TEST_ASSERT_EQUAL(Failure::kConnect, flow.failure());
  TEST_ASSERT_FALSE(flow.radioNeeded());

  // Holding A is not a retry; a press is.
  TEST_ASSERT_EQUAL(Action::kNone, flow.update(frame(200, true, false)));
  TEST_ASSERT_EQUAL(Action::kStart, flow.update(frame(300, true, true)));
  flow.start(true, 300);
  TEST_ASSERT_EQUAL(Stage::kConnecting, flow.stage());
  TEST_ASSERT_EQUAL(Failure::kNone, flow.failure());
}

void test_check_outcomes_map_to_stages() {
  Flow flow;
  flow.start(true, 0);
  flow.update(frame(1, false, false, true));
  flow.onCheck(CheckOutcome::kFailed, 2);
  TEST_ASSERT_EQUAL(Failure::kCheck, flow.failure());

  flow.start(true, 0);
  flow.update(frame(1, false, false, true));
  flow.onCheck(CheckOutcome::kIncompatible, 2);
  TEST_ASSERT_EQUAL(Stage::kFailed, flow.stage());
  TEST_ASSERT_EQUAL(Failure::kIncompatible, flow.failure());

  flow.start(true, 0);
  flow.update(frame(1, false, false, true));
  flow.onCheck(CheckOutcome::kUpToDate, 2);
  TEST_ASSERT_EQUAL(Stage::kUpToDate, flow.stage());
  TEST_ASSERT_FALSE(flow.radioNeeded());
  TEST_ASSERT_EQUAL(Action::kNone, flow.update(frame(3, true, true)));
}

void test_a_staged_image_is_reported_and_can_be_restarted_into() {
  Flow flow;
  flow.start(true, 0);
  flow.update(frame(1, false, false, true));
  flow.onCheck(CheckOutcome::kStaged, 2);
  TEST_ASSERT_EQUAL(Stage::kStaged, flow.stage());
  TEST_ASSERT_FALSE(flow.radioNeeded());
  // The press that started the check is still down.
  TEST_ASSERT_EQUAL(Action::kNone, flow.update(frame(3, true, true)));
  TEST_ASSERT_EQUAL(Action::kNone, flow.update(frame(4, false)));
  TEST_ASSERT_EQUAL(Action::kReboot, flow.update(frame(5, true, true)));
}

void test_install_refusals_and_failures() {
  Flow flow = available();
  flow.onInstallStarted(false, Failure::kPower, 2000);
  TEST_ASSERT_EQUAL(Failure::kPower, flow.failure());

  flow = available();
  flow.onInstallStarted(false, Failure::kInstall, 2000);
  TEST_ASSERT_EQUAL(Failure::kInstall, flow.failure());

  flow = available();
  flow.onInstallStarted(true, Failure::kNone, 2000);
  flow.onInstall(InstallOutcome::kFailed, 3000);
  TEST_ASSERT_EQUAL(Stage::kFailed, flow.stage());
  TEST_ASSERT_EQUAL(Failure::kInstall, flow.failure());
  TEST_ASSERT_FALSE(flow.installing());
  TEST_ASSERT_FALSE(flow.radioNeeded());
}

void test_losing_wifi_while_waiting_fails_as_connection() {
  Flow flow = available();
  flow.update(frame(1100, false, false, false, true));
  TEST_ASSERT_EQUAL(Stage::kFailed, flow.stage());
  TEST_ASSERT_EQUAL(Failure::kConnect, flow.failure());
}

void test_results_for_other_stages_are_ignored() {
  Flow flow;
  flow.start(true, 0);
  flow.onCheck(CheckOutcome::kAvailable, 1);  // Not checking yet.
  TEST_ASSERT_EQUAL(Stage::kConnecting, flow.stage());
  flow.onInstallStarted(true, Failure::kNone, 2);  // Nothing available.
  TEST_ASSERT_EQUAL(Stage::kConnecting, flow.stage());
  flow.onInstall(InstallOutcome::kSuccess, 3);  // Not installing.
  TEST_ASSERT_EQUAL(Stage::kConnecting, flow.stage());
}

void test_stage_time_restarts_on_every_change() {
  Flow flow;
  flow.start(true, 1000);
  TEST_ASSERT_EQUAL_UINT32(500, flow.stageMs(1500));
  flow.update(frame(2000, false, false, true));
  TEST_ASSERT_EQUAL_UINT32(0, flow.stageMs(2000));
}

// A critical battery stops the radio and any install; if USB then cancels the
// shutdown, the screen must not keep waiting on a radio that is off.
void test_abort_fails_work_in_progress_but_keeps_results() {
  Flow connecting;
  connecting.start(true, 0);
  connecting.abort(Failure::kPower, 10);
  TEST_ASSERT_EQUAL(Stage::kFailed, connecting.stage());
  TEST_ASSERT_EQUAL(Failure::kPower, connecting.failure());
  TEST_ASSERT_FALSE(connecting.radioNeeded());
  TEST_ASSERT_EQUAL(Action::kStart, connecting.update(frame(20, true, true)));

  Flow waiting = available();
  waiting.abort(Failure::kPower, 2000);
  TEST_ASSERT_EQUAL(Stage::kFailed, waiting.stage());

  Flow downloading = available();
  downloading.onInstallStarted(true, Failure::kNone, 2000);
  downloading.abort(Failure::kPower, 3000);
  TEST_ASSERT_EQUAL(Stage::kFailed, downloading.stage());
  TEST_ASSERT_FALSE(downloading.installing());

  Flow installed = available();
  installed.onInstallStarted(true, Failure::kNone, 2000);
  installed.onInstall(InstallOutcome::kSuccess, 3000);
  installed.abort(Failure::kPower, 4000);
  TEST_ASSERT_EQUAL(Stage::kSuccess, installed.stage());

  Flow no_credentials;
  no_credentials.start(false, 0);
  no_credentials.abort(Failure::kPower, 10);
  TEST_ASSERT_EQUAL(Stage::kNoCredentials, no_credentials.stage());
}

// --- View ------------------------------------------------------------------

void test_pulse_rhythms_have_the_right_count() {
  for (uint8_t count = 1; count <= 4; count++) {
    int rises = 0;
    bool was = false;
    for (uint32_t t = 0; t < kPulseCycleMs; t += 10) {
      const bool on = pulseOn(t, count, kPulseCycleMs);
      if (on && !was) rises++;
      was = on;
    }
    TEST_ASSERT_EQUAL_INT(count, rises);
  }
  TEST_ASSERT_FALSE(pulseOn(0, 0, kPulseCycleMs));
}

void test_every_failure_has_its_own_rhythm() {
  const Failure failures[] = {Failure::kConnect, Failure::kCheck,
                              Failure::kIncompatible, Failure::kInstall,
                              Failure::kPower};
  std::set<uint8_t> seen;
  for (const Failure f : failures) seen.insert(failurePulses(f));
  TEST_ASSERT_EQUAL_UINT32(5, seen.size());
  TEST_ASSERT_EQUAL_UINT8(0, failurePulses(Failure::kPower));
}

namespace {

Display& freshDisplay() {
  static Display display;
  display.begin();
  display.setBrightness(128);
  return display;
}

std::string frameOf(const View& view) {
  Display& display = freshDisplay();
  render(display, view);
  display.present();
  std::string bytes;
  for (uint16_t i = 0; i < display.pixelCount(); i++) {
    const RgbColor& c = display.shownPixel(i);
    bytes.push_back(static_cast<char>(c.R));
    bytes.push_back(static_cast<char>(c.G));
    bytes.push_back(static_cast<char>(c.B));
  }
  return bytes;
}

RgbColor pixelAt(const View& view, float position) {
  Display& display = freshDisplay();
  render(display, view);
  display.present();
  return display.shownPixel(
      static_cast<uint16_t>(position * (display.pixelCount() - 1) + 0.5f));
}

View viewOf(Stage stage, uint32_t ms = 0, Failure failure = Failure::kNone,
            float progress = 0.0f, float confirm = 0.0f) {
  View view = {stage, failure, ms, progress, confirm};
  return view;
}

}  // namespace

void test_no_credentials_shows_two_amber_bars_with_a_dark_gap() {
  const View view = viewOf(Stage::kNoCredentials);
  const RgbColor left = pixelAt(view, 0.2f);
  const RgbColor right = pixelAt(view, 0.8f);
  const RgbColor gap = pixelAt(view, 0.5f);
  TEST_ASSERT_TRUE(left.R > left.G && left.G > 0 && left.B == 0);
  TEST_ASSERT_TRUE(right.R > right.G && right.G > 0 && right.B == 0);
  TEST_ASSERT_EQUAL_INT(0, gap.R + gap.G + gap.B);
}

void test_download_fill_follows_real_progress() {
  const View half = viewOf(Stage::kDownloading, 0, Failure::kNone, 0.5f);
  const RgbColor filled = pixelAt(half, 0.25f);
  const RgbColor empty = pixelAt(half, 0.75f);
  TEST_ASSERT_TRUE(filled.B > 4 * empty.B);
  TEST_ASSERT_TRUE(empty.B > 0);  // The whole bar stays faintly visible.

  const RgbColor at_zero = pixelAt(viewOf(Stage::kDownloading), 0.25f);
  TEST_ASSERT_EQUAL_UINT8(empty.B, at_zero.B);
}

void test_confirm_overlay_whitens_from_the_start() {
  const View view = viewOf(Stage::kAvailable, 0, Failure::kNone, 0.0f, 0.5f);
  const RgbColor held = pixelAt(view, 0.1f);
  const RgbColor rest = pixelAt(view, 0.9f);
  TEST_ASSERT_TRUE(held.R > rest.R + 50);
}

void test_success_fills_from_both_ends_then_beats_in_the_centre() {
  const RgbColor start_centre = pixelAt(viewOf(Stage::kSuccess, 0), 0.5f);
  TEST_ASSERT_EQUAL_UINT8(0, start_centre.G);
  const RgbColor mid_end = pixelAt(viewOf(Stage::kSuccess, 500), 0.0f);
  const RgbColor mid_centre = pixelAt(viewOf(Stage::kSuccess, 500), 0.5f);
  TEST_ASSERT_TRUE(mid_end.G > 0);
  TEST_ASSERT_EQUAL_UINT8(0, mid_centre.R);
  const RgbColor beat = pixelAt(viewOf(Stage::kSuccess, kSuccessFillMs), 0.5f);
  TEST_ASSERT_GREATER_THAN_UINT8(100, beat.R);  // White centre.
  TEST_ASSERT_GREATER_THAN_UINT8(100, beat.B);
}

void test_failures_are_red() {
  const RgbColor c =
      pixelAt(viewOf(Stage::kFailed, 0, Failure::kConnect), 0.5f);
  TEST_ASSERT_TRUE(c.R > 4 * c.G);
  TEST_ASSERT_TRUE(c.R > 4 * c.B);
}

void test_every_stage_looks_different() {
  const View views[] = {
      viewOf(Stage::kNoCredentials, 0),
      viewOf(Stage::kConnecting, 100),
      viewOf(Stage::kChecking, 0),
      viewOf(Stage::kUpToDate, 2000),
      viewOf(Stage::kAvailable, 0),
      viewOf(Stage::kDownloading, 0, Failure::kNone, 0.3f),
      viewOf(Stage::kVerifying, 100),
      viewOf(Stage::kSuccess, 300),
      viewOf(Stage::kStaged, 0),
      viewOf(Stage::kFailed, 0, Failure::kConnect),
  };
  std::set<std::string> frames;
  for (const View& view : views) frames.insert(frameOf(view));
  TEST_ASSERT_EQUAL_UINT32(sizeof(views) / sizeof(views[0]), frames.size());
}

int main(int, char**) {
  UNITY_BEGIN();
  RUN_TEST(test_without_credentials_nothing_connects);
  RUN_TEST(test_happy_path_from_entry_to_reboot);
  RUN_TEST(test_a_press_carried_into_available_never_confirms);
  RUN_TEST(test_releasing_early_cancels_the_confirmation);
  RUN_TEST(test_failures_record_their_reason_and_retry_restarts);
  RUN_TEST(test_check_outcomes_map_to_stages);
  RUN_TEST(test_a_staged_image_is_reported_and_can_be_restarted_into);
  RUN_TEST(test_install_refusals_and_failures);
  RUN_TEST(test_losing_wifi_while_waiting_fails_as_connection);
  RUN_TEST(test_results_for_other_stages_are_ignored);
  RUN_TEST(test_stage_time_restarts_on_every_change);
  RUN_TEST(test_abort_fails_work_in_progress_but_keeps_results);
  RUN_TEST(test_pulse_rhythms_have_the_right_count);
  RUN_TEST(test_every_failure_has_its_own_rhythm);
  RUN_TEST(test_no_credentials_shows_two_amber_bars_with_a_dark_gap);
  RUN_TEST(test_download_fill_follows_real_progress);
  RUN_TEST(test_confirm_overlay_whitens_from_the_start);
  RUN_TEST(test_success_fills_from_both_ends_then_beats_in_the_centre);
  RUN_TEST(test_failures_are_red);
  RUN_TEST(test_every_stage_looks_different);
  return UNITY_END();
}
