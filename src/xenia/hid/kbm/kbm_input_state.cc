/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2026 Xenia authors. All rights reserved.                         *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#include "xenia/hid/kbm/kbm_input_state.h"

namespace xe::hid::kbm {
namespace {

bool IsPressed(const std::bitset<KbmInputState::kCodeCount>& pressed,
               KbmInputCode code) {
  const size_t index = static_cast<uint16_t>(code);
  return index < pressed.size() && pressed.test(index);
}

bool IsAnyPressed(const std::bitset<KbmInputState::kCodeCount>& pressed,
                  KbmInputCode generic, KbmInputCode left, KbmInputCode right) {
  return IsPressed(pressed, generic) || IsPressed(pressed, left) ||
         IsPressed(pressed, right);
}

}  // namespace

void KbmInputState::SetPressed(KbmInputCode code, bool pressed) {
  const size_t index = static_cast<uint16_t>(code);
  if (code != KbmInputCode::kNone && index < pressed_.size()) {
    pressed_.set(index, pressed);
  }
}

void KbmInputState::ResetMouseButtons() {
  for (uint16_t code = static_cast<uint16_t>(KbmInputCode::kMouseLeft);
       code <= static_cast<uint16_t>(KbmInputCode::kMouseX2); ++code) {
    pressed_.reset(code);
  }
}

void KbmInputState::Reset() {
  pressed_.reset();
  caps_lock_ = false;
}

bool KbmInputState::IsPressed(KbmInputCode code) const {
  switch (code) {
    case KbmInputCode::kShift:
      return IsAnyPressed(pressed_, code, KbmInputCode::kLeftShift,
                          KbmInputCode::kRightShift);
    case KbmInputCode::kCtrl:
      return IsAnyPressed(pressed_, code, KbmInputCode::kLeftCtrl,
                          KbmInputCode::kRightCtrl);
    case KbmInputCode::kAlt:
      return IsAnyPressed(pressed_, code, KbmInputCode::kLeftAlt,
                          KbmInputCode::kRightAlt);
    case KbmInputCode::kSuper:
      return IsAnyPressed(pressed_, code, KbmInputCode::kLeftSuper,
                          KbmInputCode::kRightSuper);
    default:
      break;
  }
  return kbm::IsPressed(pressed_, code);
}

KbmModifiers KbmInputState::modifiers() const {
  return {IsAnyPressed(pressed_, KbmInputCode::kShift, KbmInputCode::kLeftShift,
                       KbmInputCode::kRightShift),
          IsAnyPressed(pressed_, KbmInputCode::kCtrl, KbmInputCode::kLeftCtrl,
                       KbmInputCode::kRightCtrl),
          IsAnyPressed(pressed_, KbmInputCode::kAlt, KbmInputCode::kLeftAlt,
                       KbmInputCode::kRightAlt),
          IsAnyPressed(pressed_, KbmInputCode::kSuper, KbmInputCode::kLeftSuper,
                       KbmInputCode::kRightSuper),
          caps_lock_};
}

}  // namespace xe::hid::kbm
