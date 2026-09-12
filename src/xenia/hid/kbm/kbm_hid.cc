/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2014 Ben Vanik. All rights reserved.                             *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#include "xenia/hid/kbm/kbm_hid.h"

#include "xenia/hid/kbm/kbm_input_driver.h"

namespace xe {
namespace hid {
namespace kbm {

std::unique_ptr<InputDriver> Create(xe::ui::Window* window,
                                    size_t window_z_order) {
  return std::make_unique<KbmInputDriver>(window, window_z_order);
}

}  // namespace kbm
}  // namespace hid
}  // namespace xe
