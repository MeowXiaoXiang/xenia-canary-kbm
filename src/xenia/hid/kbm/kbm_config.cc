/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2026 Ben Vanik. All rights reserved.                             *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#include "xenia/hid/kbm/kbm_config.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <optional>
#include <string_view>
#include <vector>

#include "third_party/fmt/include/fmt/format.h"
#include "third_party/tomlplusplus/toml.hpp"
#include "xenia/base/filesystem.h"
#include "xenia/base/logging.h"
#include "xenia/base/platform_win.h"

#define XE_HID_KBM_BINDING(button, description, cvar_name, cvar_default_value) \
  DEFINE_transient_string(kbm_##cvar_name, cvar_default_value,                 \
                          "Keys or chords bound to " description               \
                          ", with alternatives separated by spaces",           \
                          "HID.KBM")
#include "xenia/hid/kbm/kbm_binding_table.inc"
#undef XE_HID_KBM_BINDING

DEFINE_transient_bool(kbm_enabled, true, "Enable the KBM virtual controller.",
                      "HID.KBM");

DEFINE_transient_int32(kbm_user_index, 0,
                       "Controller port that KBM emulates. [0, 3].", "HID.KBM");

DEFINE_transient_bool(
    raw_mouse, true,
    "Use Windows Raw Input mouse movement for the emulated right "
    "thumbstick. Requires kbm_enabled = true.",
    "HID.KBM");

DEFINE_transient_double(raw_mouse_sensitivity, 10.0,
                        "Raw mouse sensitivity multiplier.", "HID.KBM");

DEFINE_transient_double(
    raw_mouse_full_scale_velocity, 24000.0,
    "Raw Input counts per second that produce full right-stick deflection "
    "before applying the response curve.",
    "HID.KBM");

DEFINE_transient_double(
    raw_mouse_response_curve, 0.8,
    "Raw mouse response exponent. 1 is linear, values above 1 add precision "
    "near the center, and values below 1 boost low speeds.",
    "HID.KBM");

DEFINE_transient_double(
    raw_mouse_smoothing_time_ms, 8.0,
    "Time-based smoothing applied to Raw Input mouse velocity. Zero disables "
    "smoothing.",
    "HID.KBM");

DEFINE_transient_bool(
    raw_mouse_deadzone_compensation, false,
    "Raise non-zero Raw Input mouse output above a configurable minimum "
    "response. Off preserves the original mouse-to-stick translation.",
    "HID.KBM");

DEFINE_transient_double(
    raw_mouse_minimum_response, 0.30,
    "Minimum absolute right-stick response applied only while Raw Input mouse "
    "movement is non-zero and deadzone compensation is enabled.",
    "HID.KBM");

DEFINE_transient_bool(raw_mouse_invert_y, false,
                      "Invert Raw Input mouse Y movement.", "HID.KBM");

DEFINE_transient_string(
    raw_mouse_capture_toggle_key, "Key.F8",
    "Key or modifier chord used to toggle Raw Input mouse capture. Leave "
    "empty to disable the hotkey.",
    "HID.KBM");

DEFINE_transient_bool(
    raw_mouse_capture_on_start, false,
    "Capture and hide the mouse when Raw Input is initialized. The mouse is "
    "automatically released when Xenia loses focus.",
    "HID.KBM");

