/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2026 Xenia authors. All rights reserved.                         *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#ifndef XENIA_HID_KBM_KBM_MOUSE_PREVIEW_H_
#define XENIA_HID_KBM_KBM_MOUSE_PREVIEW_H_

#include "xenia/hid/kbm/kbm_mouse_processor.h"

namespace xe::hid::kbm {

// UI-thread owned. This processor never shares buckets or clocks with gameplay.
class KbmMousePreview {
 public:
  using Time = std::chrono::steady_clock::time_point;
  void Start(Time now, KbmMouseProcessing processing = {}) {
    active_ = true;
    Reset(now, processing);
  }
  void Stop() {
    active_ = false;
    current_ = 0.0;
  }
  void Reset(Time now, KbmMouseProcessing processing = {}) {
    if (!processor_.Configure(processing, now)) {
      processor_.Reset(now);
    }
    // Prime only this preview's reset marker, before any preview motion
    // arrives.
    processor_.Consume({}, false, now + std::chrono::steady_clock::duration(1));
    sampled_ = now;
    current_ = peak_ = 0.0;
  }
  void AddDelta(int64_t x, int64_t y, Time now) {
    if (active_) {
      processor_.AddDelta(x, y, now);
    }
  }
  void Sample(const KbmMouseSettings& settings, Time now) {
    if (!active_ || now - sampled_ < std::chrono::milliseconds(16)) {
      return;
    }
    sampled_ = now;
    const auto sample = processor_.Consume(settings, true, now);
    current_ =
        std::hypot(double(sample.thumb_x), double(sample.thumb_y)) / 32767.0;
    if (!settings.deadzone_compensation ||
        settings.anti_deadzone_shape != KbmAntiDeadzoneShape::kAxial) {
      current_ = std::min(1.0, current_);
    }
    peak_ = std::max(peak_, current_);
  }
  bool active() const { return active_; }
  double speed_cm_s(double dpi) const {
    if (!active_) {
      return 0.0;
    }
    const auto velocity = processor_.velocity();
    return std::hypot(velocity.x, velocity.y) * kCentimetersPerInch /
           NormalizeMouseDpi(dpi);
  }
  double current() const { return current_; }
  double peak() const { return peak_; }

 private:
  KbmMouseProcessor processor_;
  Time sampled_;
  bool active_ = false;
  double current_ = 0.0;
  double peak_ = 0.0;
};

}  // namespace xe::hid::kbm
#endif  // XENIA_HID_KBM_KBM_MOUSE_PREVIEW_H_
