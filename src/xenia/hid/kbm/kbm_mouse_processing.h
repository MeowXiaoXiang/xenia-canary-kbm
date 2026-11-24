/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2026 Xenia authors. All rights reserved.                         *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#ifndef XENIA_HID_KBM_KBM_MOUSE_PROCESSING_H_
#define XENIA_HID_KBM_KBM_MOUSE_PROCESSING_H_

#include <algorithm>
#include <cmath>
#include <optional>
#include <string_view>

namespace xe::hid::kbm {

enum class KbmMouseMode { kEma, kFixedWindow, kDirect };

inline constexpr KbmMouseMode kDefaultMouseMode = KbmMouseMode::kEma;
inline constexpr double kDefaultFixedWindowMs = 16.0;
inline constexpr double kDefaultEmaTauMs = 8.0;
inline constexpr double kMinFixedWindowMs = 4.0;
inline constexpr double kMaxFixedWindowMs = 24.0;
inline constexpr double kMinEmaTauMs = 2.0;
inline constexpr double kMaxEmaTauMs = 32.0;

struct KbmMouseProcessing {
  KbmMouseMode mode = kDefaultMouseMode;
  double fixed_window_ms = kDefaultFixedWindowMs;
  double ema_tau_ms = kDefaultEmaTauMs;

  bool operator==(const KbmMouseProcessing&) const = default;
};

inline constexpr std::string_view MouseModeName(KbmMouseMode mode) {
  switch (mode) {
    case KbmMouseMode::kFixedWindow:
      return "fixed_window";
    case KbmMouseMode::kEma:
      return "ema";
    default:
      return "direct";
  }
}

inline std::optional<KbmMouseMode> ParseMouseMode(std::string_view name) {
  if (name == "direct") {
    return KbmMouseMode::kDirect;
  }
  if (name == "fixed_window") {
    return KbmMouseMode::kFixedWindow;
  }
  if (name == "ema") {
    return KbmMouseMode::kEma;
  }
  return std::nullopt;
}

inline KbmMouseProcessing NormalizeMouseProcessing(KbmMouseProcessing value) {
  auto normalize_time = [](double ms, double fallback, double low,
                           double high) {
    return std::round(std::clamp(std::isfinite(ms) ? ms : fallback, low, high) *
                      2.0) /
           2.0;
  };
  if (value.mode != KbmMouseMode::kDirect &&
      value.mode != KbmMouseMode::kFixedWindow &&
      value.mode != KbmMouseMode::kEma) {
    value.mode = kDefaultMouseMode;
  }
  value.fixed_window_ms =
      normalize_time(value.fixed_window_ms, kDefaultFixedWindowMs,
                     kMinFixedWindowMs, kMaxFixedWindowMs);
  value.ema_tau_ms = normalize_time(value.ema_tau_ms, kDefaultEmaTauMs,
                                    kMinEmaTauMs, kMaxEmaTauMs);
  return value;
}

}  // namespace xe::hid::kbm

#endif  // XENIA_HID_KBM_KBM_MOUSE_PROCESSING_H_
