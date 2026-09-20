/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2026 Xenia authors. All rights reserved.                         *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#ifndef XENIA_HID_KBM_KBM_INPUT_ADAPTER_H_
#define XENIA_HID_KBM_KBM_INPUT_ADAPTER_H_

#include "xenia/hid/kbm/kbm_binding.h"
#include "xenia/ui/ui_event.h"

namespace xe::hid::kbm {

// Windows/UI boundary only. KBM core and persisted bindings never expose
// ui::VirtualKey values. The original scan code distinguishes left and right
// modifiers without changing the upstream keyboard driver's event key.
KbmInputCode ToKbmInputCode(const ui::KeyEvent& event);

}  // namespace xe::hid::kbm

#endif  // XENIA_HID_KBM_KBM_INPUT_ADAPTER_H_
