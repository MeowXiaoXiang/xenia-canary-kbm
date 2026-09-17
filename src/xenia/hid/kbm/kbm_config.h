/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2026 Ben Vanik. All rights reserved.                             *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#ifndef XENIA_HID_KBM_KBM_CONFIG_H_
#define XENIA_HID_KBM_KBM_CONFIG_H_

#include <filesystem>
#include <string>
#include <string_view>

#include "xenia/base/cvar.h"

namespace xe::hid::kbm {

struct KbmChord {
  uint16_t virtual_key = 0;
  bool shift = false;
  bool ctrl = false;
  bool alt = false;
  bool super = false;
};

bool ParseKbmChord(std::string_view text, KbmChord& chord);
std::string FormatKbmChord(const KbmChord& chord);
std::string FormatKbmBinding(std::string_view binding);

struct KbmSettings {
  bool enabled = true;
  int32_t user_index = 0;

#define XE_HID_KBM_BINDING(button, description, cvar_name, cvar_default_value) \
  std::string cvar_name = cvar_default_value;
#include "xenia/hid/kbm/kbm_binding_table.inc"
#undef XE_HID_KBM_BINDING

  bool raw_mouse = true;
  double raw_mouse_sensitivity = 10.0;
  double raw_mouse_full_scale_velocity = 24000.0;
  double raw_mouse_response_curve = 0.8;
  double raw_mouse_smoothing_time_ms = 8.0;
  bool raw_mouse_deadzone_compensation = false;
  double raw_mouse_minimum_response = 0.30;
  bool raw_mouse_invert_y = false;
  std::string raw_mouse_capture_toggle_key = "F8";
  bool raw_mouse_capture_on_start = false;
};

void SetupConfig(const std::filesystem::path& storage_root);
bool ConfigExists();
const std::filesystem::path& ConfigPath();
bool SaveConfig();

KbmSettings GetSettingsFromCvars();
KbmSettings NormalizeSettings(KbmSettings settings);
void ApplySettingsToCvars(const KbmSettings& settings);

}  // namespace xe::hid::kbm

#endif  // XENIA_HID_KBM_KBM_CONFIG_H_
