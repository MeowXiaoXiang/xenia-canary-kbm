/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2026 Xenia authors. All rights reserved.                         *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#ifndef XENIA_HID_KBM_KBM_INPUT_REPORT_H_
#define XENIA_HID_KBM_KBM_INPUT_REPORT_H_

#include <chrono>
#include <cstdint>
#include <string>
#include <vector>

namespace xe::hid::kbm {

struct KbmInputSample {
  uint64_t event_sequence = 0;
  std::chrono::steady_clock::time_point time;
  double elapsed_seconds = 0.0;
  int64_t raw_delta_x = 0;
  int64_t raw_delta_y = 0;
  double filtered_velocity_x = 0.0;
  double filtered_velocity_y = 0.0;
  int16_t thumb_x = 0;
  int16_t thumb_y = 0;
  bool input_active = false;
  bool capture_active = false;
  bool input_suspended = false;
  bool reset_sample = false;
  bool stale_sample = false;
};

struct KbmRawInputSample {
  uint64_t sequence = 0;
  bool reset_pending = false;
  std::chrono::steady_clock::time_point time;
  int64_t delta_x = 0;
  int64_t delta_y = 0;
  uint32_t kind = 0;
  double velocity_x = 0.0;
  double velocity_y = 0.0;
  double previous_poll_offset_seconds = 0.0;
  bool capture_active = false;
  bool suspended = false;
};

struct KbmInputReportSettings {
  double sensitivity = 10.0;
  double full_scale_velocity = 24000.0;
  double response_curve = 0.8;
  double smoothing_time_ms = 8.0;
  bool deadzone_compensation = false;
  double minimum_response = 0.30;
  bool invert_y = false;
};

struct KbmInputReportContents {
  std::string poll_csv;
  std::string events_csv;
};

KbmInputReportContents SerializeKbmInputReport(
    const std::vector<KbmInputSample>& samples,
    const std::vector<KbmRawInputSample>& events, size_t dropped_samples,
    size_t dropped_events, const KbmInputReportSettings& settings,
    std::chrono::steady_clock::time_point started);

}  // namespace xe::hid::kbm

#endif  // XENIA_HID_KBM_KBM_INPUT_REPORT_H_
