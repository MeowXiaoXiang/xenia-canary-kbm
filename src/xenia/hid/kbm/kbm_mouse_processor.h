/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2026 Xenia authors. All rights reserved.                         *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#ifndef XENIA_HID_KBM_KBM_MOUSE_PROCESSOR_H_
#define XENIA_HID_KBM_KBM_MOUSE_PROCESSOR_H_

#include <chrono>
#include <cstdint>

#include "xenia/hid/kbm/kbm_mouse_math.h"

namespace xe::hid::kbm {

struct KbmMouseSettings {
  double sensitivity = 10.0;
  double full_scale_velocity = 24000.0;
  double response_curve = 0.8;
  double smoothing_time_ms = 8.0;
  bool deadzone_compensation = false;
  double minimum_response = 0.30;
  bool invert_y = false;
};

struct KbmMouseSample {
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
  bool reset_sample = false;
  bool stale_sample = false;
};

// Stateful Raw Mouse filter and mapper. Its caller serializes access so an
// input event and a controller poll observe one coherent timeline.
class KbmMouseProcessor {
 public:
  void AddDelta(int64_t delta_x, int64_t delta_y,
                std::chrono::steady_clock::time_point time);
  void Reset(std::chrono::steady_clock::time_point time);
  KbmMouseSample Consume(const KbmMouseSettings& settings, bool input_active,
                         std::chrono::steady_clock::time_point time);

  uint64_t event_sequence() const { return event_sequence_; }
  int64_t pending_delta_x() const { return pending_delta_x_; }
  int64_t pending_delta_y() const { return pending_delta_y_; }
  bool reset_pending() const { return reset_pending_; }
  MouseVector filtered_velocity() const { return filtered_velocity_; }
  std::chrono::steady_clock::time_point last_sample_time() const {
    return last_sample_time_;
  }

 private:
  static constexpr double kMaximumSampleIntervalSeconds = 0.1;

  uint64_t event_sequence_ = 0;
  int64_t pending_delta_x_ = 0;
  int64_t pending_delta_y_ = 0;
  bool has_motion_ = false;
  bool reset_pending_ = true;
  std::chrono::steady_clock::time_point last_motion_time_;
  std::chrono::steady_clock::time_point last_sample_time_ =
      std::chrono::steady_clock::now();
  MouseVector filtered_velocity_;
};

}  // namespace xe::hid::kbm

#endif  // XENIA_HID_KBM_KBM_MOUSE_PROCESSOR_H_
