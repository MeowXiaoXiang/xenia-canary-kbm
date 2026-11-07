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
#include <functional>
#include <optional>
#include <string>

#include "xenia/base/cvar.h"
#include "xenia/hid/kbm/kbm_binding.h"

namespace xe::hid::kbm {

inline constexpr int64_t kKbmConfigSchemaVersion = 4;

enum class KbmConfigState {
  kMissing,
  kCompatible,
  kNeedsMigration,
  kIncompatible,
};

inline bool IsKbmConfigSchemaVersionSupported(std::optional<int64_t> version) {
  return version && *version == kKbmConfigSchemaVersion;
}

struct KbmSettings {
  bool enabled = true;
  int32_t user_index = 0;

#define XE_HID_KBM_BINDING(button, description, cvar_name, cvar_default_value) \
  std::string cvar_name = cvar_default_value;
#include "xenia/hid/kbm/kbm_binding_table.inc"
#undef XE_HID_KBM_BINDING

  bool raw_mouse = true;
  double raw_mouse_dpi = 800.0;
  double raw_mouse_full_stick_speed_cm_s = 10.0;
  bool raw_mouse_deadzone_compensation = true;
  double raw_mouse_minimum_response = 0.30;
  bool raw_mouse_invert_y = false;
  std::string raw_mouse_capture_toggle_key = "Key.F8";
  bool raw_mouse_capture_on_start = false;
};

void SetupConfig(const std::filesystem::path& storage_root,
                 bool create_if_kbm_selected);
bool ConfigExists();
KbmConfigState GetConfigState();
const std::filesystem::path& ConfigPath();
bool SaveConfig();
bool SaveConfig(const KbmSettings& settings);
// Publish first; only a successful save may activate the draft.
bool CommitKbmSettings(KbmSettings& draft,
                       const std::function<bool(const KbmSettings&)>& persist,
                       const std::function<void(const KbmSettings&)>& activate);

KbmSettings GetSettingsFromCvars();
KbmSettings NormalizeSettings(KbmSettings settings);
void ApplySettingsToCvars(const KbmSettings& settings);

}  // namespace xe::hid::kbm

#endif  // XENIA_HID_KBM_KBM_CONFIG_H_
