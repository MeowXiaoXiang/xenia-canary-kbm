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
#include <array>
#include <cctype>
#include <charconv>
#include <cmath>
#include <cstdio>
#include <vector>

#include "third_party/fmt/include/fmt/format.h"
#include "third_party/tomlplusplus/toml.hpp"
#include "xenia/base/filesystem.h"
#include "xenia/base/logging.h"
#include "xenia/base/platform_win.h"
#include "xenia/base/string.h"

DECLARE_string(hid);
DECLARE_bool(kbm_enabled);
DECLARE_int32(kbm_user_index);

#define XE_HID_KBM_BINDING(button, description, cvar_name, cvar_default_value) \
  DECLARE_string(kbm_##cvar_name);
#include "xenia/hid/kbm/kbm_binding_table.inc"
#undef XE_HID_KBM_BINDING

DECLARE_bool(raw_mouse);
DECLARE_double(raw_mouse_sensitivity);
DECLARE_double(raw_mouse_full_scale_velocity);
DECLARE_double(raw_mouse_response_curve);
DECLARE_double(raw_mouse_smoothing_time_ms);
DECLARE_bool(raw_mouse_deadzone_compensation);
DECLARE_double(raw_mouse_minimum_response);
DECLARE_bool(raw_mouse_invert_y);
DECLARE_string(raw_mouse_capture_toggle_key);
DECLARE_bool(raw_mouse_capture_on_start);

namespace xe::hid::kbm {
namespace {

struct NamedVirtualKey {
  std::string_view canonical_name;
  std::string_view display_name;
  uint16_t virtual_key;
};

constexpr NamedVirtualKey kNamedVirtualKeys[] = {
    {"MouseLeft", "Mouse Left", VK_LBUTTON},
    {"MouseRight", "Mouse Right", VK_RBUTTON},
    {"MouseMiddle", "Mouse Middle", VK_MBUTTON},
    {"MouseX1", "Mouse X1", VK_XBUTTON1},
    {"MouseX2", "Mouse X2", VK_XBUTTON2},
    {"Backspace", "Backspace", VK_BACK},
    {"Tab", "Tab", VK_TAB},
    {"Enter", "Enter", VK_RETURN},
    {"Shift", "Shift", VK_SHIFT},
    {"Ctrl", "Ctrl", VK_CONTROL},
    {"Alt", "Alt", VK_MENU},
    {"Pause", "Pause", VK_PAUSE},
    {"CapsLock", "Caps Lock", VK_CAPITAL},
    {"Esc", "Esc", VK_ESCAPE},
    {"Space", "Space", VK_SPACE},
    {"PageUp", "Page Up", VK_PRIOR},
    {"PageDown", "Page Down", VK_NEXT},
    {"End", "End", VK_END},
    {"Home", "Home", VK_HOME},
    {"Left", "Left", VK_LEFT},
    {"Up", "Up", VK_UP},
    {"Right", "Right", VK_RIGHT},
    {"Down", "Down", VK_DOWN},
    {"Insert", "Insert", VK_INSERT},
    {"Delete", "Delete", VK_DELETE},
    {"Win", "Win", VK_LWIN},
    {"Apps", "Menu", VK_APPS},
    {"NumLock", "Num Lock", VK_NUMLOCK},
    {"NumpadMultiply", "Numpad *", VK_MULTIPLY},
    {"NumpadAdd", "Numpad +", VK_ADD},
    {"NumpadSubtract", "Numpad -", VK_SUBTRACT},
    {"NumpadDecimal", "Numpad .", VK_DECIMAL},
    {"NumpadDivide", "Numpad /", VK_DIVIDE},
};

std::string NormalizeKeyName(std::string_view text) {
  std::string normalized;
  normalized.reserve(text.size());
  for (char character : text) {
    if (character == ' ' || character == '-' || character == '_') {
      continue;
    }
    normalized.push_back(
        static_cast<char>(std::tolower(static_cast<unsigned char>(character))));
  }
  return normalized;
}

bool ParseUnsigned(std::string_view text, int base, uint16_t& value) {
  unsigned int parsed = 0;
  const auto result =
      std::from_chars(text.data(), text.data() + text.size(), parsed, base);
  if (result.ec != std::errc() || result.ptr != text.data() + text.size() ||
      parsed > UINT16_MAX) {
    return false;
  }
  value = static_cast<uint16_t>(parsed);
  return true;
}

bool ParseVirtualKeyName(std::string_view text, uint16_t& virtual_key) {
  if (text.size() > 2 && text[0] == '0' && (text[1] == 'x' || text[1] == 'X')) {
    return ParseUnsigned(text.substr(2), 16, virtual_key);
  }

  if (text.size() == 1 && std::isalnum(static_cast<unsigned char>(text[0]))) {
    virtual_key = static_cast<uint16_t>(
        std::toupper(static_cast<unsigned char>(text[0])));
    return true;
  }

  const std::string normalized = NormalizeKeyName(text);
  for (const NamedVirtualKey& key : kNamedVirtualKeys) {
    if (normalized == NormalizeKeyName(key.canonical_name) ||
        normalized == NormalizeKeyName(key.display_name)) {
      virtual_key = key.virtual_key;
      return true;
    }
  }

  if (normalized == "control") {
    virtual_key = VK_CONTROL;
    return true;
  }
  if (normalized == "escape") {
    virtual_key = VK_ESCAPE;
    return true;
  }
  if (normalized == "windows" || normalized == "super") {
    virtual_key = VK_LWIN;
    return true;
  }

  if (normalized.size() >= 2 && normalized[0] == 'f') {
    uint16_t function_number = 0;
    if (ParseUnsigned(std::string_view(normalized).substr(1), 10,
                      function_number) &&
        function_number >= 1 && function_number <= 24) {
      virtual_key = static_cast<uint16_t>(VK_F1 + function_number - 1);
      return true;
    }
  }
  constexpr std::string_view kNumpadPrefix = "numpad";
  if (normalized.size() == kNumpadPrefix.size() + 1 &&
      normalized.starts_with(kNumpadPrefix) &&
      std::isdigit(static_cast<unsigned char>(normalized.back()))) {
    virtual_key = static_cast<uint16_t>(VK_NUMPAD0 + normalized.back() - '0');
    return true;
  }
  return false;
}

std::string VirtualKeyName(uint16_t virtual_key, bool display) {
  if ((virtual_key >= 'A' && virtual_key <= 'Z') ||
      (virtual_key >= '0' && virtual_key <= '9')) {
    return std::string(1, static_cast<char>(virtual_key));
  }
  if (virtual_key >= VK_F1 && virtual_key <= VK_F24) {
    return fmt::format("F{}", virtual_key - VK_F1 + 1);
  }
  if (virtual_key >= VK_NUMPAD0 && virtual_key <= VK_NUMPAD9) {
    return fmt::format("Numpad{}", virtual_key - VK_NUMPAD0);
  }
  for (const NamedVirtualKey& key : kNamedVirtualKeys) {
    if (key.virtual_key == virtual_key) {
      return std::string(display ? key.display_name : key.canonical_name);
    }
  }
  return fmt::format("0x{:02X}", virtual_key);
}

bool IsModifierName(std::string_view text, KbmChord& chord) {
  const std::string normalized = NormalizeKeyName(text);
  if (normalized == "shift") {
    chord.shift = true;
  } else if (normalized == "ctrl" || normalized == "control") {
    chord.ctrl = true;
  } else if (normalized == "alt") {
    chord.alt = true;
  } else if (normalized == "win" || normalized == "windows" ||
             normalized == "super") {
    chord.super = true;
  } else {
    return false;
  }
  return true;
}

std::string FormatChord(const KbmChord& chord, bool display) {
  std::string result;
  if (chord.ctrl) {
    result += "Ctrl+";
  }
  if (chord.alt) {
    result += "Alt+";
  }
  if (chord.shift) {
    result += "Shift+";
  }
  if (chord.super) {
    result += "Win+";
  }
  result += VirtualKeyName(chord.virtual_key, display);
  return result;
}

std::filesystem::path config_path;
KbmConfigState config_state = KbmConfigState::kMissing;

bool IsKbmConfigVar(const cvar::IConfigVar& config_var) {
  return config_var.category() == "HID.KBM";
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

  const auto schema_version =
      parsed.at_path("schema_version").value<int64_t>();
  if (!IsKbmConfigSchemaVersionSupported(schema_version)) {
    config_state = KbmConfigState::kIncompatible;
    XELOGW("kbm: ignored incompatible config '{}' (expected schema_version = "
           "{}).",
           config_path, kKbmConfigSchemaVersion);
    return;
  }

  if (!cvar::ConfigVars) {
    return;
  }
  for (const auto& [name, config_var] : *cvar::ConfigVars) {
    if (!IsKbmConfigVar(*config_var)) {
      continue;
    }
    const auto node = parsed.at_path(
        toml::path(config_var->category() + "." + config_var->name()));
    if (node) {
      config_var->LoadConfigValue(node.node());
    }
  }
  config_state = KbmConfigState::kCompatible;
  XELOGI("kbm: loaded config '{}'.", config_path);
}

}  // namespace

bool ParseKbmChord(std::string_view text, KbmChord& chord) {
  chord = {};
  if (text.empty()) {
    return false;
  }

  size_t part_begin = 0;
  while (true) {
    const size_t plus = text.find('+', part_begin);
    const bool is_last = plus == std::string_view::npos;
    const std::string_view part = text.substr(
        part_begin, is_last ? text.size() - part_begin : plus - part_begin);
    if (part.empty()) {
      return false;
    }
    if (is_last) {
      return ParseVirtualKeyName(part, chord.virtual_key);
    }
    if (!IsModifierName(part, chord)) {
      return false;
    }
    part_begin = plus + 1;
  }
}

std::string FormatKbmChord(const KbmChord& chord) {
  return chord.virtual_key ? FormatChord(chord, false) : std::string();
}

std::string FormatKbmBinding(std::string_view binding) {
  std::string result;
  for (std::string_view token : utf8::split(binding, " ", true)) {
    if (!token.empty() && (token.front() == '_' || token.front() == '^')) {
      token.remove_prefix(1);
    }
    KbmChord chord;
    const std::string display = ParseKbmChord(token, chord)
                                    ? FormatChord(chord, true)
                                    : std::string(token);
    if (!result.empty()) {
      result += " / ";
    }
    result += display;
  }
  return result.empty() ? "Unbound" : result;
}

void SetupConfig(const std::filesystem::path& storage_root) {
  config_path = storage_root / "kbm.toml";
  config_state = KbmConfigState::kMissing;
  if (std::filesystem::exists(config_path)) {
    LoadConfig();
  } else if (cvars::hid == "kbm") {
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

  std::vector<cvar::IConfigVar*> vars;
  for (const auto& [name, config_var] : *cvar::ConfigVars) {
    if (IsKbmConfigVar(*config_var)) {
      vars.push_back(config_var);
    }
  }
  std::sort(vars.begin(), vars.end(), [](const auto* a, const auto* b) {
    return a->category() == b->category() ? a->name() < b->name()
                                          : a->category() < b->category();
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
      "schema_version = " + std::to_string(kKbmConfigSchemaVersion) + "\n";
  std::string category;
  for (const auto* config_var : vars) {
    if (category != config_var->category()) {
      category = config_var->category();
      output += "\n[" + category + "]\n";
    }
    output += config_var->name() + " = " + config_var->config_value();
    if (!config_var->description().empty()) {
      output += " # " + config_var->description();
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
  cvars::kbm_enabled = settings.enabled;
  cvars::kbm_user_index = std::clamp(settings.user_index, 0, 3);
#define XE_HID_KBM_BINDING(button, description, cvar_name, cvar_default_value) \
  cvars::kbm_##cvar_name = settings.cvar_name;
#include "xenia/hid/kbm/kbm_binding_table.inc"
#undef XE_HID_KBM_BINDING
  cvars::raw_mouse = settings.raw_mouse;
  cvars::raw_mouse_sensitivity =
      std::clamp(settings.raw_mouse_sensitivity, 0.01, 256.0);
  cvars::raw_mouse_full_scale_velocity =
      std::clamp(settings.raw_mouse_full_scale_velocity, 1.0, 1000000.0);
  cvars::raw_mouse_response_curve =
      std::clamp(settings.raw_mouse_response_curve, 0.1, 4.0);
  cvars::raw_mouse_smoothing_time_ms =
      std::clamp(settings.raw_mouse_smoothing_time_ms, 0.0, 20.0);
  cvars::raw_mouse_deadzone_compensation =
      settings.raw_mouse_deadzone_compensation;
  cvars::raw_mouse_minimum_response =
      std::clamp(settings.raw_mouse_minimum_response, 0.0, 0.5);
  cvars::raw_mouse_invert_y = settings.raw_mouse_invert_y;
  cvars::raw_mouse_capture_toggle_key = settings.raw_mouse_capture_toggle_key;
  cvars::raw_mouse_capture_on_start = settings.raw_mouse_capture_on_start;
}

}  // namespace xe::hid::kbm
