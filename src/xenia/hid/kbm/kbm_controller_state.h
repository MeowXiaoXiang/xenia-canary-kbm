/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2026 Xenia authors. All rights reserved.                         *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#ifndef XENIA_HID_KBM_KBM_CONTROLLER_STATE_H_
#define XENIA_HID_KBM_KBM_CONTROLLER_STATE_H_

#include <cstdint>

namespace xe::hid::kbm {

// Platform-neutral names for the XInput-compatible controls KBM can emulate.
// The values are the documented VK_PAD offsets used by XInputGetKeystroke.
enum class KbmControl : uint8_t {
  kA = 0x00,
  kB = 0x01,
  kX = 0x02,
  kY = 0x03,
  kRShoulder = 0x04,
  kLShoulder = 0x05,
  kLTrigger = 0x06,
  kRTrigger = 0x07,
  kDpadUp = 0x10,
  kDpadDown = 0x11,
  kDpadLeft = 0x12,
  kDpadRight = 0x13,
  kStart = 0x14,
  kBack = 0x15,
  kLThumbPress = 0x16,
  kRThumbPress = 0x17,
  kLThumbUp = 0x20,
  kLThumbDown = 0x21,
  kLThumbRight = 0x22,
  kLThumbLeft = 0x23,
  kRThumbUp = 0x30,
  kRThumbDown = 0x31,
  kRThumbRight = 0x32,
  kRThumbLeft = 0x33,
  kGuide = 0x38,
};

struct KbmControllerState {
  uint16_t buttons = 0;
  uint8_t left_trigger = 0;
  uint8_t right_trigger = 0;
  int16_t thumb_lx = 0;
  int16_t thumb_ly = 0;
  int16_t thumb_rx = 0;
  int16_t thumb_ry = 0;
};

void ApplyKbmControl(KbmControllerState& state, KbmControl control);
void AddKbmThumb(int16_t& destination, int16_t value);

}  // namespace xe::hid::kbm

#endif  // XENIA_HID_KBM_KBM_CONTROLLER_STATE_H_
