/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2026 Xenia authors. All rights reserved.                         *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#include "xenia/hid/kbm/kbm_mouse_processor.h"

#include <algorithm>
#include <cmath>
#include <utility>

namespace xe::hid::kbm {

void KbmMouseProcessor::AddDelta(int64_t delta_x, int64_t delta_y,
                                 std::chrono::steady_clock::time_point time) {
  pending_delta_x_ += delta_x;
  pending_delta_y_ += delta_y;
  ++event_sequence_;
  if (delta_x || delta_y) {
    has_motion_ = true;
    last_motion_time_ = time;
  }
}

void KbmMouseProcessor::Reset(std::chrono::steady_clock::time_point time) {
  pending_delta_x_ = 0;
  pending_delta_y_ = 0;
  filtered_velocity_ = {};
  has_motion_ = false;
  reset_pending_ = true;
  last_sample_time_ = time;
  ++event_sequence_;
}

KbmMouseSample KbmMouseProcessor::Consume(
    const KbmMouseSettings& settings, bool input_active,
    std::chrono::steady_clock::time_point time) {
  KbmMouseSample sample;
  sample.event_sequence = event_sequence_;
  sample.time = time;
  sample.input_active = input_active;
  sample.elapsed_seconds =
      std::chrono::duration<double>(time - last_sample_time_).count();
  const bool advance = sample.elapsed_seconds > 0.0;
  if (advance) {
    last_sample_time_ = time;
    sample.raw_delta_x = std::exchange(pending_delta_x_, 0);
    sample.raw_delta_y = std::exchange(pending_delta_y_, 0);
    sample.reset_sample = std::exchange(reset_pending_, false);
  } else {
    sample.reset_sample = reset_pending_;
  }
  sample.stale_sample = sample.elapsed_seconds > kMaximumSampleIntervalSeconds;

  int64_t delta_x = sample.raw_delta_x;
  int64_t delta_y = sample.raw_delta_y;
  if (!input_active || sample.reset_sample || sample.stale_sample) {
    delta_x = 0;
    delta_y = 0;
  }
  if (input_active && !sample.reset_sample && !sample.stale_sample) {
    filtered_velocity_.x = FilterMouseDisplacement(
        double(delta_x), sample.elapsed_seconds, settings.smoothing_time_ms,
        filtered_velocity_.x);
    filtered_velocity_.y = FilterMouseDisplacement(
        double(settings.invert_y ? delta_y : -delta_y), sample.elapsed_seconds,
        settings.smoothing_time_ms, filtered_velocity_.y);
  } else {
    filtered_velocity_ = {};
  }
  sample.filtered_velocity_x = filtered_velocity_.x;
  sample.filtered_velocity_y = filtered_velocity_.y;

  const double full_scale =
      settings.full_scale_velocity / std::max(settings.sensitivity, 0.01);
  const auto mapped = MapMouseRadial(
      filtered_velocity_, full_scale, settings.response_curve,
      settings.deadzone_compensation ? settings.minimum_response : 0.0);
  sample.thumb_x = static_cast<int16_t>(std::lround(mapped.x * 32767.0));
  sample.thumb_y = static_cast<int16_t>(std::lround(mapped.y * 32767.0));
  if (settings.deadzone_compensation &&
      (!has_motion_ ||
       !MouseCompensationActive(
           std::chrono::duration<double>(time - last_motion_time_).count(),
           std::hypot(filtered_velocity_.x, filtered_velocity_.y),
           full_scale))) {
    sample.thumb_x = 0;
    sample.thumb_y = 0;
  }
  return sample;
}

}  // namespace xe::hid::kbm
