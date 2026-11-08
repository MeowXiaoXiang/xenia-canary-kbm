/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2026 Xenia authors. All rights reserved.                         *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#include "xenia/hid/kbm/kbm_config.h"

#include <chrono>
#include <fstream>
#include <limits>

#include "third_party/catch/single_include/catch2/catch.hpp"
#include "xenia/base/platform_win.h"

namespace xe::hid::kbm {
namespace {

std::string ReadFile(const std::filesystem::path& path) {
  std::ifstream file(path, std::ios::binary);
  return {std::istreambuf_iterator<char>(file), {}};
}

class ScopedConfigDirectory {
 public:
  ScopedConfigDirectory()
      : path_(std::filesystem::temp_directory_path() /
              ("xenia-hid-kbm-config-test-" +
               std::to_string(GetCurrentProcessId()) + "-" +
               std::to_string(std::chrono::steady_clock::now()
                                  .time_since_epoch()
                                  .count()))) {
    std::error_code error;
    REQUIRE(std::filesystem::create_directory(path_, error));
    REQUIRE_FALSE(error);
    ApplySettingsToCvars(KbmSettings());
  }

  ~ScopedConfigDirectory() {
    ApplySettingsToCvars(original_settings_);
    std::error_code error;
    std::filesystem::remove_all(path_, error);
  }

  const std::filesystem::path& path() const { return path_; }

