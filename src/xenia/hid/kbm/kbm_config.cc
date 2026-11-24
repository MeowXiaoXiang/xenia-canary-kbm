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
#include <atomic>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <optional>
#include <sstream>
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

DEFINE_transient_double(
    raw_mouse_dpi, 800.0,
    "Mouse hardware DPI, entered manually; not auto-detected. [100, 32000].",
    "HID.KBM");
DEFINE_transient_double(
    raw_mouse_full_stick_speed_cm_s, 10.0,
    "Estimated mouse speed that reaches full stick, in cm/s. [1, 100].",
    "HID.KBM");

DEFINE_transient_bool(
    raw_mouse_deadzone_compensation, true,
    "Enable stick anti-deadzone with a configurable minimum stick output. "
    "Off uses pure linear radial output.",
    "HID.KBM");

DEFINE_transient_double(
    raw_mouse_minimum_response, 0.30,
    "Minimum radial stick output used by stick anti-deadzone. A speed "
    "estimate of zero produces zero output.",
    "HID.KBM");

DEFINE_transient_string(raw_mouse_anti_deadzone_shape, "radial",
                        "Stick anti-deadzone shape: radial or axial.",
                        "HID.KBM");
DEFINE_transient_double(
    raw_mouse_minimum_response_x, 0.30,
    "Minimum horizontal output for axial anti-deadzone. [0, 0.5].", "HID.KBM");
DEFINE_transient_double(
    raw_mouse_minimum_response_y, 0.30,
    "Minimum vertical output for axial anti-deadzone. [0, 0.5].", "HID.KBM");

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

DEFINE_transient_string(
    raw_mouse_processing_mode,
    std::string(xe::hid::kbm::MouseModeName(xe::hid::kbm::kDefaultMouseMode)),
    "Mouse processing mode: fixed_window, ema or direct.", "HID.KBM");
DEFINE_transient_double(raw_mouse_fixed_window_ms,
                        xe::hid::kbm::kDefaultFixedWindowMs,
                        "Fixed mouse history window in milliseconds.",
                        "HID.KBM");
DEFINE_transient_double(raw_mouse_ema_tau_ms, xe::hid::kbm::kDefaultEmaTauMs,
                        "Mouse EMA time constant in milliseconds.", "HID.KBM");

DEFINE_transient_bool(
    kbm_overlay_enabled, xe::hid::kbm::KbmOverlaySettings{}.enabled,
    "Show in-game KBM diagnostics in Fixed window or EMA mode.", "HID.KBM");
DEFINE_transient_int32(kbm_overlay_corner,
                       xe::hid::kbm::KbmOverlaySettings{}.corner,
                       "Overlay corner: 0 top left, 1 top right, "
                       "2 bottom left, 3 bottom right.",
                       "HID.KBM");
DEFINE_transient_int32(
    kbm_overlay_ema_background_transparency,
    xe::hid::kbm::KbmOverlaySettings{}.ema_background_transparency,
    "EMA overlay background transparency, in percent. [0, 100].", "HID.KBM");

