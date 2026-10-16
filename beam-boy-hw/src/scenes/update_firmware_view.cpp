#include "scenes/update_firmware_view.h"

#include <math.h>

namespace beamboy {
namespace update {
namespace {

const Color kConnectColor(0, 160, 255);
const Color kAvailableColor = colors::kTurquoise;
const Color kFailColor(255, 30, 20);
const Color kOkColor(0, 255, 60);

float seconds(uint32_t ms) { return static_cast<float>(ms) / 1000.0f; }

float fraction(uint32_t elapsed_ms, uint32_t total_ms) {
  return elapsed_ms >= total_ms
             ? 1.0f
             : static_cast<float>(elapsed_ms) / static_cast<float>(total_ms);
}

void drawNoCredentials(Display& display, uint32_t ms) {
  const float level = pulseOn(ms, 2, 1600) ? 0.7f : 0.15f;
  display.span(0.1f, 0.3f, colors::kAmber, level);
  display.span(0.7f, 0.9f, colors::kAmber, level);
}

void drawConnecting(Display& display, uint32_t ms) {
  const float head = fmodf(seconds(ms) * 1.6f, 1.0f);
  display.point(head, kConnectColor);
  display.point(head - display.pixelWidth(), kConnectColor, 0.5f);
  display.point(head - display.pixelWidth() * 2.0f, kConnectColor, 0.2f);
}

void drawUpToDate(Display& display, uint32_t ms) {
  display.span(0.0f, 1.0f, colors::kBlue, 0.3f);
  if (ms < kAcknowledgeMs) {
    display.point(fraction(ms, kAcknowledgeMs), colors::kWhite, 0.9f);
  }
}

void drawAvailable(Display& display, float confirm) {
  display.span(0.0f, 1.0f, kAvailableColor, 0.35f);
  if (confirm > 0.0f) {
    display.overlaySpan(0.0f, confirm, colors::kWhite, 0.9f);
  }
}

void drawDownloading(Display& display, float progress) {
  if (progress < 0.0f) progress = 0.0f;
  if (progress > 1.0f) progress = 1.0f;
  display.span(0.0f, 1.0f, colors::kBlue, 0.06f);
  if (progress > 0.0f) display.span(0.0f, progress, colors::kBlue, 0.8f);
}

void drawVerifying(Display& display, uint32_t ms) {
  display.span(0.0f, 1.0f, colors::kBlue, 0.3f);
  const float t = fmodf(seconds(ms) * 1.25f, 1.0f) * 0.5f;
  display.point(t, colors::kWhite, 0.9f);
  display.point(1.0f - t, colors::kWhite, 0.9f);
}

void drawInstalled(Display& display, uint32_t ms) {
  const float t = fraction(ms, kSuccessFillMs);
  display.span(0.0f, t * 0.5f, kOkColor, 0.6f);
  display.span(1.0f - t * 0.5f, 1.0f, kOkColor, 0.6f);
  if (ms >= kSuccessFillMs) {
    // Heartbeat: two quick white beats over the centre, then a pause.
    const bool beat = pulseOn(ms - kSuccessFillMs, 2, 1200);
    const float w = display.pixelWidth() * 1.5f;
    display.overlaySpan(0.5f - w, 0.5f + w, colors::kWhite,
                        beat ? 1.0f : 0.25f);
  }
}

void drawFailed(Display& display, Failure failure, uint32_t ms) {
  const uint8_t pulses = failurePulses(failure);
  float level;
  if (pulses == 0) {
    // Power: a slow breath, unlike every counted error.
    level = 0.35f + 0.25f * pulse(seconds(ms), 2.0f);
  } else {
    level = pulseOn(ms, pulses, kPulseCycleMs) ? 0.8f : 0.08f;
  }
  display.span(0.0f, 1.0f, kFailColor, level);
}

}  // namespace

bool pulseOn(uint32_t elapsed_ms, uint8_t count, uint32_t cycle_ms) {
  if (count == 0 || cycle_ms == 0) return false;
  const uint32_t t = elapsed_ms % cycle_ms;
  const uint32_t period = kPulseOnMs + kPulseGapMs;
  if (t >= period * count) return false;
  return t % period < kPulseOnMs;
}

uint8_t failurePulses(Failure failure) {
  switch (failure) {
    case Failure::kConnect:
      return 1;
    case Failure::kCheck:
      return 2;
    case Failure::kIncompatible:
      return 3;
    case Failure::kInstall:
      return 4;
    case Failure::kPower:
    case Failure::kNone:
      return 0;
  }
  return 0;
}

void render(Display& display, const View& view) {
  display.clear();
  switch (view.stage) {
    case Stage::kNoCredentials:
      drawNoCredentials(display, view.stage_ms);
      break;
    case Stage::kConnecting:
      drawConnecting(display, view.stage_ms);
      break;
    case Stage::kChecking:
      display.span(0.0f, 1.0f, colors::kBlue, 0.15f);
      break;
    case Stage::kUpToDate:
      drawUpToDate(display, view.stage_ms);
      break;
    case Stage::kAvailable:
      drawAvailable(display, view.confirm_progress);
      break;
    case Stage::kDownloading:
      drawDownloading(display, view.download_progress);
      break;
    case Stage::kVerifying:
      drawVerifying(display, view.stage_ms);
      break;
    case Stage::kSuccess:
      drawInstalled(display, view.stage_ms);
      break;
    case Stage::kStaged:
      // Already installed: skip straight to the settled frame.
      drawInstalled(display, kSuccessFillMs + view.stage_ms);
      break;
    case Stage::kFailed:
      drawFailed(display, view.failure, view.stage_ms);
      break;
  }
}

}  // namespace update
}  // namespace beamboy
