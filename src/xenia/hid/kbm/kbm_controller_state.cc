/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2026 Xenia authors. All rights reserved.                         *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#include "xenia/hid/kbm/kbm_controller_state.h"

#include <algorithm>
#include <limits>

namespace xe::hid::kbm {
namespace {

constexpr uint16_t kDpadUp = 0x0001;
constexpr uint16_t kDpadDown = 0x0002;
constexpr uint16_t kDpadLeft = 0x0004;
constexpr uint16_t kDpadRight = 0x0008;
constexpr uint16_t kStart = 0x0010;
constexpr uint16_t kBack = 0x0020;
constexpr uint16_t kLeftThumb = 0x0040;
constexpr uint16_t kRightThumb = 0x0080;
constexpr uint16_t kLeftShoulder = 0x0100;
constexpr uint16_t kRightShoulder = 0x0200;
constexpr uint16_t kGuide = 0x0400;
constexpr uint16_t kA = 0x1000;
constexpr uint16_t kB = 0x2000;
constexpr uint16_t kX = 0x4000;
constexpr uint16_t kY = 0x8000;

}  // namespace

void AddKbmThumb(int16_t& destination, int16_t value) {
  destination = static_cast<int16_t>(
      std::clamp(int32_t(destination) + int32_t(value),
                 int32_t(std::numeric_limits<int16_t>::min()),
                 int32_t(std::numeric_limits<int16_t>::max())));
}

void ApplyKbmControl(KbmControllerState& state, KbmControl control) {
  switch (control) {
    case KbmControl::kA:
      state.buttons |= kA;
      break;
    case KbmControl::kB:
      state.buttons |= kB;
      break;
    case KbmControl::kX:
      state.buttons |= kX;
      break;
    case KbmControl::kY:
      state.buttons |= kY;
      break;
    case KbmControl::kGuide:
      state.buttons |= kGuide;
      break;
    case KbmControl::kDpadLeft:
      state.buttons |= kDpadLeft;
      break;
    case KbmControl::kDpadRight:
      state.buttons |= kDpadRight;
      break;
    case KbmControl::kDpadDown:
      state.buttons |= kDpadDown;
      break;
    case KbmControl::kDpadUp:
      state.buttons |= kDpadUp;
      break;
    case KbmControl::kRThumbPress:
      state.buttons |= kRightThumb;
      break;
    case KbmControl::kLThumbPress:
      state.buttons |= kLeftThumb;
      break;
    case KbmControl::kBack:
      state.buttons |= kBack;
      break;
    case KbmControl::kStart:
      state.buttons |= kStart;
      break;
    case KbmControl::kLShoulder:
      state.buttons |= kLeftShoulder;
      break;
    case KbmControl::kRShoulder:
      state.buttons |= kRightShoulder;
      break;
    case KbmControl::kLTrigger:
      state.left_trigger = 0xFF;
      break;
    case KbmControl::kRTrigger:
      state.right_trigger = 0xFF;
      break;
    case KbmControl::kLThumbLeft:
      AddKbmThumb(state.thumb_lx, std::numeric_limits<int16_t>::min());
      break;
    case KbmControl::kLThumbRight:
      AddKbmThumb(state.thumb_lx, std::numeric_limits<int16_t>::max());
      break;
    case KbmControl::kLThumbDown:
      AddKbmThumb(state.thumb_ly, std::numeric_limits<int16_t>::min());
      break;
    case KbmControl::kLThumbUp:
      AddKbmThumb(state.thumb_ly, std::numeric_limits<int16_t>::max());
      break;
    case KbmControl::kRThumbUp:
      AddKbmThumb(state.thumb_ry, std::numeric_limits<int16_t>::max());
      break;
    case KbmControl::kRThumbDown:
      AddKbmThumb(state.thumb_ry, std::numeric_limits<int16_t>::min());
      break;
    case KbmControl::kRThumbRight:
      AddKbmThumb(state.thumb_rx, std::numeric_limits<int16_t>::max());
      break;
    case KbmControl::kRThumbLeft:
      AddKbmThumb(state.thumb_rx, std::numeric_limits<int16_t>::min());
      break;
  }
}

}  // namespace xe::hid::kbm
