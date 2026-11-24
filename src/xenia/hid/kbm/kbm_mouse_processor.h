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
#include <optional>

#include "xenia/hid/kbm/kbm_mouse_math.h"
#include "xenia/hid/kbm/kbm_mouse_processing.h"
#include "xenia/hid/kbm/kbm_mouse_window.h"

namespace xe::hid::kbm {

struct KbmMouseSettings {
  double dpi = 800.0;
  double full_stick_speed_cm_s = 10.0;
  bool deadzone_compensation = true;
  double minimum_response = 0.30;
  bool invert_y = false;
  KbmMouseProcessing processing;
  KbmAntiDeadzoneShape anti_deadzone_shape = KbmAntiDeadzoneShape::kRadial;
  double minimum_response_x = 0.30;
  double minimum_response_y = 0.30;
};

struct KbmMouseSample {
  uint64_t event_sequence = 0;
  std::chrono::steady_clock::time_point time;
  double elapsed_seconds = 0.0;
  int64_t raw_delta_x = 0;
  int64_t raw_delta_y = 0;
  double velocity_x = 0.0;
  double velocity_y = 0.0;
  std::optional<double> ema_alpha;
  int16_t thumb_x = 0;
  int16_t thumb_y = 0;
  bool input_active = false;
  bool reset_sample = false;
  bool stale_sample = false;
};

// Stateful Raw Mouse bucket and mapper. Its caller serializes access so an
// input event and a controller poll observe one coherent timeline.
class KbmMouseProcessor {
 public:
  // A changed processing configuration starts a new input history.
  bool Configure(KbmMouseProcessing processing,
                 std::chrono::steady_clock::time_point time);
  void AddDelta(int64_t delta_x, int64_t delta_y,
                std::chrono::steady_clock::time_point time);
  void Reset(std::chrono::steady_clock::time_point time);
  KbmMouseSample Consume(const KbmMouseSettings& settings, bool input_active,
                         std::chrono::steady_clock::time_point time);

  uint64_t event_sequence() const { return event_sequence_; }
  int64_t pending_delta_x() const { return pending_delta_x_; }
  int64_t pending_delta_y() const { return pending_delta_y_; }
  MouseVector velocity() const { return velocity_; }
  std::chrono::steady_clock::time_point last_sample_time() const {
    return last_sample_time_;
  }

 private:
  static constexpr double kMaximumSampleIntervalSeconds = 0.1;
  void ClearEstimator();
  MouseVector Estimate(const KbmMouseSettings& settings,
                       KbmMouseSample& sample);

  uint64_t event_sequence_ = 0;
  int64_t pending_delta_x_ = 0;
  int64_t pending_delta_y_ = 0;
  bool reset_pending_ = true;
  std::chrono::steady_clock::time_point last_sample_time_ =
      std::chrono::steady_clock::now();
  MouseVector velocity_;
  KbmMouseProcessing processing_;
  KbmMouseWindow window_;
  KbmMouseWindow::Duration window_width_{
      int64_t(kDefaultFixedWindowMs * 1000.0)};
  MouseVector ema_velocity_;
  std::optional<std::chrono::steady_clock::time_point> last_motion_time_;
};

}  // namespace xe::hid::kbm

#endif  // XENIA_HID_KBM_KBM_MOUSE_PROCESSOR_H_
