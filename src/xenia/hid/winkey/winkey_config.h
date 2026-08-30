/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2026 Ben Vanik. All rights reserved.                             *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#ifndef XENIA_HID_WINKEY_WINKEY_CONFIG_H_
#define XENIA_HID_WINKEY_WINKEY_CONFIG_H_

#include <filesystem>
#include <string>
#include <string_view>

#include "xenia/base/cvar.h"

namespace xe::hid::winkey {

enum class KeyboardMode { Disabled, Enabled, Passthrough };

struct WinKeyChord {
  uint16_t virtual_key = 0;
  bool shift = false;
  bool ctrl = false;
  bool alt = false;
  bool super = false;
};

bool ParseWinKeyChord(std::string_view text, WinKeyChord& chord);
std::string FormatWinKeyChord(const WinKeyChord& chord);
std::string FormatWinKeyBinding(std::string_view binding);

struct WinKeySettings {
  int32_t keyboard_mode = 1;
  int32_t keyboard_user_index = 0;

#define XE_HID_WINKEY_BINDING(button, description, cvar_name, \
                              cvar_default_value)             \
  std::string cvar_name = cvar_default_value;
#include "xenia/hid/winkey/winkey_binding_table.inc"
#undef XE_HID_WINKEY_BINDING

  bool raw_mouse = true;
  double raw_mouse_sensitivity = 10.0;
  double raw_mouse_full_scale_velocity = 24000.0;
  double raw_mouse_response_curve = 1.2;
  bool raw_mouse_invert_y = false;
  std::string raw_mouse_capture_toggle_key = "F8";
  bool raw_mouse_capture_on_start = false;
};

void SetupConfig(const std::filesystem::path& storage_root);
bool ConfigExists();
const std::filesystem::path& ConfigPath();
bool SaveConfig();

WinKeySettings GetSettingsFromCvars();
void ApplySettingsToCvars(const WinKeySettings& settings);

}  // namespace xe::hid::winkey

#endif  // XENIA_HID_WINKEY_WINKEY_CONFIG_H_
