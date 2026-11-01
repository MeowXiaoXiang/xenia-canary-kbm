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

TEST_CASE("KBM settings persist in the grouped v3 configuration", "[kbm]") {
  ScopedConfigDirectory directory;
  SetupConfig(directory.path(), false);

  KbmSettings saved_settings;
  saved_settings.enabled = false;
  saved_settings.user_index = 3;
  saved_settings.keybind_a = "Key.G";
  saved_settings.keybind_left_trigger = "Mouse.X1";
  saved_settings.raw_mouse_sensitivity = 13.25;
  saved_settings.raw_mouse_response_curve = 1.1;
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
  REQUIRE(contents.find("sensitivity = 13.25") != std::string::npos);

  ApplySettingsToCvars(KbmSettings());
  SetupConfig(directory.path(), false);
  REQUIRE(GetConfigState() == KbmConfigState::kCompatible);
  const KbmSettings loaded_settings = GetSettingsFromCvars();
  REQUIRE_FALSE(loaded_settings.enabled);
  REQUIRE(loaded_settings.user_index == 3);
  REQUIRE(loaded_settings.keybind_a == "Key.G");
  REQUIRE(loaded_settings.keybind_left_trigger == "Mouse.X1");
  REQUIRE(loaded_settings.raw_mouse_sensitivity == Approx(13.25));
  REQUIRE(loaded_settings.raw_mouse_response_curve == Approx(1.1));
  REQUIRE(loaded_settings.raw_mouse_deadzone_compensation);
  REQUIRE(loaded_settings.raw_mouse_capture_toggle_key == "Ctrl+Key.F8");
}

TEST_CASE("Flat v3 KBM configurations are rejected", "[kbm]") {
  ScopedConfigDirectory directory;
  const auto path = directory.path() / "kbm.toml";
  std::ofstream file(path, std::ios::binary);
  file << "schema_version = 3\n[HID.KBM]\nkbm_enabled = false\n";
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
  disk_settings.raw_mouse_sensitivity = 13.25;
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
    from = "sensitivity = 13.25";
    to = "sensitivity = \"fast\"";
  }
  SECTION("Malformed TOML") {
    from = "schema_version = 3";
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
  active.raw_mouse_sensitivity = 7.5;
  ApplySettingsToCvars(active);
  SetupConfig(directory.path(), false);
  REQUIRE(GetConfigState() == KbmConfigState::kIncompatible);
  const auto retained = GetSettingsFromCvars();
  REQUIRE(retained.enabled == active.enabled);
  REQUIRE(retained.user_index == active.user_index);
  REQUIRE(retained.keybind_a == active.keybind_a);
  REQUIRE(retained.raw_mouse_sensitivity == active.raw_mouse_sensitivity);
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
  settings.raw_mouse_sensitivity = std::numeric_limits<double>::quiet_NaN();
  settings.raw_mouse_full_scale_velocity =
      std::numeric_limits<double>::infinity();
  settings.raw_mouse_response_curve = -std::numeric_limits<double>::infinity();
  const auto normalized = NormalizeSettings(settings);
  const KbmSettings defaults;
  REQUIRE(normalized.raw_mouse_sensitivity == defaults.raw_mouse_sensitivity);
  REQUIRE(normalized.raw_mouse_full_scale_velocity ==
          defaults.raw_mouse_full_scale_velocity);
  REQUIRE(normalized.raw_mouse_response_curve ==
          defaults.raw_mouse_response_curve);
}

}  // namespace xe::hid::kbm
