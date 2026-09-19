/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2026 Xenia authors. All rights reserved.                         *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#ifndef XENIA_HID_KBM_KBM_MOUSE_MATH_H_
#define XENIA_HID_KBM_KBM_MOUSE_MATH_H_

#include <algorithm>
#include <cmath>

namespace xe::hid::kbm {

struct MouseVector {
  double x = 0.0;
  double y = 0.0;
};

// Gate only anti-deadzone: a recent nonzero motion may restart immediately,
// but an exponentially small tail must not sustain minimum stick indefinitely.
inline bool MouseCompensationActive(double quiet_seconds, double speed,
                                    double full_speed) {
  return quiet_seconds >= 0.0 && quiet_seconds < 0.050 &&
         (quiet_seconds < 0.012 || speed > 0.02 * full_speed);
}

inline MouseVector MapMouseRadial(MouseVector velocity, double full_speed,
                                  double exponent, double minimum = 0.0) {
  const double speed = std::hypot(velocity.x, velocity.y);
  if (speed == 0.0 || full_speed <= 0.0) {
    return {};
  }
  const double radius =
      minimum +
      (1.0 - minimum) * std::pow(std::min(speed / full_speed, 1.0), exponent);
  return {velocity.x / speed * radius, velocity.y / speed * radius};
}

// Candidate event-domain estimator, deliberately separate from the production
// poll filter. Timestamps are host processing seconds, not hardware timestamps.
class MouseEventExponential {
 public:
  explicit MouseEventExponential(double tau_seconds) : tau_(tau_seconds) {}

  void Reset(double now) {
    state_ = {};
    time_ = now;
  }

  void Add(double now, MouseVector delta) {
    state_ = Snapshot(now);
    time_ = std::max(now, time_);
    if (tau_ > 0.0) {
      state_.x += delta.x / tau_;
      state_.y += delta.y / tau_;
    }
  }

  MouseVector Snapshot(double now) const {
    if (tau_ <= 0.0) {
      return {};
    }
    const double decay = std::exp(-std::max(0.0, now - time_) / tau_);
    return {state_.x * decay, state_.y * decay};
  }

 private:
  double tau_;
  double time_ = 0.0;
  MouseVector state_;
};

// Integrate a displacement bucket over its actual duration. In particular,
// querying a controller more often must not advance the filter's clock faster.
inline double FilterMouseDisplacement(double delta, double seconds,
                                      double smoothing_ms, double previous) {
  if (seconds <= 0.0) {
    return previous;
  }
  if (smoothing_ms <= 0.0) {
    return delta / seconds;
  }
  const double tau = smoothing_ms * 0.001;
  const double ratio = seconds / tau;
  const double alpha = -std::expm1(-ratio);
  // alpha / ratio tends to 1 as the interval tends to zero. This form avoids
  // both cancellation in 1-exp(-ratio) and a very large intermediate velocity.
  const double gain = ratio > 0.0 ? alpha / ratio : 1.0;
  return previous * std::exp(-ratio) + (delta / tau) * gain;
}

}  // namespace xe::hid::kbm

#endif  // XENIA_HID_KBM_KBM_MOUSE_MATH_H_
