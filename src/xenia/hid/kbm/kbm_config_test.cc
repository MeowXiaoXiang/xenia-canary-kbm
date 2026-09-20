/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2026 Xenia authors. All rights reserved.                         *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#include "xenia/hid/kbm/kbm_config.h"

#include <fstream>

#include "third_party/catch/single_include/catch2/catch.hpp"

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
              "xenia-hid-kbm-config-test") {
    std::error_code error;
    std::filesystem::remove_all(path_, error);
    std::filesystem::create_directory(path_, error);
    REQUIRE_FALSE(error);
  }

  ~ScopedConfigDirectory() {
    std::error_code error;
    std::filesystem::remove_all(path_, error);
  }

  const std::filesystem::path& path() const { return path_; }

 private:
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

}  // namespace xe::hid::kbm
