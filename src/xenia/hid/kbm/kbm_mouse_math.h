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
#include <optional>
#include <string_view>

namespace xe::hid::kbm {

struct MouseVector {
  double x = 0.0;
  double y = 0.0;
};

enum class KbmAntiDeadzoneShape { kRadial, kAxial };

inline constexpr std::string_view AntiDeadzoneShapeName(
    KbmAntiDeadzoneShape shape) {
  return shape == KbmAntiDeadzoneShape::kAxial ? "axial" : "radial";
}

inline std::optional<KbmAntiDeadzoneShape> ParseAntiDeadzoneShape(
    std::string_view name) {
  if (name == "radial") {
    return KbmAntiDeadzoneShape::kRadial;
  }
  if (name == "axial") {
    return KbmAntiDeadzoneShape::kAxial;
  }
  return std::nullopt;
}

inline constexpr double kCentimetersPerInch = 2.54;

inline double NormalizeMouseNumber(double value, double fallback, double low,
                                   double high) {
  return std::clamp(std::isfinite(value) ? value : fallback, low, high);
}

inline double NormalizeMouseDpi(double dpi) {
  return NormalizeMouseNumber(dpi, 800.0, 100.0, 32000.0);
}

inline double FullStickCountsPerSecond(double dpi, double speed_cm_s) {
  return NormalizeMouseDpi(dpi) *
         NormalizeMouseNumber(speed_cm_s, 10.0, 1.0, 100.0) /
         kCentimetersPerInch;
}

// Physical speed mapper shared by gameplay and the independent UI preview.
inline MouseVector MapMousePhysical(
    MouseVector velocity, double dpi, double full_stick_speed_cm_s,
    double minimum = 0.0,
    KbmAntiDeadzoneShape shape = KbmAntiDeadzoneShape::kRadial,
    MouseVector axial_minimum = {0.30, 0.30}) {
  if (!std::isfinite(velocity.x) || !std::isfinite(velocity.y)) {
    return {};
  }
  minimum = NormalizeMouseNumber(minimum, 0.30, 0.0, 0.5);
  const double speed = std::hypot(velocity.x, velocity.y);
  if (speed == 0.0 || !std::isfinite(speed)) {
    return {};
  }
  const double full_scale =
      FullStickCountsPerSecond(dpi, full_stick_speed_cm_s);
  if (shape == KbmAntiDeadzoneShape::kAxial) {
    const double scale = std::min(speed / full_scale, 1.0);
    const auto compensate = [](double value, double floor) {
      if (value == 0.0) {
        return 0.0;
      }
      floor = NormalizeMouseNumber(floor, 0.30, 0.0, 0.5);
      return std::copysign(
          std::min(floor + (1.0 - floor) * std::abs(value), 1.0), value);
    };
    return {compensate(velocity.x / speed * scale, axial_minimum.x),
            compensate(velocity.y / speed * scale, axial_minimum.y)};
  }
  const double radius =
      minimum + (1.0 - minimum) * std::min(speed / full_scale, 1.0);
  return {velocity.x / speed * radius, velocity.y / speed * radius};
}

}  // namespace xe::hid::kbm

#endif  // XENIA_HID_KBM_KBM_MOUSE_MATH_H_