namespace xe::hid::kbm {
namespace {

std::filesystem::path config_path;
KbmConfigState config_state = KbmConfigState::kMissing;

// Exclusive creation prevents deleting or truncating another writer's file.
bool WriteNewConfigFile(const std::filesystem::path& path,
                        const std::string& bytes) {
  HANDLE handle = CreateFileW(path.c_str(), GENERIC_WRITE, 0, nullptr,
                              CREATE_NEW, FILE_ATTRIBUTE_NORMAL, nullptr);
  if (handle == INVALID_HANDLE_VALUE) {
    return false;
  }
  size_t offset = 0;
  DWORD error = ERROR_SUCCESS;
  while (offset < bytes.size()) {
    DWORD written = 0;
    const DWORD count =
        static_cast<DWORD>(std::min<size_t>(bytes.size() - offset, MAXDWORD));
    if (!WriteFile(handle, bytes.data() + offset, count, &written, nullptr) ||
        !written) {
      error = GetLastError();
      if (!error) {
        error = ERROR_WRITE_FAULT;
      }
      break;
    }
    offset += written;
  }
  if (!CloseHandle(handle) && !error) {
    error = GetLastError();
  }
  if (error) {
    DeleteFileW(path.c_str());
    SetLastError(error);
    return false;
  }
  return true;
}

std::optional<std::filesystem::path> WriteTemporaryConfigFile(
    const std::filesystem::path& destination, const std::string& bytes) {
  static std::atomic<uint64_t> next_sequence{0};
  for (int attempt = 0; attempt < 32; ++attempt) {
    auto path = destination;
    path += fmt::format(".{}.{}.tmp", GetCurrentProcessId(),
                        next_sequence.fetch_add(1, std::memory_order_relaxed));
    if (WriteNewConfigFile(path, bytes)) {
      return path;
    }
    const DWORD error = GetLastError();
    // A previous run may have left a file under a reused process ID.
    if (error != ERROR_FILE_EXISTS && error != ERROR_ALREADY_EXISTS) {
      break;
    }
  }
  return std::nullopt;
}

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
  if (name == "raw_mouse_dpi") {
    return {{"HID.KBM.RawMouse.Tuning", "dpi", 3}};
  }
  if (name == "raw_mouse_full_stick_speed_cm_s") {
    return {{"HID.KBM.RawMouse.Tuning", "full_stick_speed_cm_s", 3}};
  }
  if (name == "raw_mouse_minimum_response") {
    return {{"HID.KBM.RawMouse.Tuning", "minimum_response", 3}};
  }
  if (name == "raw_mouse_anti_deadzone_shape") {
    return {{"HID.KBM.RawMouse.Tuning", "anti_deadzone_shape", 3}};
  }
  if (name == "raw_mouse_minimum_response_x") {
    return {{"HID.KBM.RawMouse.Tuning", "minimum_response_x", 3}};
  }
  if (name == "raw_mouse_minimum_response_y") {
    return {{"HID.KBM.RawMouse.Tuning", "minimum_response_y", 3}};
  }
  if (name == "raw_mouse_processing_mode") {
    return {{"HID.KBM.RawMouse.Tuning", "processing_mode", 3}};
  }
  if (name == "raw_mouse_fixed_window_ms") {
    return {{"HID.KBM.RawMouse.Tuning", "fixed_window_ms", 3}};
  }
  if (name == "raw_mouse_ema_tau_ms") {
    return {{"HID.KBM.RawMouse.Tuning", "ema_tau_ms", 3}};
  }
  if (name == "kbm_overlay_enabled") {
    return {{"HID.KBM.Diagnostics", "show_overlay", 4}};
  }
  if (name == "kbm_overlay_corner") {
    return {{"HID.KBM.Diagnostics", "overlay_corner", 4}};
  }
  if (name == "kbm_overlay_ema_background_transparency") {
    return {{"HID.KBM.Diagnostics", "ema_background_transparency", 4}};
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
         name == "raw_mouse_invert_y" || name == "raw_mouse_capture_on_start" ||
         name == "kbm_overlay_enabled";
}

std::optional<int64_t> IntegerSettingMaximum(std::string_view name) {
  if (name == "kbm_user_index" || name == "kbm_overlay_corner") {
    return 3;
  }
  if (name == "kbm_overlay_ema_background_transparency") {
    return 100;
  }
  return std::nullopt;
}

bool IsNumberSetting(std::string_view name) {
  return name == "raw_mouse_dpi" || name == "raw_mouse_full_stick_speed_cm_s" ||
         name == "raw_mouse_minimum_response" ||
         name == "raw_mouse_minimum_response_x" ||
         name == "raw_mouse_minimum_response_y" ||
         name == "raw_mouse_fixed_window_ms" || name == "raw_mouse_ema_tau_ms";
}

bool IsOptionalSetting(std::string_view name) {
  return name == "raw_mouse_processing_mode" ||
         name == "raw_mouse_anti_deadzone_shape" ||
         name == "raw_mouse_minimum_response_x" ||
         name == "raw_mouse_minimum_response_y" ||
         name == "raw_mouse_fixed_window_ms" ||
         name == "raw_mouse_ema_tau_ms" || name.starts_with("kbm_overlay_");
}

bool IsCompatibleConfigValue(const cvar::IConfigVar& config_var,
                             const toml::node& node) {
  if (config_var.name() == "raw_mouse_anti_deadzone_shape") {
    const auto shape = node.value<std::string_view>();
    return shape && ParseAntiDeadzoneShape(*shape).has_value();
  }
  if (config_var.name() == "raw_mouse_processing_mode") {
    const auto mode = node.value<std::string_view>();
    return mode && ParseMouseMode(*mode).has_value();
  }
  if (config_var.name() == "raw_mouse_capture_toggle_key") {
    const auto binding = node.value<std::string>();
    KbmChord chord;
    return binding && (binding->empty() || (ParseKbmChord(*binding, chord) &&
                                            !IsMouseInputCode(chord.input)));
  }
  if (IsBindingConfigVar(config_var)) {
    const auto binding = node.value<std::string>();
    return binding && IsCanonicalBinding(*binding);
  }
  if (IntegerSettingMaximum(config_var.name())) {
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
    if (!node && IsOptionalSetting(name)) {
      continue;
    }
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
    if (!node) {
      config_var->ResetConfigValueToDefault();
    } else if (const auto maximum = IntegerSettingMaximum(name)) {
      // Clamp TOML integers before converting to int32.
      const toml::value<int64_t> value(
          std::clamp(*node.value<int64_t>(), int64_t(0), *maximum));
      config_var->LoadConfigValue(&value);
    } else {
      config_var->LoadConfigValue(node.node());
    }
  }
  ApplySettingsToCvars(GetSettingsFromCvars());
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

bool SaveConfig() { return SaveConfig(GetSettingsFromCvars()); }

bool CommitKbmSettings(
    KbmSettings& draft, const std::function<bool(const KbmSettings&)>& persist,
    const std::function<void(const KbmSettings&)>& activate) {
  const auto normalized = NormalizeSettings(draft);
  if (!persist(normalized)) {
    return false;
  }
  activate(normalized);
  draft = normalized;
  return true;
}

bool SaveConfig(const KbmSettings& source_settings, bool reset_incompatible) {
  const auto settings = NormalizeSettings(source_settings);
  if (config_path.empty() || !cvar::ConfigVars ||
      !CanSaveKbmConfig(config_state, reset_incompatible)) {
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

  std::error_code directory_error;
  std::filesystem::create_directories(config_path.parent_path(),
                                      directory_error);
  if (directory_error) {
    return false;
  }
  toml::table values;
  values.insert("kbm_enabled", settings.enabled);
  values.insert("kbm_user_index", settings.user_index);
#define XE_HID_KBM_BINDING(button, description, cvar_name, cvar_default_value) \
  values.insert("kbm_" #cvar_name, settings.cvar_name);
#include "xenia/hid/kbm/kbm_binding_table.inc"
#undef XE_HID_KBM_BINDING
  values.insert("raw_mouse", settings.raw_mouse);
  values.insert("raw_mouse_dpi", settings.raw_mouse_dpi);
  values.insert("raw_mouse_full_stick_speed_cm_s",
                settings.raw_mouse_full_stick_speed_cm_s);
  values.insert("raw_mouse_deadzone_compensation",
                settings.raw_mouse_deadzone_compensation);
  values.insert("raw_mouse_minimum_response",
                settings.raw_mouse_minimum_response);
  values.insert("raw_mouse_anti_deadzone_shape",
                AntiDeadzoneShapeName(settings.raw_mouse_anti_deadzone_shape));
  values.insert("raw_mouse_minimum_response_x",
                settings.raw_mouse_minimum_response_x);
  values.insert("raw_mouse_minimum_response_y",
                settings.raw_mouse_minimum_response_y);
  values.insert("raw_mouse_invert_y", settings.raw_mouse_invert_y);
  values.insert("raw_mouse_capture_toggle_key",
                settings.raw_mouse_capture_toggle_key);
  values.insert("raw_mouse_capture_on_start",
                settings.raw_mouse_capture_on_start);
  values.insert("raw_mouse_processing_mode",
                MouseModeName(settings.mouse_processing.mode));
  values.insert("raw_mouse_fixed_window_ms",
                settings.mouse_processing.fixed_window_ms);
  values.insert("raw_mouse_ema_tau_ms", settings.mouse_processing.ema_tau_ms);
  values.insert("kbm_overlay_enabled", settings.overlay.enabled);
  values.insert("kbm_overlay_corner", settings.overlay.corner);
  values.insert("kbm_overlay_ema_background_transparency",
                settings.overlay.ema_background_transparency);
  for (const auto& var : vars) {
    if (!IsCompatibleConfigValue(*var.config_var,
                                 *values.get(var.config_var->name()))) {
      XELOGE("kbm: refused to save invalid setting '{}'.",
             var.config_var->name());
      return false;
    }
  }
  std::string output =
      "# KBM Controller keyboard and Raw Input mouse settings.\n"
      "# This file is intentionally separate from xenia-canary.config.toml.\n"
      "# Settings are grouped by controller, bindings, mouse and diagnostics.\n"
      "schema_version = " +
      std::to_string(kKbmConfigSchemaVersion) + "\n";
  std::string category;
  for (const auto& var : vars) {
    if (category != var.entry.table) {
      category = var.entry.table;
      output += "\n[" + category + "]\n";
    }
    std::ostringstream value;
    value << toml::toml_formatter{*values.get(var.config_var->name()),
                                  toml::format_flags::none};
    output += std::string(var.entry.key) + " = " + value.str();
    if (!var.config_var->description().empty()) {
      output += " # " + var.config_var->description();
    }
    output += '\n';
  }

  const auto temporary_path = WriteTemporaryConfigFile(config_path, output);
  if (!temporary_path) {
    XELOGE("kbm: failed to write a temporary file for '{}'.", config_path);
    return false;
  }
  if (!MoveFileExW(temporary_path->c_str(), config_path.c_str(),
                   MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH)) {
    DeleteFileW(temporary_path->c_str());
    XELOGE("kbm: failed to replace config '{}'.", config_path);
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
  settings.raw_mouse_dpi = finite_clamp(settings.raw_mouse_dpi,
                                        defaults.raw_mouse_dpi, 100.0, 32000.0);
  settings.raw_mouse_full_stick_speed_cm_s =
      finite_clamp(settings.raw_mouse_full_stick_speed_cm_s,
                   defaults.raw_mouse_full_stick_speed_cm_s, 1.0, 100.0);
  settings.raw_mouse_minimum_response =
      finite_clamp(settings.raw_mouse_minimum_response,
                   defaults.raw_mouse_minimum_response, 0.0, 0.5);
  settings.raw_mouse_minimum_response_x =
      finite_clamp(settings.raw_mouse_minimum_response_x,
                   defaults.raw_mouse_minimum_response_x, 0.0, 0.5);
  settings.raw_mouse_minimum_response_y =
      finite_clamp(settings.raw_mouse_minimum_response_y,
                   defaults.raw_mouse_minimum_response_y, 0.0, 0.5);
  if (settings.raw_mouse_anti_deadzone_shape != KbmAntiDeadzoneShape::kAxial) {
    settings.raw_mouse_anti_deadzone_shape = KbmAntiDeadzoneShape::kRadial;
  }
  settings.mouse_processing =
      NormalizeMouseProcessing(settings.mouse_processing);
  settings.overlay.corner = std::clamp(settings.overlay.corner, 0, 3);
  settings.overlay.ema_background_transparency =
      std::clamp(settings.overlay.ema_background_transparency, 0, 100);
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
  settings.raw_mouse_dpi = cvars::raw_mouse_dpi;
  settings.raw_mouse_full_stick_speed_cm_s =
      cvars::raw_mouse_full_stick_speed_cm_s;
  settings.raw_mouse_deadzone_compensation =
      cvars::raw_mouse_deadzone_compensation;
  settings.raw_mouse_minimum_response = cvars::raw_mouse_minimum_response;
  settings.raw_mouse_anti_deadzone_shape =
      ParseAntiDeadzoneShape(cvars::raw_mouse_anti_deadzone_shape)
          .value_or(KbmAntiDeadzoneShape::kRadial);
  settings.raw_mouse_minimum_response_x = cvars::raw_mouse_minimum_response_x;
  settings.raw_mouse_minimum_response_y = cvars::raw_mouse_minimum_response_y;
  settings.raw_mouse_invert_y = cvars::raw_mouse_invert_y;
  settings.raw_mouse_capture_toggle_key = cvars::raw_mouse_capture_toggle_key;
  settings.raw_mouse_capture_on_start = cvars::raw_mouse_capture_on_start;
  settings.mouse_processing = {ParseMouseMode(cvars::raw_mouse_processing_mode)
                                   .value_or(kDefaultMouseMode),
                               cvars::raw_mouse_fixed_window_ms,
                               cvars::raw_mouse_ema_tau_ms};
  settings.overlay = {cvars::kbm_overlay_enabled, cvars::kbm_overlay_corner,
                      cvars::kbm_overlay_ema_background_transparency};
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
  OVERRIDE_double(raw_mouse_dpi, settings.raw_mouse_dpi);
  OVERRIDE_double(raw_mouse_full_stick_speed_cm_s,
                  settings.raw_mouse_full_stick_speed_cm_s);
  OVERRIDE_bool(raw_mouse_deadzone_compensation,
                settings.raw_mouse_deadzone_compensation);
  OVERRIDE_double(raw_mouse_minimum_response,
                  std::clamp(settings.raw_mouse_minimum_response, 0.0, 0.5));
  OVERRIDE_string(raw_mouse_anti_deadzone_shape,
                  std::string(AntiDeadzoneShapeName(
                      settings.raw_mouse_anti_deadzone_shape)));
  OVERRIDE_double(raw_mouse_minimum_response_x,
                  settings.raw_mouse_minimum_response_x);
  OVERRIDE_double(raw_mouse_minimum_response_y,
                  settings.raw_mouse_minimum_response_y);
  OVERRIDE_bool(raw_mouse_invert_y, settings.raw_mouse_invert_y);
  OVERRIDE_string(raw_mouse_capture_toggle_key,
                  settings.raw_mouse_capture_toggle_key);
  OVERRIDE_string(raw_mouse_processing_mode,
                  std::string(MouseModeName(settings.mouse_processing.mode)));
  OVERRIDE_double(raw_mouse_fixed_window_ms,
                  settings.mouse_processing.fixed_window_ms);
  OVERRIDE_double(raw_mouse_ema_tau_ms, settings.mouse_processing.ema_tau_ms);
  OVERRIDE_bool(raw_mouse_capture_on_start,
                settings.raw_mouse_capture_on_start);
  OVERRIDE_bool(kbm_overlay_enabled, settings.overlay.enabled);
  OVERRIDE_int32(kbm_overlay_corner, settings.overlay.corner);
  OVERRIDE_int32(kbm_overlay_ema_background_transparency,
                 settings.overlay.ema_background_transparency);
}

}  // namespace xe::hid::kbm
