/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2026 Xenia authors. All rights reserved.                         *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#include "xenia/hid/kbm/kbm_input_report.h"

#include <algorithm>
#include <cmath>

#include "third_party/fmt/include/fmt/format.h"

namespace xe::hid::kbm {

KbmInputReportContents SerializeKbmInputReport(
    const std::vector<KbmInputSample>& samples,
    const std::vector<KbmRawInputSample>& events, size_t dropped_samples,
    size_t dropped_events, const KbmInputReportSettings& settings,
    std::chrono::steady_clock::time_point started) {
  size_t active_samples = 0;
  size_t moved_samples = 0;
  size_t zero_output_samples = 0;
  size_t reset_samples = 0;
  size_t stale_samples = 0;
  double total_interval_seconds = 0.0;
  double longest_interval_seconds = 0.0;
  for (const auto& sample : samples) {
    active_samples += sample.input_active;
    const bool moved = sample.raw_delta_x || sample.raw_delta_y;
    moved_samples += moved;
    zero_output_samples +=
        moved && !sample.thumb_x && !sample.thumb_y && sample.input_active;
    reset_samples += sample.reset_sample;
    stale_samples += sample.stale_sample;
    total_interval_seconds += sample.elapsed_seconds;
    longest_interval_seconds =
        std::max(longest_interval_seconds, sample.elapsed_seconds);
  }

  KbmInputReportContents contents;
  std::string& output = contents.poll_csv;
  output.reserve(1024 + samples.size() * 110);
  output += "# KBM Controller Raw Input sampling report\n";
  output += fmt::format(
      "# samples={}, dropped_samples={}, active_samples={}, "
      "moved_samples={}, moved_with_zero_output={}, "
      "reset_samples={}, stale_samples={}\n",
      samples.size(), dropped_samples, active_samples, moved_samples,
      zero_output_samples, reset_samples, stale_samples);
  output += fmt::format(
      "# average_interval_ms={:.3f}, longest_interval_ms={:.3f}\n",
      samples.empty() ? 0.0 : total_interval_seconds * 1000.0 / samples.size(),
      longest_interval_seconds * 1000.0);
  output += fmt::format(
      "# sensitivity={}, full_scale_velocity={}, response_curve={}, "
      "smoothing_time_ms={}, deadzone_compensation={}, "
      "minimum_response={}, invert_y={}, mapper=radial\n",
      settings.sensitivity, settings.full_scale_velocity,
      settings.response_curve, settings.smoothing_time_ms,
      settings.deadzone_compensation, settings.minimum_response,
      settings.invert_y);
  output += fmt::format(
      "# schema=2, estimator=poll_ema_actual_time, "
      "timestamp=host_processing_steady_clock, events={}, dropped_events={}\n",
      events.size(), dropped_events);
  output +=
      "elapsed_ms,interval_ms,input_active,capture_active,input_suspended,"
      "raw_delta_x,raw_delta_y,filtered_velocity_x,filtered_velocity_y,"
      "thumb_x,thumb_y,reset_sample,stale_sample,event_sequence,output_"
      "radius\n";
  for (const auto& sample : samples) {
    output += fmt::format(
        "{:.6f},{:.6f},{},{},{},{},{},{:.9f},{:.9f},{},{},{},{},{},{:.9f}\n",
        std::chrono::duration<double, std::milli>(sample.time - started)
            .count(),
        sample.elapsed_seconds * 1000.0, sample.input_active,
        sample.capture_active, sample.input_suspended, sample.raw_delta_x,
        sample.raw_delta_y, sample.filtered_velocity_x,
        sample.filtered_velocity_y, sample.thumb_x, sample.thumb_y,
        sample.reset_sample, sample.stale_sample, sample.event_sequence,
        std::hypot(double(sample.thumb_x), double(sample.thumb_y)) / 32767.0);
  }

  std::string& event_output = contents.events_csv;
  event_output =
      "# schema=2, timestamp=host_processing_steady_clock; "
      "kind: 0=motion,1=reset,2=initial_state\n"
      "event_elapsed_us,sequence,kind,delta_x,delta_y,velocity_x,velocity_y,"
      "previous_poll_offset_ms,capture_active,input_suspended,reset_pending\n";
  event_output.reserve(256 + events.size() * 100);
  for (const auto& event : events) {
    event_output += fmt::format(
        "{:.3f},{},{},{},{},{:.9f},{:.9f},{:.6f},{},{},{}\n",
        std::chrono::duration<double, std::micro>(event.time - started).count(),
        event.sequence, event.kind, event.delta_x, event.delta_y,
        event.velocity_x, event.velocity_y,
        event.previous_poll_offset_seconds * 1000.0, event.capture_active,
        event.suspended, event.reset_pending);
  }
  return contents;
}

}  // namespace xe::hid::kbm
