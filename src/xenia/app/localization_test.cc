/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2026 Xenia authors. All rights reserved.                          *
 * Released under the BSD license - see LICENSE for more details.              *
 ******************************************************************************/

#define CATCH_CONFIG_MAIN
#include "third_party/catch/single_include/catch2/catch.hpp"

#include <string_view>

#include "xenia/app/localization.h"
#include "xenia/base/cvar.h"

DECLARE_string(ui_language);

namespace config {
void SaveConfig() {}
}  // namespace config

namespace xe::app::localization {
namespace {

TEST_CASE("Built-in localization catalogs are available", "[localization]") {
  const auto languages = GetAvailableLanguages();
  REQUIRE(languages.size() == 2);
  REQUIRE(languages[0].language == Language::kEnglish);
  REQUIRE(std::string_view(languages[0].code) == "en");
  REQUIRE(languages[1].language == Language::kTraditionalChinese);
  REQUIRE(std::string_view(languages[1].code) == "zh-TW");
  REQUIRE(std::string_view(
              GetLanguageInfo(Language::kTraditionalChinese).native_name) ==
          "繁體中文");
}

TEST_CASE("Language aliases and fallback are stable", "[localization]") {
  for (const char* alias : {"zh-TW", "zh-tw", "zh_TW", "zh_tw"}) {
    cvars::ui_language = alias;
    REQUIRE(GetLanguage() == Language::kTraditionalChinese);
    REQUIRE(std::string_view(Get(StringId::kKbmTitle)) == "KBM 控制器設定");
  }

  SetLanguage(Language::kTraditionalChinese, false);
  REQUIRE(cvars::ui_language == "zh-TW");

  cvars::ui_language = "unsupported";
  REQUIRE(GetLanguage() == Language::kEnglish);
  REQUIRE(std::string_view(Get(StringId::kKbmTitle)) ==
          "KBM Controller Settings");

  cvars::ui_language = "en";
}

TEST_CASE("Localization strings remain addressable", "[localization]") {
  cvars::ui_language = "en";
  REQUIRE(std::string_view(Get(StringId::kMenuFile)) == "&File");
  REQUIRE(std::string_view(Get(StringId::kLanguageActiveSuffix)) ==
          " (Active)");

  cvars::ui_language = "zh-TW";
  REQUIRE(std::string_view(Get(StringId::kMenuFile)) == "檔案(&F)");
  REQUIRE(std::string_view(Get(StringId::kLanguageActiveSuffix)) ==
          "（目前使用）");

  cvars::ui_language = "en";
}

}  // namespace
}  // namespace xe::app::localization
