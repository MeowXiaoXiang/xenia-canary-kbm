/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2026 Xenia Canary. All rights reserved.                          *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#ifndef XENIA_APP_LOCALIZATION_H_
#define XENIA_APP_LOCALIZATION_H_

#include <cstddef>
#include <cstdint>
#include <span>

namespace xe::app::localization {

enum class Language : uint8_t {
#define XE_LOCALIZATION_LANGUAGE(id, code, aliases, native_name, catalog) id,
#include "xenia/app/localization/languages.inc"
#undef XE_LOCALIZATION_LANGUAGE
  kCount,
};

struct LanguageInfo {
  Language language;
  const char* code;
  // Semicolon-separated legacy aliases accepted when reading configuration.
  const char* aliases;
  const char* native_name;
};

// Stable identifiers keep translations independent from the English wording.
enum class StringId : uint16_t {
#define XE_LOCALIZATION_STRING_ID(id) id,
#include "xenia/app/localization/string_ids.inc"
#undef XE_LOCALIZATION_STRING_ID
  kCount,
};

Language GetLanguage();
bool IsTraditionalChinese();
void SetLanguage(Language language, bool save_config = true);
std::span<const LanguageInfo> GetAvailableLanguages();
const LanguageInfo& GetLanguageInfo(Language language);
const char* Get(StringId id);

}  // namespace xe::app::localization

#endif  // XENIA_APP_LOCALIZATION_H_
