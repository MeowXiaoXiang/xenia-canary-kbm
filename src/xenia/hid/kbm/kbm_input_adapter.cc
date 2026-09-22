/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2026 Xenia authors. All rights reserved.                         *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#include "xenia/hid/kbm/kbm_input_adapter.h"

namespace xe::hid::kbm {

KbmInputCode ToKbmInputCode(const ui::KeyEvent& event) {
  const ui::VirtualKey key = event.virtual_key();
  const auto value = static_cast<uint16_t>(key);
  if (value >= static_cast<uint16_t>(ui::VirtualKey::kA) &&
      value <= static_cast<uint16_t>(ui::VirtualKey::kZ)) {
    return static_cast<KbmInputCode>(
        static_cast<uint16_t>(KbmInputCode::kKeyA) + value -
        static_cast<uint16_t>(ui::VirtualKey::kA));
  }
  if (value >= static_cast<uint16_t>(ui::VirtualKey::k0) &&
      value <= static_cast<uint16_t>(ui::VirtualKey::k9)) {
    return static_cast<KbmInputCode>(
        static_cast<uint16_t>(KbmInputCode::kDigit0) + value -
        static_cast<uint16_t>(ui::VirtualKey::k0));
  }
  if (value >= static_cast<uint16_t>(ui::VirtualKey::kF1) &&
      value <= static_cast<uint16_t>(ui::VirtualKey::kF24)) {
    return static_cast<KbmInputCode>(
        static_cast<uint16_t>(KbmInputCode::kF1) + value -
        static_cast<uint16_t>(ui::VirtualKey::kF1));
  }
  if (value >= static_cast<uint16_t>(ui::VirtualKey::kNumpad0) &&
      value <= static_cast<uint16_t>(ui::VirtualKey::kNumpad9)) {
    return static_cast<KbmInputCode>(
        static_cast<uint16_t>(KbmInputCode::kNumpad0) + value -
        static_cast<uint16_t>(ui::VirtualKey::kNumpad0));
  }
  switch (key) {
    case ui::VirtualKey::kLButton:
      return KbmInputCode::kMouseLeft;
    case ui::VirtualKey::kRButton:
      return KbmInputCode::kMouseRight;
    case ui::VirtualKey::kMButton:
      return KbmInputCode::kMouseMiddle;
    case ui::VirtualKey::kXButton1:
      return KbmInputCode::kMouseX1;
    case ui::VirtualKey::kXButton2:
      return KbmInputCode::kMouseX2;
    case ui::VirtualKey::kBack:
      return KbmInputCode::kBackspace;
    case ui::VirtualKey::kTab:
      return KbmInputCode::kTab;
    case ui::VirtualKey::kReturn:
      return KbmInputCode::kEnter;
    case ui::VirtualKey::kShift:
      if (event.scan_code() == 0x2A) {
        return KbmInputCode::kLeftShift;
      }
      if (event.scan_code() == 0x36) {
        return KbmInputCode::kRightShift;
      }
      return KbmInputCode::kShift;
    case ui::VirtualKey::kLShift:
      return KbmInputCode::kLeftShift;
    case ui::VirtualKey::kRShift:
      return KbmInputCode::kRightShift;
    case ui::VirtualKey::kControl:
      return event.extended() ? KbmInputCode::kRightCtrl
                              : KbmInputCode::kLeftCtrl;
    case ui::VirtualKey::kLControl:
      return KbmInputCode::kLeftCtrl;
    case ui::VirtualKey::kRControl:
      return KbmInputCode::kRightCtrl;
    case ui::VirtualKey::kMenu:
      return event.extended() ? KbmInputCode::kRightAlt
                              : KbmInputCode::kLeftAlt;
    case ui::VirtualKey::kLMenu:
      return KbmInputCode::kLeftAlt;
    case ui::VirtualKey::kRMenu:
      return KbmInputCode::kRightAlt;
    case ui::VirtualKey::kPause:
      return KbmInputCode::kPause;
    case ui::VirtualKey::kCapital:
      return KbmInputCode::kCapsLock;
    case ui::VirtualKey::kEscape:
      return KbmInputCode::kEscape;
    case ui::VirtualKey::kSpace:
      return KbmInputCode::kSpace;
    case ui::VirtualKey::kPrior:
      return KbmInputCode::kPageUp;
    case ui::VirtualKey::kNext:
      return KbmInputCode::kPageDown;
    case ui::VirtualKey::kEnd:
      return KbmInputCode::kEnd;
    case ui::VirtualKey::kHome:
      return KbmInputCode::kHome;
    case ui::VirtualKey::kLeft:
      return KbmInputCode::kArrowLeft;
    case ui::VirtualKey::kUp:
      return KbmInputCode::kArrowUp;
    case ui::VirtualKey::kRight:
      return KbmInputCode::kArrowRight;
    case ui::VirtualKey::kDown:
      return KbmInputCode::kArrowDown;
    case ui::VirtualKey::kInsert:
      return KbmInputCode::kInsert;
    case ui::VirtualKey::kDelete:
      return KbmInputCode::kDelete;
    case ui::VirtualKey::kLWin:
      return KbmInputCode::kLeftSuper;
    case ui::VirtualKey::kRWin:
      return KbmInputCode::kRightSuper;
    case ui::VirtualKey::kApps:
      return KbmInputCode::kMenu;
    case ui::VirtualKey::kNumLock:
      return KbmInputCode::kNumLock;
    case ui::VirtualKey::kMultiply:
      return KbmInputCode::kNumpadMultiply;
    case ui::VirtualKey::kAdd:
      return KbmInputCode::kNumpadAdd;
    case ui::VirtualKey::kSubtract:
      return KbmInputCode::kNumpadSubtract;
    case ui::VirtualKey::kDecimal:
      return KbmInputCode::kNumpadDecimal;
    case ui::VirtualKey::kDivide:
      return KbmInputCode::kNumpadDivide;
    default:
      return KbmInputCode::kNone;
  }
}

}  // namespace xe::hid::kbm
