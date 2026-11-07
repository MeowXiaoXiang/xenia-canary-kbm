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

// Physical speed mapper shared by gameplay and the independent UI preview.
inline double NormalizeMouseNumber(double value, double fallback, double low,
                                   double high) {
  return std::clamp(std::isfinite(value) ? value : fallback, low, high);
}

inline MouseVector MapMousePhysical(MouseVector velocity, double dpi,
                                    double full_stick_speed_cm_s,
                                    double minimum = 0.0) {
  if (!std::isfinite(velocity.x) || !std::isfinite(velocity.y)) {
    return {};
  }
  dpi = NormalizeMouseNumber(dpi, 800.0, 100.0, 32000.0);
  full_stick_speed_cm_s =
      NormalizeMouseNumber(full_stick_speed_cm_s, 10.0, 1.0, 100.0);
  minimum = NormalizeMouseNumber(minimum, 0.30, 0.0, 0.5);
  const double speed = std::hypot(velocity.x, velocity.y);
  if (speed == 0.0 || !std::isfinite(speed)) {
    return {};
  }
  const double radius =
      minimum + (1.0 - minimum) *
                    std::min(speed / (dpi * full_stick_speed_cm_s / 2.54), 1.0);
  return {velocity.x / speed * radius, velocity.y / speed * radius};
}

}  // namespace xe::hid::kbm

#endif  // XENIA_HID_KBM_KBM_MOUSE_MATH_H_
