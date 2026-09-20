/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2026 Xenia authors. All rights reserved.                         *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#ifndef XENIA_HID_KBM_KBM_INPUT_STATE_H_
#define XENIA_HID_KBM_KBM_INPUT_STATE_H_

#include <bitset>
#include <cstddef>

#include "xenia/hid/kbm/kbm_binding.h"

namespace xe::hid::kbm {

struct KbmModifiers {
  bool shift = false;
  bool ctrl = false;
  bool alt = false;
  bool super = false;
  bool caps_lock = false;
};

// Platform adapters submit transitions in window event order. This state has
// no dependency on a platform keyboard API or ui::VirtualKey.
class KbmInputState {
 public:
  static constexpr size_t kCodeCount = 0x200;

  void SetPressed(KbmInputCode code, bool pressed);
  void SetCapsLock(bool enabled) { caps_lock_ = enabled; }
  void ResetMouseButtons();
  void Reset();

  bool IsPressed(KbmInputCode code) const;
  KbmModifiers modifiers() const;

 private:
  std::bitset<kCodeCount> pressed_;
  bool caps_lock_ = false;
};

}  // namespace xe::hid::kbm

#endif  // XENIA_HID_KBM_KBM_INPUT_STATE_H_
