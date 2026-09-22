/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2026 Xenia authors. All rights reserved.                         *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#ifndef XENIA_HID_KBM_KBM_BINDING_H_
#define XENIA_HID_KBM_KBM_BINDING_H_

#include <cstdint>
#include <string>
#include <string_view>

namespace xe::hid::kbm {

// Stable KBM input vocabulary. Values are private implementation details;
// kbm.toml stores the canonical names produced by FormatKbmInputCode.
enum class KbmInputCode : uint16_t {
  kNone = 0,
  kMouseLeft,
  kMouseRight,
  kMouseMiddle,
  kMouseX1,
  kMouseX2,
  kBackspace,
  kTab,
  kEnter,
  kShift,
  kLeftShift,
  kRightShift,
  kCtrl,
  kLeftCtrl,
  kRightCtrl,
  kAlt,
  kLeftAlt,
  kRightAlt,
  kPause,
  kCapsLock,
  kEscape,
  kSpace,
  kPageUp,
  kPageDown,
  kEnd,
  kHome,
  kArrowLeft,
  kArrowUp,
  kArrowRight,
  kArrowDown,
  kInsert,
  kDelete,
  kSuper,
  kLeftSuper,
  kRightSuper,
  kMenu,
  kNumLock,
  kNumpadMultiply,
  kNumpadAdd,
  kNumpadSubtract,
  kNumpadDecimal,
  kNumpadDivide,
  kKeyA = 0x100,
  kDigit0 = 0x120,
  kF1 = 0x140,
  kNumpad0 = 0x180,
};

struct KbmChord {
  KbmInputCode input = KbmInputCode::kNone;
  bool shift = false;
  bool ctrl = false;
  bool alt = false;
  bool super = false;
};

bool IsMouseInputCode(KbmInputCode code);
bool IsModifierInputCode(KbmInputCode code);
bool ParseKbmInputCode(std::string_view text, KbmInputCode& code);
std::string FormatKbmInputCode(KbmInputCode code, bool display = false);
bool ParseKbmChord(std::string_view text, KbmChord& chord);
std::string FormatKbmChord(const KbmChord& chord, bool display = false);
std::string FormatKbmBinding(std::string_view binding);

}  // namespace xe::hid::kbm

#endif  // XENIA_HID_KBM_KBM_BINDING_H_
