/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2026 Xenia Canary. All rights reserved.                          *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#include "xenia/app/localization.h"

#include <array>
#include <cctype>
#include <cstddef>
#include <string>
#include <string_view>

#include "xenia/base/cvar.h"
#include "xenia/config.h"

DEFINE_string(ui_language, "en",
              "Host interface language. Use: [en, zh-TW]. This does not "
              "change the emulated Xbox 360 language.",
              "General");

namespace xe::app::localization {
namespace {

constexpr size_t kStringCount = static_cast<size_t>(StringId::kCount);

struct Catalog {
  std::array<std::string_view, kStringCount> values{};
  std::array<bool, kStringCount> defined{};
  size_t entry_count = 0;
};

constexpr Catalog MakeEnglishCatalog() {
  Catalog catalog;
#define XE_LOCALIZATION_STRING(id, text)                        \
  do {                                                          \
    constexpr size_t index = static_cast<size_t>(StringId::id); \
    catalog.values[index] = text;                               \
    catalog.defined[index] = true;                              \
    ++catalog.entry_count;                                      \
  } while (false);
#include "xenia/app/localization/en.inc"
#undef XE_LOCALIZATION_STRING
  return catalog;
}

constexpr Catalog MakeTraditionalChineseCatalog() {
  Catalog catalog;
#define XE_LOCALIZATION_STRING(id, text)                        \
  do {                                                          \
    constexpr size_t index = static_cast<size_t>(StringId::id); \
    catalog.values[index] = text;                               \
    catalog.defined[index] = true;                              \
    ++catalog.entry_count;                                      \
  } while (false);
#include "xenia/app/localization/zh_tw.inc"
#undef XE_LOCALIZATION_STRING
  return catalog;
}

constexpr bool IsCompleteCatalog(const Catalog& catalog) {
  if (catalog.entry_count != kStringCount) {
    return false;
  }
  for (size_t i = 0; i < kStringCount; ++i) {
    if (!catalog.defined[i] || catalog.values[i].empty()) {
      return false;
    }
  }
  return true;
}

constexpr Catalog kEnglishCatalog = MakeEnglishCatalog();
constexpr Catalog kTraditionalChineseCatalog = MakeTraditionalChineseCatalog();
static_assert(IsCompleteCatalog(kEnglishCatalog),
              "English localization catalog is incomplete or duplicated.");
static_assert(
    IsCompleteCatalog(kTraditionalChineseCatalog),
    "Traditional Chinese localization catalog is incomplete or duplicated.");

#define XE_LOCALIZATION_LANGUAGE(id, code, aliases, native_name, catalog) \
  LanguageInfo{Language::id, code, aliases, native_name},
constexpr std::array<LanguageInfo, static_cast<size_t>(Language::kCount)>
    kLanguageInfos = {{
#include "xenia/app/localization/languages.inc"
    }};
#undef XE_LOCALIZATION_LANGUAGE

#define XE_LOCALIZATION_LANGUAGE(id, code, aliases, native_name, catalog) \
  &catalog,
constexpr std::array<const Catalog*, static_cast<size_t>(Language::kCount)>
    kLanguageCatalogs = {{
#include "xenia/app/localization/languages.inc"
    }};
#undef XE_LOCALIZATION_LANGUAGE

std::string NormalizeLanguageCode(std::string_view code) {
  std::string normalized;
  normalized.reserve(code.size());
  for (const unsigned char character : code) {
    if (character == '_') {
      normalized.push_back('-');
    } else {
      normalized.push_back(static_cast<char>(
          std::tolower(static_cast<unsigned char>(character))));
    }
  }
  return normalized;
}

bool MatchesLanguageCode(const LanguageInfo& language, std::string_view code) {
  const std::string normalized = NormalizeLanguageCode(code);
  if (NormalizeLanguageCode(language.code) == normalized) {
    return true;
  }

  std::string_view aliases = language.aliases;
  while (!aliases.empty()) {
    const size_t separator = aliases.find(';');
    const std::string_view alias = aliases.substr(0, separator);
    if (!alias.empty() && NormalizeLanguageCode(alias) == normalized) {
      return true;
    }
    if (separator == std::string_view::npos) {
      break;
    }
    aliases.remove_prefix(separator + 1);
  }
  return false;
}

const LanguageInfo* FindLanguage(std::string_view code) {
  for (const auto& language : kLanguageInfos) {
    if (MatchesLanguageCode(language, code)) {
      return &language;
    }
  }
  return nullptr;
}

const LanguageInfo& GetEnglishLanguage() {
  return kLanguageInfos[static_cast<size_t>(Language::kEnglish)];
}

}  // namespace

Language GetLanguage() {
  const LanguageInfo* language = FindLanguage(cvars::ui_language);
  return language ? language->language : Language::kEnglish;
}

bool IsTraditionalChinese() {
  return GetLanguage() == Language::kTraditionalChinese;
}

void SetLanguage(Language language, bool save_config) {
  const char* value = GetLanguageInfo(language).code;
  if (cvars::ui_language != value) {
    OVERRIDE_string(ui_language, value);
  }
  if (save_config) {
    config::SaveConfig();
  }
}

std::span<const LanguageInfo> GetAvailableLanguages() {
  return std::span<const LanguageInfo>(kLanguageInfos);
}

const LanguageInfo& GetLanguageInfo(Language language) {
  const size_t language_index = static_cast<size_t>(language);
  if (language_index < kLanguageInfos.size()) {
    return kLanguageInfos[language_index];
  }
  return GetEnglishLanguage();
}

const char* Get(StringId id) {
  const size_t index = static_cast<size_t>(id);
  if (index >= kStringCount) {
    return "";
  }
  const size_t language_index = static_cast<size_t>(GetLanguage());
  if (language_index >= kLanguageCatalogs.size()) {
    return "";
  }
  return kLanguageCatalogs[language_index]->values[index].data();
}

}  // namespace xe::app::localization
