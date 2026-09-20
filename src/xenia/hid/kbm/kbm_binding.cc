/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2026 Xenia authors. All rights reserved.                         *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#include "xenia/hid/kbm/kbm_binding.h"

#include <array>
#include <cctype>

#include "third_party/fmt/include/fmt/format.h"
#include "xenia/base/string.h"

namespace xe::hid::kbm {
namespace {

struct NamedInputCode {
  std::string_view name;
  std::string_view display_name;
  KbmInputCode code;
};

constexpr std::array kNamedInputCodes = {
    NamedInputCode{"Mouse.Left", "Mouse Left", KbmInputCode::kMouseLeft},
    NamedInputCode{"Mouse.Right", "Mouse Right", KbmInputCode::kMouseRight},
    NamedInputCode{"Mouse.Middle", "Mouse Middle", KbmInputCode::kMouseMiddle},
    NamedInputCode{"Mouse.X1", "Mouse X1", KbmInputCode::kMouseX1},
    NamedInputCode{"Mouse.X2", "Mouse X2", KbmInputCode::kMouseX2},
    NamedInputCode{"Key.Backspace", "Backspace", KbmInputCode::kBackspace},
    NamedInputCode{"Key.Tab", "Tab", KbmInputCode::kTab},
    NamedInputCode{"Key.Enter", "Enter", KbmInputCode::kEnter},
    NamedInputCode{"Key.Shift", "Shift", KbmInputCode::kShift},
    NamedInputCode{"Key.LeftShift", "Left Shift", KbmInputCode::kLeftShift},
    NamedInputCode{"Key.RightShift", "Right Shift", KbmInputCode::kRightShift},
    NamedInputCode{"Key.Ctrl", "Ctrl", KbmInputCode::kCtrl},
    NamedInputCode{"Key.LeftCtrl", "Left Ctrl", KbmInputCode::kLeftCtrl},
    NamedInputCode{"Key.RightCtrl", "Right Ctrl", KbmInputCode::kRightCtrl},
    NamedInputCode{"Key.Alt", "Alt", KbmInputCode::kAlt},
    NamedInputCode{"Key.LeftAlt", "Left Alt", KbmInputCode::kLeftAlt},
    NamedInputCode{"Key.RightAlt", "Right Alt", KbmInputCode::kRightAlt},
    NamedInputCode{"Key.Pause", "Pause", KbmInputCode::kPause},
    NamedInputCode{"Key.CapsLock", "Caps Lock", KbmInputCode::kCapsLock},
    NamedInputCode{"Key.Escape", "Esc", KbmInputCode::kEscape},
    NamedInputCode{"Key.Space", "Space", KbmInputCode::kSpace},
    NamedInputCode{"Key.PageUp", "Page Up", KbmInputCode::kPageUp},
    NamedInputCode{"Key.PageDown", "Page Down", KbmInputCode::kPageDown},
    NamedInputCode{"Key.End", "End", KbmInputCode::kEnd},
    NamedInputCode{"Key.Home", "Home", KbmInputCode::kHome},
    NamedInputCode{"Key.ArrowLeft", "Left", KbmInputCode::kArrowLeft},
    NamedInputCode{"Key.ArrowUp", "Up", KbmInputCode::kArrowUp},
    NamedInputCode{"Key.ArrowRight", "Right", KbmInputCode::kArrowRight},
    NamedInputCode{"Key.ArrowDown", "Down", KbmInputCode::kArrowDown},
    NamedInputCode{"Key.Insert", "Insert", KbmInputCode::kInsert},
    NamedInputCode{"Key.Delete", "Delete", KbmInputCode::kDelete},
    NamedInputCode{"Key.Super", "Super", KbmInputCode::kSuper},
    NamedInputCode{"Key.LeftSuper", "Left Super", KbmInputCode::kLeftSuper},
    NamedInputCode{"Key.RightSuper", "Right Super", KbmInputCode::kRightSuper},
    NamedInputCode{"Key.Menu", "Menu", KbmInputCode::kMenu},
    NamedInputCode{"Key.NumLock", "Num Lock", KbmInputCode::kNumLock},
    NamedInputCode{"Key.NumpadMultiply", "Numpad *",
                   KbmInputCode::kNumpadMultiply},
    NamedInputCode{"Key.NumpadAdd", "Numpad +", KbmInputCode::kNumpadAdd},
    NamedInputCode{"Key.NumpadSubtract", "Numpad -",
                   KbmInputCode::kNumpadSubtract},
    NamedInputCode{"Key.NumpadDecimal", "Numpad .",
                   KbmInputCode::kNumpadDecimal},
    NamedInputCode{"Key.NumpadDivide", "Numpad /", KbmInputCode::kNumpadDivide},
};

bool IsCodeInRange(KbmInputCode code, KbmInputCode first, size_t count) {
  const auto value = static_cast<uint16_t>(code);
  const auto first_value = static_cast<uint16_t>(first);
  return value >= first_value && value < first_value + count;
}

bool StartsWith(std::string_view text, std::string_view prefix) {
  return text.size() >= prefix.size() &&
         text.substr(0, prefix.size()) == prefix;
}

std::string Normalize(std::string_view text) {
  std::string result;
  result.reserve(text.size());
  for (char character : text) {
    if (character != '.' && character != '-' && character != '_' &&
        !std::isspace(static_cast<unsigned char>(character))) {
      result.push_back(static_cast<char>(
          std::tolower(static_cast<unsigned char>(character))));
    }
  }
  return result;
}

bool ParseModifier(std::string_view text, KbmChord& chord) {
  const std::string normalized = Normalize(text);
  if (normalized == "ctrl" || normalized == "control") {
    chord.ctrl = true;
  } else if (normalized == "alt") {
    chord.alt = true;
  } else if (normalized == "shift") {
    chord.shift = true;
  } else if (normalized == "super" || normalized == "win" ||
             normalized == "windows") {
    chord.super = true;
  } else {
    return false;
  }
  return true;
}

}  // namespace

bool IsMouseInputCode(KbmInputCode code) {
  return IsCodeInRange(code, KbmInputCode::kMouseLeft, 5);
}

bool IsModifierInputCode(KbmInputCode code) {
  return code == KbmInputCode::kShift || code == KbmInputCode::kLeftShift ||
         code == KbmInputCode::kRightShift || code == KbmInputCode::kCtrl ||
         code == KbmInputCode::kLeftCtrl || code == KbmInputCode::kRightCtrl ||
         code == KbmInputCode::kAlt || code == KbmInputCode::kLeftAlt ||
         code == KbmInputCode::kRightAlt || code == KbmInputCode::kSuper ||
         code == KbmInputCode::kLeftSuper || code == KbmInputCode::kRightSuper;
}

bool ParseKbmInputCode(std::string_view text, KbmInputCode& code) {
  for (const auto& named : kNamedInputCodes) {
    if (text == named.name) {
      code = named.code;
      return true;
    }
  }
  if (text.size() == 5 && StartsWith(text, "Key.") &&
      std::isalpha(static_cast<unsigned char>(text[4]))) {
    const char character =
        static_cast<char>(std::toupper(static_cast<unsigned char>(text[4])));
    code = static_cast<KbmInputCode>(
        static_cast<uint16_t>(KbmInputCode::kKeyA) + character - 'A');
    return true;
  }
  if (text.size() == 10 && StartsWith(text, "Key.Digit") &&
      std::isdigit(static_cast<unsigned char>(text[9]))) {
    code = static_cast<KbmInputCode>(
        static_cast<uint16_t>(KbmInputCode::kDigit0) + text[9] - '0');
    return true;
  }
  if (StartsWith(text, "Key.F")) {
    unsigned int number = 0;
    for (char character : text.substr(5)) {
      if (!std::isdigit(static_cast<unsigned char>(character))) {
        return false;
      }
      number = number * 10 + unsigned(character - '0');
    }
    if (number >= 1 && number <= 24) {
      code = static_cast<KbmInputCode>(
          static_cast<uint16_t>(KbmInputCode::kF1) + number - 1);
      return true;
    }
  }
  if (text.size() == 12 && StartsWith(text, "Key.Numpad") &&
      std::isdigit(static_cast<unsigned char>(text[11]))) {
    code = static_cast<KbmInputCode>(
        static_cast<uint16_t>(KbmInputCode::kNumpad0) + text[11] - '0');
    return true;
  }
  return false;
}

std::string FormatKbmInputCode(KbmInputCode code, bool display) {
  for (const auto& named : kNamedInputCodes) {
    if (code == named.code) {
      return std::string(display ? named.display_name : named.name);
    }
  }
  if (IsCodeInRange(code, KbmInputCode::kKeyA, 26)) {
    const char character =
        static_cast<char>('A' + static_cast<uint16_t>(code) -
                          static_cast<uint16_t>(KbmInputCode::kKeyA));
    return display ? std::string(1, character)
                   : fmt::format("Key.{}", character);
  }
  if (IsCodeInRange(code, KbmInputCode::kDigit0, 10)) {
    const char character =
        static_cast<char>('0' + static_cast<uint16_t>(code) -
                          static_cast<uint16_t>(KbmInputCode::kDigit0));
    return display ? std::string(1, character)
                   : fmt::format("Key.Digit{}", character);
  }
  if (IsCodeInRange(code, KbmInputCode::kF1, 24)) {
    const auto number = static_cast<uint16_t>(code) -
                        static_cast<uint16_t>(KbmInputCode::kF1) + 1;
    return display ? fmt::format("F{}", number)
                   : fmt::format("Key.F{}", number);
  }
  if (IsCodeInRange(code, KbmInputCode::kNumpad0, 10)) {
    const auto number = static_cast<uint16_t>(code) -
                        static_cast<uint16_t>(KbmInputCode::kNumpad0);
    return display ? fmt::format("Numpad {}", number)
                   : fmt::format("Key.Numpad{}", number);
  }
  return {};
}

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
      return ParseKbmInputCode(part, chord.input);
    }
    if (!ParseModifier(part, chord)) {
      return false;
    }
    part_begin = plus + 1;
  }
}

std::string FormatKbmChord(const KbmChord& chord, bool display) {
  if (chord.input == KbmInputCode::kNone) {
    return {};
  }
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
    result += "Super+";
  }
  result += FormatKbmInputCode(chord.input, display);
  return result;
}

std::string FormatKbmBinding(std::string_view binding) {
  std::string result;
  for (std::string_view token : utf8::split(binding, " ", true)) {
    std::string_view prefix;
    if (!token.empty() && (token.front() == '_' || token.front() == '^')) {
      prefix = token.substr(0, 1);
      token.remove_prefix(1);
    }
    KbmChord chord;
    const std::string formatted = ParseKbmChord(token, chord)
                                      ? FormatKbmChord(chord, true)
                                      : std::string(token);
    if (!result.empty()) {
      result += " / ";
    }
    result += prefix;
    result += formatted;
  }
  return result;
}

}  // namespace xe::hid::kbm
