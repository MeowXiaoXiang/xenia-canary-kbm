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

bool KbmMouseProcessor::Configure(KbmMouseProcessing processing,
                                  std::chrono::steady_clock::time_point time) {
  processing = NormalizeMouseProcessing(processing);
  if (processing_ == processing) {
    return false;
  }
  processing_ = processing;
  window_width_ =
      KbmMouseWindow::Duration(int64_t(processing.fixed_window_ms * 1000.0));
  Reset(time);
  return true;
}

void KbmMouseProcessor::AddDelta(int64_t delta_x, int64_t delta_y,
                                 std::chrono::steady_clock::time_point time) {
  pending_delta_x_ += delta_x;
  pending_delta_y_ += delta_y;
  ++event_sequence_;
  if (processing_.mode == KbmMouseMode::kFixedWindow) {
    window_.Add(delta_x, delta_y, time, window_width_);
  } else if (processing_.mode == KbmMouseMode::kEma && (delta_x || delta_y)) {
    last_motion_time_ = time;
  }
}

void KbmMouseProcessor::ClearEstimator() {
  window_.Reset();
  ema_velocity_ = {};
  last_motion_time_.reset();
}

void KbmMouseProcessor::Reset(std::chrono::steady_clock::time_point time) {
  pending_delta_x_ = 0;
  pending_delta_y_ = 0;
  velocity_ = {};
  ClearEstimator();
  reset_pending_ = true;
  last_sample_time_ = time;
  ++event_sequence_;
}

MouseVector KbmMouseProcessor::Estimate(const KbmMouseSettings& settings,
                                        KbmMouseSample& sample) {
  MouseVector velocity = {double(sample.raw_delta_x) / sample.elapsed_seconds,
                          double(sample.raw_delta_y) / sample.elapsed_seconds};
  if (processing_.mode == KbmMouseMode::kFixedWindow) {
    velocity = window_.Read(sample.time, window_width_);
  } else if (processing_.mode == KbmMouseMode::kEma) {
    const double ratio =
        sample.elapsed_seconds / (processing_.ema_tau_ms * 0.001);
    const double alpha = -std::expm1(-ratio);
    sample.ema_alpha = alpha;
    const double decay = std::exp(-ratio);
    ema_velocity_ = {alpha * velocity.x + decay * ema_velocity_.x,
                     alpha * velocity.y + decay * ema_velocity_.y};
    const double full_scale =
        FullStickCountsPerSecond(settings.dpi, settings.full_stick_speed_cm_s);
    if (!last_motion_time_ ||
        sample.time - *last_motion_time_ >= std::chrono::milliseconds(50) ||
        (sample.time - *last_motion_time_ >= std::chrono::milliseconds(12) &&
         std::hypot(ema_velocity_.x, ema_velocity_.y) <= 0.02 * full_scale)) {
      ema_velocity_ = {};
    }
    velocity = ema_velocity_;
  }
  if (!settings.invert_y) {
    velocity.y = -velocity.y;
  }
  return velocity;
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

  velocity_ = {};
  if (advance) {
    if (input_active && !sample.reset_sample && !sample.stale_sample) {
      velocity_ = Estimate(settings, sample);
    } else {
      ClearEstimator();
    }
  }
  sample.velocity_x = velocity_.x;
  sample.velocity_y = velocity_.y;
  const auto mapped = MapMousePhysical(
      velocity_, settings.dpi, settings.full_stick_speed_cm_s,
      settings.deadzone_compensation ? settings.minimum_response : 0.0);
  sample.thumb_x = static_cast<int16_t>(std::lround(mapped.x * 32767.0));
  sample.thumb_y = static_cast<int16_t>(std::lround(mapped.y * 32767.0));
  return sample;
}

}  // namespace xe::hid::kbm