 private:
  KbmSettings original_settings_ = GetSettingsFromCvars();
  std::filesystem::path path_;
};

}  // namespace

TEST_CASE("KBM settings persist in the grouped v4 configuration", "[kbm]") {
  ScopedConfigDirectory directory;
  SetupConfig(directory.path(), false);

  KbmSettings saved_settings;
  saved_settings.enabled = false;
  saved_settings.user_index = 3;
  saved_settings.keybind_a = "Key.G";
  saved_settings.keybind_left_trigger = "Mouse.X1";
  saved_settings.raw_mouse_dpi = 1325.0;
  saved_settings.raw_mouse_deadzone_compensation = true;
  saved_settings.raw_mouse_capture_toggle_key = "Ctrl+Key.F8";
  ApplySettingsToCvars(saved_settings);

  REQUIRE(SaveConfig());
  const std::string contents = ReadFile(ConfigPath());
  REQUIRE(contents.find("[HID.KBM.Controller]") != std::string::npos);
  REQUIRE(contents.find("[HID.KBM.Bindings]") != std::string::npos);
  REQUIRE(contents.find("[HID.KBM.RawMouse]") != std::string::npos);
  REQUIRE(contents.find("[HID.KBM.RawMouse.Tuning]") != std::string::npos);
  REQUIRE(contents.find("a = \"Key.G\"") != std::string::npos);
  REQUIRE(contents.find("dpi = 1325.0") != std::string::npos);

  ApplySettingsToCvars(KbmSettings());
  SetupConfig(directory.path(), false);
  REQUIRE(GetConfigState() == KbmConfigState::kCompatible);
  const KbmSettings loaded_settings = GetSettingsFromCvars();
  REQUIRE_FALSE(loaded_settings.enabled);
  REQUIRE(loaded_settings.user_index == 3);
  REQUIRE(loaded_settings.keybind_a == "Key.G");
  REQUIRE(loaded_settings.keybind_left_trigger == "Mouse.X1");
  REQUIRE(loaded_settings.raw_mouse_dpi == Approx(1325.0));
  REQUIRE(loaded_settings.raw_mouse_deadzone_compensation);
  REQUIRE(loaded_settings.raw_mouse_capture_toggle_key == "Ctrl+Key.F8");
}

TEST_CASE("Flat v4 KBM configurations are rejected", "[kbm]") {
  ScopedConfigDirectory directory;
  const auto path = directory.path() / "kbm.toml";
  std::ofstream file(path, std::ios::binary);
  file << "schema_version = 4\n[HID.KBM]\nkbm_enabled = false\n";
  file.close();

  SetupConfig(directory.path(), false);
  REQUIRE(GetConfigState() == KbmConfigState::kIncompatible);
}

TEST_CASE("Invalid KBM files never partially replace active settings",
          "[kbm]") {
  ScopedConfigDirectory directory;
  SetupConfig(directory.path(), false);
  KbmSettings disk_settings;
  disk_settings.enabled = false;
  disk_settings.user_index = 3;
  disk_settings.keybind_a = "Key.G";
  disk_settings.raw_mouse_dpi = 1325.0;
  ApplySettingsToCvars(disk_settings);
  REQUIRE(SaveConfig());
  std::string contents = ReadFile(ConfigPath());

  std::string from, to;
  SECTION("Invalid binding token") {
    from = "a = \"Key.G\"";
    to = "a = \"VK_G\"";
  }
  SECTION("Wrong controller value type") {
    from = "user_index = 3";
    to = "user_index = true";
  }
  SECTION("Wrong mouse value type") {
    from = "dpi = 1325.0";
    to = "dpi = \"fast\"";
  }
  SECTION("Malformed TOML") {
    from = "schema_version = 4";
    to = "schema_version = [";
  }
  const auto offset = contents.find(from);
  REQUIRE(offset != std::string::npos);
  contents.replace(offset, from.size(), to);
  {
    std::ofstream file(ConfigPath(), std::ios::binary | std::ios::trunc);
    file << contents;
    REQUIRE(file.good());
  }

  KbmSettings active;
  active.user_index = 1;
  active.keybind_a = "Key.H";
  active.raw_mouse_dpi = 750.0;
  ApplySettingsToCvars(active);
  SetupConfig(directory.path(), false);
  REQUIRE(GetConfigState() == KbmConfigState::kIncompatible);
  const auto retained = GetSettingsFromCvars();
  REQUIRE(retained.enabled == active.enabled);
  REQUIRE(retained.user_index == active.user_index);
  REQUIRE(retained.keybind_a == active.keybind_a);
  REQUIRE(retained.raw_mouse_dpi == active.raw_mouse_dpi);
  REQUIRE(ReadFile(ConfigPath()) == contents);
}

TEST_CASE("Failed KBM replacement preserves the previous saved file", "[kbm]") {
  ScopedConfigDirectory directory;
  SetupConfig(directory.path(), false);
  REQUIRE(SaveConfig());
  const std::string saved = ReadFile(ConfigPath());
  // Permit reads but deny replacement, exercising failure after writing .tmp.
  const HANDLE held =
      CreateFileW(ConfigPath().c_str(), GENERIC_READ, FILE_SHARE_READ, nullptr,
                  OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
  REQUIRE(held != INVALID_HANDLE_VALUE);
  auto changed = GetSettingsFromCvars();
  changed.keybind_a = "Key.G";
  ApplySettingsToCvars(changed);
  const bool written = SaveConfig();
  CloseHandle(held);
  REQUIRE_FALSE(written);
  REQUIRE(ReadFile(ConfigPath()) == saved);
  auto temporary = ConfigPath();
  temporary += ".tmp";
  REQUIRE_FALSE(std::filesystem::exists(temporary));
}

TEST_CASE("Non-finite KBM tuning values normalize to safe defaults", "[kbm]") {
  KbmSettings settings;
  settings.raw_mouse_dpi = std::numeric_limits<double>::quiet_NaN();
  settings.raw_mouse_full_stick_speed_cm_s =
      std::numeric_limits<double>::infinity();
  const auto normalized = NormalizeSettings(settings);
  const KbmSettings defaults;
  REQUIRE(normalized.raw_mouse_dpi == defaults.raw_mouse_dpi);
  REQUIRE(normalized.raw_mouse_full_stick_speed_cm_s ==
          defaults.raw_mouse_full_stick_speed_cm_s);
}

namespace {
constexpr auto kLegacyV3 = R"toml(schema_version = 3
[HID.KBM.Controller]
enabled = true
user_index = 2
[HID.KBM.Bindings]
guide = ""
dpad_left = "Key.Digit1"
dpad_right = "Key.Digit2"
dpad_down = "Key.Digit3"
dpad_up = "Key.Digit4"
left_thumb_left = "Key.A"
left_thumb_right = "Key.D"
left_thumb_down = "Key.S"
left_thumb_up = "Key.W"
left_thumb = "Key.C"
right_thumb_up = "Key.ArrowUp"
right_thumb_down = "Key.ArrowDown"
right_thumb_right = "Key.ArrowRight"
right_thumb_left = "Key.ArrowLeft"
right_thumb = "Mouse.Middle"
x = "Key.E"
b = "Key.F"
a = "Ctrl+Key.G"
y = "Key.Q"
left_trigger = "Mouse.Right"
right_trigger = "Mouse.Left"
back = "Key.Tab"
start = "Key.Escape"
left_shoulder = "Key.Shift"
right_shoulder = "Key.R"
[HID.KBM.RawMouse]
enabled = true
capture_on_start = false
capture_toggle_key = "Alt+Key.F9"
deadzone_compensation = true
invert_y = true
[HID.KBM.RawMouse.Tuning]
sensitivity = 17.25
full_scale_velocity = 37500.0
response_curve = 1.7
smoothing_time_ms = 12.0
minimum_response = 0.23
)toml";
}

TEST_CASE("Grouped v3 is rejected until explicit reset and save", "[kbm]") {
  ScopedConfigDirectory directory;
  const auto path = directory.path() / "kbm.toml";
  {
    std::ofstream file(path, std::ios::binary);
    file << kLegacyV3;
  }
  SetupConfig(directory.path(), true);
  REQUIRE(GetConfigState() == KbmConfigState::kIncompatible);
  REQUIRE(ReadFile(path) == kLegacyV3);
  const auto active = GetSettingsFromCvars();
  const KbmSettings defaults;
  REQUIRE(active.user_index == defaults.user_index);
  REQUIRE(active.raw_mouse_invert_y == defaults.raw_mouse_invert_y);
  REQUIRE(active.raw_mouse_capture_toggle_key ==
          defaults.raw_mouse_capture_toggle_key);
#define XE_HID_KBM_BINDING(button, description, cvar_name, cvar_default_value) \
  REQUIRE(active.cvar_name == defaults.cvar_name);
#include "xenia/hid/kbm/kbm_binding_table.inc"
#undef XE_HID_KBM_BINDING
  REQUIRE_FALSE(CanSaveKbmConfig(GetConfigState(), false));
  REQUIRE_FALSE(SaveConfig());
  REQUIRE_FALSE(SaveConfig(defaults));
  REQUIRE(ReadFile(path) == kLegacyV3);
  auto draft = defaults;
  bool activated = false;
  REQUIRE_FALSE(CommitKbmSettings(
      draft, [](const auto& value) { return SaveConfig(value); },
      [&](const auto&) { activated = true; }));
  REQUIRE_FALSE(activated);
  REQUIRE(CanSaveKbmConfig(GetConfigState(), true));
  REQUIRE(CommitKbmSettings(
      draft, [](const auto& value) { return SaveConfig(value, true); },
      [&](const auto& value) {
        activated = true;
        ApplySettingsToCvars(value);
      }));
  REQUIRE(activated);
  REQUIRE(GetConfigState() == KbmConfigState::kCompatible);
  REQUIRE(ReadFile(path).find("schema_version = 4") != std::string::npos);
  REQUIRE_FALSE(std::filesystem::exists(directory.path() / "kbm.toml.v3.bak"));
  SetupConfig(directory.path(), false);
  REQUIRE(GetSettingsFromCvars().keybind_a == defaults.keybind_a);
}

TEST_CASE("Draft commit activates only after successful publication", "[kbm]") {
  KbmSettings draft;
  draft.raw_mouse_dpi = 1600;
  bool published = false, activated = false;
  auto activate = [&](const auto& settings) {
    REQUIRE(published);
    REQUIRE(settings.raw_mouse_dpi == 1600);
    activated = true;
  };
  REQUIRE_FALSE(
      CommitKbmSettings(draft, [](const auto&) { return false; }, activate));
  REQUIRE_FALSE(activated);
  REQUIRE(draft.raw_mouse_dpi == 1600);
  REQUIRE(CommitKbmSettings(
      draft,
      [&](const auto&) {
        published = true;
        return true;
      },
      activate));
  REQUIRE(activated);
}

TEST_CASE("Physical settings clamp finite values and default non-finite values",
          "[kbm]") {
  KbmSettings settings;
  settings.raw_mouse_dpi = -1;
  settings.raw_mouse_full_stick_speed_cm_s = 0;
  auto normalized = NormalizeSettings(settings);
  REQUIRE(normalized.raw_mouse_dpi == 100);
  REQUIRE(normalized.raw_mouse_full_stick_speed_cm_s == 1);
  settings.raw_mouse_dpi = 100000;
  settings.raw_mouse_full_stick_speed_cm_s = 1000;
  normalized = NormalizeSettings(settings);
  REQUIRE(normalized.raw_mouse_dpi == 32000);
  REQUIRE(normalized.raw_mouse_full_stick_speed_cm_s == 100);
}

TEST_CASE("Failed reset publication preserves the incompatible original",
          "[kbm]") {
  ScopedConfigDirectory directory;
  const auto path = directory.path() / "kbm.toml";
  {
    std::ofstream file(path, std::ios::binary);
    file << kLegacyV3;
  }
  SetupConfig(directory.path(), false);
  const auto temporary = directory.path() / "kbm.toml.tmp";
  {
    std::ofstream file(temporary);
    file << "another writer";
  }
  KbmSettings draft;
  bool activated = false;
  REQUIRE_FALSE(CommitKbmSettings(
      draft, [](const auto& value) { return SaveConfig(value, true); },
      [&](const auto&) { activated = true; }));
  REQUIRE_FALSE(activated);
  REQUIRE(ReadFile(path) == kLegacyV3);
  REQUIRE(ReadFile(temporary) == "another writer");
  REQUIRE(GetConfigState() == KbmConfigState::kIncompatible);
  REQUIRE_FALSE(CanSaveKbmConfig(GetConfigState(), false));
  REQUIRE_FALSE(std::filesystem::exists(directory.path() / "kbm.toml.v3.bak"));
}

TEST_CASE("Malformed v3 values and unknown versions never partially load",
          "[kbm]") {
  ScopedConfigDirectory directory;
  std::string contents = kLegacyV3;
  SECTION("Invalid compatible binding") {
    auto pos = contents.find("Ctrl+Key.G");
    contents.replace(pos, 10, "VK_G");
  }
  SECTION("Unknown schema") {
    auto pos = contents.find("schema_version = 3");
    contents.replace(pos, 18, "schema_version = 999");
  }
  {
    std::ofstream file(directory.path() / "kbm.toml", std::ios::binary);
    file << contents;
  }
  SetupConfig(directory.path(), false);
  REQUIRE(GetConfigState() == KbmConfigState::kIncompatible);
  REQUIRE(GetSettingsFromCvars().user_index == 0);
  REQUIRE(GetSettingsFromCvars().keybind_a == KbmSettings().keybind_a);
}

TEST_CASE("Anti-deadzone defaults on without overriding explicit saved choices",
          "[kbm]") {
  ScopedConfigDirectory directory;
  REQUIRE(KbmSettings().raw_mouse_deadzone_compensation);
  REQUIRE(KbmSettings().raw_mouse_minimum_response == Approx(0.30));
  SECTION("Explicit v4 off remains off") {
    SetupConfig(directory.path(), false);
    auto settings = KbmSettings();
    settings.raw_mouse_deadzone_compensation = false;
    REQUIRE(SaveConfig(settings));
  }
  SetupConfig(directory.path(), false);
  REQUIRE_FALSE(GetSettingsFromCvars().raw_mouse_deadzone_compensation);
}

}  // namespace xe::hid::kbm