namespace xe::hid::kbm {
namespace {

std::filesystem::path config_path;
KbmConfigState config_state = KbmConfigState::kMissing;

struct KbmConfigEntry {
  std::string_view table;
  std::string_view key;
  uint8_t table_order;
};

std::optional<KbmConfigEntry> GetKbmConfigEntry(
    const cvar::IConfigVar& config_var) {
  const std::string_view name = config_var.name();
  if (name == "kbm_enabled") {
    return {{"HID.KBM.Controller", "enabled", 0}};
  }
  if (name == "kbm_user_index") {
    return {{"HID.KBM.Controller", "user_index", 0}};
  }
  if (name.starts_with("kbm_keybind_")) {
    return {{"HID.KBM.Bindings", name.substr(12), 1}};
  }
  if (name == "raw_mouse") {
    return {{"HID.KBM.RawMouse", "enabled", 2}};
  }
  if (name == "raw_mouse_capture_on_start") {
    return {{"HID.KBM.RawMouse", "capture_on_start", 2}};
  }
  if (name == "raw_mouse_capture_toggle_key") {
    return {{"HID.KBM.RawMouse", "capture_toggle_key", 2}};
  }
  if (name == "raw_mouse_deadzone_compensation") {
    return {{"HID.KBM.RawMouse", "deadzone_compensation", 2}};
  }
  if (name == "raw_mouse_invert_y") {
    return {{"HID.KBM.RawMouse", "invert_y", 2}};
  }
  if (name == "raw_mouse_sensitivity") {
    return {{"HID.KBM.RawMouse.Tuning", "sensitivity", 3}};
  }
  if (name == "raw_mouse_full_scale_velocity") {
    return {{"HID.KBM.RawMouse.Tuning", "full_scale_velocity", 3}};
  }
  if (name == "raw_mouse_response_curve") {
    return {{"HID.KBM.RawMouse.Tuning", "response_curve", 3}};
  }
  if (name == "raw_mouse_smoothing_time_ms") {
    return {{"HID.KBM.RawMouse.Tuning", "smoothing_time_ms", 3}};
  }
  if (name == "raw_mouse_minimum_response") {
    return {{"HID.KBM.RawMouse.Tuning", "minimum_response", 3}};
  }
  return std::nullopt;
}

bool IsKbmConfigVar(const cvar::IConfigVar& config_var) {
  return GetKbmConfigEntry(config_var).has_value();
}

bool IsBindingConfigVar(const cvar::IConfigVar& config_var) {
  return config_var.name().starts_with("kbm_keybind_") ||
         config_var.name() == "raw_mouse_capture_toggle_key";
}

bool IsCanonicalBinding(std::string_view binding) {
  size_t token_begin = 0;
  while (token_begin < binding.size()) {
    const size_t token_end = binding.find(' ', token_begin);
    std::string_view token =
        binding.substr(token_begin, token_end == std::string_view::npos
                                        ? binding.size() - token_begin
                                        : token_end - token_begin);
    if (!token.empty()) {
      if (token.front() == '_' || token.front() == '^') {
        token.remove_prefix(1);
      }
      KbmChord chord;
      if (!ParseKbmChord(token, chord)) {
        return false;
      }
    }
    if (token_end == std::string_view::npos) {
      break;
    }
    token_begin = token_end + 1;
  }
  return true;
}

bool IsBooleanSetting(std::string_view name) {
  return name == "kbm_enabled" || name == "raw_mouse" ||
         name == "raw_mouse_deadzone_compensation" ||
         name == "raw_mouse_invert_y" || name == "raw_mouse_capture_on_start";
}

bool IsNumberSetting(std::string_view name) {
  return name == "raw_mouse_sensitivity" ||
         name == "raw_mouse_full_scale_velocity" ||
         name == "raw_mouse_response_curve" ||
         name == "raw_mouse_smoothing_time_ms" ||
         name == "raw_mouse_minimum_response";
}

bool IsCompatibleConfigValue(const cvar::IConfigVar& config_var,
                             const toml::node& node) {
  if (IsBindingConfigVar(config_var)) {
    const auto binding = node.value<std::string>();
    return binding && IsCanonicalBinding(*binding);
  }
  if (config_var.name() == "kbm_user_index") {
    return node.is_integer();
  }
  if (IsBooleanSetting(config_var.name())) {
    return node.is_boolean();
  }
  if (IsNumberSetting(config_var.name())) {
    return node.is_integer() || node.is_floating_point();
  }
  return false;
}

toml::node_view<const toml::node> GetConfigNode(const toml::table& table,
                                                const KbmConfigEntry& entry) {
  return table.at_path(
      toml::path(std::string(entry.table) + "." + std::string(entry.key)));
}

void LoadConfig() {
  toml::parse_result parsed;
  try {
    parsed = toml::parse_file(xe::path_to_utf8(config_path));
  } catch (const toml::parse_error& e) {
    config_state = KbmConfigState::kIncompatible;
    XELOGE("kbm: failed to parse '{}': {}", config_path, e.what());
    return;
  }

  const auto schema_version = parsed.at_path("schema_version").value<int64_t>();
  if (!IsKbmConfigSchemaVersionSupported(schema_version)) {
    config_state = KbmConfigState::kIncompatible;
    XELOGW(
        "kbm: ignored incompatible config '{}' (expected schema_version = "
        "{}).",
        config_path, kKbmConfigSchemaVersion);
    return;
  }

  if (!cvar::ConfigVars) {
    return;
  }
  for (const auto& [name, config_var] : *cvar::ConfigVars) {
    const auto entry = GetKbmConfigEntry(*config_var);
    if (!entry) {
      continue;
    }
    const auto node = GetConfigNode(parsed, *entry);
    if (!node || !IsCompatibleConfigValue(*config_var, *node.node())) {
      config_state = KbmConfigState::kIncompatible;
      XELOGW("kbm: ignored incompatible setting '{}' in '{}'.",
             std::string(entry->table) + "." + std::string(entry->key),
             config_path);
      return;
    }
  }
  for (const auto& [name, config_var] : *cvar::ConfigVars) {
    const auto entry = GetKbmConfigEntry(*config_var);
    if (!entry) {
      continue;
    }
    const auto node = GetConfigNode(parsed, *entry);
    if (node) {
      config_var->LoadConfigValue(node.node());
    }
  }
  config_state = KbmConfigState::kCompatible;
  XELOGI("kbm: loaded config '{}'.", config_path);
}

}  // namespace

void SetupConfig(const std::filesystem::path& storage_root,
                 bool create_if_kbm_selected) {
  config_path = storage_root / "kbm.toml";
  config_state = KbmConfigState::kMissing;
  if (std::filesystem::exists(config_path)) {
    LoadConfig();
  } else if (create_if_kbm_selected) {
    SaveConfig();
  }
}

bool ConfigExists() {
  return !config_path.empty() && std::filesystem::exists(config_path);
}

KbmConfigState GetConfigState() { return config_state; }

const std::filesystem::path& ConfigPath() { return config_path; }

bool SaveConfig() {
  if (config_path.empty() || !cvar::ConfigVars) {
    return false;
  }

  struct ConfigVarEntry {
    cvar::IConfigVar* config_var;
    KbmConfigEntry entry;
  };
  std::vector<ConfigVarEntry> vars;
  for (const auto& [name, config_var] : *cvar::ConfigVars) {
    if (const auto entry = GetKbmConfigEntry(*config_var)) {
      vars.push_back({config_var, *entry});
    }
  }
  std::sort(vars.begin(), vars.end(), [](const auto& a, const auto& b) {
    return a.entry.table_order == b.entry.table_order
               ? a.entry.key < b.entry.key
               : a.entry.table_order < b.entry.table_order;
  });

  xe::filesystem::CreateParentFolder(config_path);
  auto temporary_path = config_path;
  temporary_path += ".tmp";
  FILE* file = xe::filesystem::OpenFile(temporary_path, "wb");
  if (!file) {
    XELOGE("kbm: failed to open '{}' for writing.", config_path);
    return false;
  }

  std::string output =
      "# KBM Controller keyboard and Raw Input mouse settings.\n"
      "# This file is intentionally separate from xenia-canary.config.toml.\n"
      "# Settings are grouped by controller, bindings and mouse purpose.\n"
      "schema_version = " +
      std::to_string(kKbmConfigSchemaVersion) + "\n";
  std::string category;
  for (const auto& var : vars) {
    if (category != var.entry.table) {
      category = var.entry.table;
      output += "\n[" + category + "]\n";
    }
    output +=
        std::string(var.entry.key) + " = " + var.config_var->config_value();
    if (!var.config_var->description().empty()) {
      output += " # " + var.config_var->description();
    }
    output += '\n';
  }

  const bool written =
      fwrite(output.data(), 1, output.size(), file) == output.size();
  const bool closed = fclose(file) == 0;
  if (!written || !closed ||
      !MoveFileExW(temporary_path.c_str(), config_path.c_str(),
                   MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH)) {
    std::error_code ignored;
    std::filesystem::remove(temporary_path, ignored);
    XELOGE("kbm: failed to write '{}'.", config_path);
    return false;
  }
  config_state = KbmConfigState::kCompatible;
  XELOGI("kbm: saved config '{}'.", config_path);
  return true;
}

KbmSettings NormalizeSettings(KbmSettings settings) {
  const KbmSettings defaults;
  auto finite_clamp = [](double value, double fallback, double low,
                         double high) {
    return std::clamp(std::isfinite(value) ? value : fallback, low, high);
  };
  settings.user_index = std::clamp(settings.user_index, 0, 3);
  settings.raw_mouse_sensitivity =
      finite_clamp(settings.raw_mouse_sensitivity,
                   defaults.raw_mouse_sensitivity, 0.01, 256.0);
  settings.raw_mouse_full_scale_velocity =
      finite_clamp(settings.raw_mouse_full_scale_velocity,
                   defaults.raw_mouse_full_scale_velocity, 1.0, 1000000.0);
  settings.raw_mouse_response_curve =
      finite_clamp(settings.raw_mouse_response_curve,
                   defaults.raw_mouse_response_curve, 0.1, 4.0);
  settings.raw_mouse_smoothing_time_ms =
      finite_clamp(settings.raw_mouse_smoothing_time_ms,
                   defaults.raw_mouse_smoothing_time_ms, 0.0, 20.0);
  settings.raw_mouse_minimum_response =
      finite_clamp(settings.raw_mouse_minimum_response,
                   defaults.raw_mouse_minimum_response, 0.0, 0.5);
  return settings;
}

KbmSettings GetSettingsFromCvars() {
  KbmSettings settings;
  settings.enabled = cvars::kbm_enabled;
  settings.user_index = cvars::kbm_user_index;
#define XE_HID_KBM_BINDING(button, description, cvar_name, cvar_default_value) \
  settings.cvar_name = cvars::kbm_##cvar_name;
#include "xenia/hid/kbm/kbm_binding_table.inc"
#undef XE_HID_KBM_BINDING
  settings.raw_mouse = cvars::raw_mouse;
  settings.raw_mouse_sensitivity = cvars::raw_mouse_sensitivity;
  settings.raw_mouse_full_scale_velocity = cvars::raw_mouse_full_scale_velocity;
  settings.raw_mouse_response_curve = cvars::raw_mouse_response_curve;
  settings.raw_mouse_smoothing_time_ms = cvars::raw_mouse_smoothing_time_ms;
  settings.raw_mouse_deadzone_compensation =
      cvars::raw_mouse_deadzone_compensation;
  settings.raw_mouse_minimum_response = cvars::raw_mouse_minimum_response;
  settings.raw_mouse_invert_y = cvars::raw_mouse_invert_y;
  settings.raw_mouse_capture_toggle_key = cvars::raw_mouse_capture_toggle_key;
  settings.raw_mouse_capture_on_start = cvars::raw_mouse_capture_on_start;
  return NormalizeSettings(settings);
}

void ApplySettingsToCvars(const KbmSettings& source_settings) {
  const KbmSettings settings = NormalizeSettings(source_settings);
  OVERRIDE_bool(kbm_enabled, settings.enabled);
  OVERRIDE_int32(kbm_user_index, std::clamp(settings.user_index, 0, 3));
#define XE_HID_KBM_BINDING(button, description, cvar_name, cvar_default_value) \
  OVERRIDE_string(kbm_##cvar_name, settings.cvar_name);
#include "xenia/hid/kbm/kbm_binding_table.inc"
#undef XE_HID_KBM_BINDING
  OVERRIDE_bool(raw_mouse, settings.raw_mouse);
  OVERRIDE_double(raw_mouse_sensitivity,
                  std::clamp(settings.raw_mouse_sensitivity, 0.01, 256.0));
  OVERRIDE_double(
      raw_mouse_full_scale_velocity,
      std::clamp(settings.raw_mouse_full_scale_velocity, 1.0, 1000000.0));
  OVERRIDE_double(raw_mouse_response_curve,
                  std::clamp(settings.raw_mouse_response_curve, 0.1, 4.0));
  OVERRIDE_double(raw_mouse_smoothing_time_ms,
                  std::clamp(settings.raw_mouse_smoothing_time_ms, 0.0, 20.0));
  OVERRIDE_bool(raw_mouse_deadzone_compensation,
                settings.raw_mouse_deadzone_compensation);
  OVERRIDE_double(raw_mouse_minimum_response,
                  std::clamp(settings.raw_mouse_minimum_response, 0.0, 0.5));
  OVERRIDE_bool(raw_mouse_invert_y, settings.raw_mouse_invert_y);
  OVERRIDE_string(raw_mouse_capture_toggle_key,
                  settings.raw_mouse_capture_toggle_key);
  OVERRIDE_bool(raw_mouse_capture_on_start,
                settings.raw_mouse_capture_on_start);
}

}  // namespace xe::hid::kbm
