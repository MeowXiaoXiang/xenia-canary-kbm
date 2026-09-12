/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2022 Ben Vanik. All rights reserved.                             *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#include "xenia/hid/winkey/winkey_input_driver.h"

#include <algorithm>
#include <cmath>
#include <limits>

#include "xenia/base/logging.h"
#include "xenia/base/platform_win.h"
#include "xenia/hid/hid_flags.h"
#include "xenia/hid/input_system.h"
#include "xenia/ui/virtual_key.h"
#include "xenia/ui/window.h"
#include "xenia/ui/window_win.h"
#include "xenia/ui/windowed_app_context.h"

#define XE_HID_WINKEY_BINDING(button, description, cvar_name,        \
                              cvar_default_value)                    \
  DEFINE_transient_string(cvar_name, cvar_default_value,             \
                          "Keys or chords bound to " description     \
                          ", with alternatives separated by spaces", \
                          "HID.WinKey")
#include "winkey_binding_table.inc"
#undef XE_HID_WINKEY_BINDING

DEFINE_transient_int32(
    keyboard_mode, 1,
    "Allows user do specify keyboard working mode. Possible values: 0 "
    "- Disabled, 1 - Enabled, 2 - Passthrough. Passthrough requires "
    "controller being connected!",
    "HID");

DEFINE_transient_int32(
    keyboard_user_index, 0,
    "Controller port that keyboard emulates. [0, 3] - Keyboard is assigned to "
    "selected slot. Passthrough does not require assigning slot.",
    "HID");

DEFINE_transient_bool(
    raw_mouse, true,
    "Use Windows Raw Input mouse movement for the emulated right "
    "thumbstick. Requires keyboard_mode = 1.",
    "HID.WinKey");

DEFINE_transient_double(raw_mouse_sensitivity, 10.0,
                        "Raw mouse sensitivity multiplier.", "HID.WinKey");

DEFINE_transient_double(
    raw_mouse_full_scale_velocity, 24000.0,
    "Raw Input counts per second that produce full right-stick deflection "
    "before applying the response curve.",
    "HID.WinKey");

DEFINE_transient_double(
    raw_mouse_response_curve, 1.2,
    "Raw mouse response exponent. 1 is linear, values above 1 add precision "
    "near the center, and values below 1 boost low speeds.",
    "HID.WinKey");

DEFINE_transient_bool(
    raw_mouse_deadzone_compensation, false,
    "Raise non-zero Raw Input mouse output above a configurable minimum "
    "response. Off preserves the original mouse-to-stick translation.",
    "HID.WinKey");

DEFINE_transient_double(
    raw_mouse_minimum_response, 0.30,
    "Minimum absolute right-stick response applied only while Raw Input mouse "
    "movement is non-zero and deadzone compensation is enabled.",
    "HID.WinKey");

DEFINE_transient_bool(raw_mouse_invert_y, false,
                      "Invert Raw Input mouse Y movement.", "HID.WinKey");

DEFINE_transient_string(
    raw_mouse_capture_toggle_key, "F8",
    "Key or modifier chord used to toggle Raw Input mouse capture. Leave "
    "empty to disable the hotkey.",
    "HID.WinKey");

DEFINE_transient_bool(
    raw_mouse_capture_on_start, false,
    "Capture and hide the mouse when Raw Input is initialized. The mouse is "
    "automatically released when Xenia loses focus.",
    "HID.WinKey");

namespace xe {
namespace hid {
namespace winkey {

static uint8_t VirtualKeyToHIDUsage(UINT vk) {
  // Letters: contiguous in both VK and HID space
  if (vk >= 'A' && vk <= 'Z') {
    return vk - 'A' + 0x04;
  }

  // Digits 1-9 (0 is irregular: 0x27)
  if (vk >= '1' && vk <= '9') {
    return vk - '1' + 0x1E;
  }

  // F1-F12
  if (vk >= VK_F1 && vk <= VK_F12) {
    return vk - VK_F1 + 0x3A;
  }

  // F13-F24
  if (vk >= VK_F13 && vk <= VK_F24) {
    return vk - VK_F13 + 0x68;
  }

  // Numpad 1-9 (0 is irregular: 0x62)
  if (vk >= VK_NUMPAD1 && vk <= VK_NUMPAD9) {
    return vk - VK_NUMPAD1 + 0x59;
  }

  // Modifiers (Left side starts at 0xE0, Right at 0xE4)
  if (vk >= VK_LCONTROL && vk <= VK_LWIN) {
    return vk - VK_LCONTROL + 0xE0;
  }
  if (vk >= VK_RCONTROL && vk <= VK_RWIN) {
    return vk - VK_RCONTROL + 0xE4;
  }

  switch (vk) {
    case '0':
      return 0x27;
    case VK_RETURN:
      return 0x28;
    case VK_ESCAPE:
      return 0x29;
    case VK_BACK:
      return 0x2A;
    case VK_TAB:
      return 0x2B;
    case VK_SPACE:
      return 0x2C;
    case VK_OEM_MINUS:
      return 0x2D;
    case VK_OEM_PLUS:
      return 0x2E;
    case VK_OEM_4:
      return 0x2F;
    case VK_OEM_6:
      return 0x30;
    case VK_OEM_5:
      return 0x31;
    case VK_OEM_1:
      return 0x33;
    case VK_OEM_7:
      return 0x34;
    case VK_OEM_3:
      return 0x35;
    case VK_OEM_COMMA:
      return 0x36;
    case VK_OEM_PERIOD:
      return 0x37;
    case VK_OEM_2:
      return 0x38;
    case VK_CAPITAL:
      return 0x39;
    case VK_SNAPSHOT:
      return 0x46;
    case VK_SCROLL:
      return 0x47;
    case VK_PAUSE:
      return 0x48;
    case VK_INSERT:
      return 0x49;
    case VK_HOME:
      return 0x4A;
    case VK_PRIOR:
      return 0x4B;
    case VK_DELETE:
      return 0x4C;
    case VK_END:
      return 0x4D;
    case VK_NEXT:
      return 0x4E;
    case VK_RIGHT:
      return 0x4F;
    case VK_LEFT:
      return 0x50;
    case VK_DOWN:
      return 0x51;
    case VK_UP:
      return 0x52;
    case VK_NUMLOCK:
      return 0x53;
    case VK_DIVIDE:
      return 0x54;
    case VK_MULTIPLY:
      return 0x55;
    case VK_SUBTRACT:
      return 0x56;
    case VK_ADD:
      return 0x57;
    case VK_NUMPAD0:
      return 0x62;
    case VK_DECIMAL:
      return 0x63;
    case VK_APPS:
      return 0x65;
    default:
      break;
  }
  return 0x00;
}

bool static IsPassthroughEnabled(const WinKeySettings& settings) {
  return static_cast<KeyboardMode>(settings.keyboard_mode) ==
         KeyboardMode::Passthrough;
}

bool static IsKeyboardForUserEnabled(const WinKeySettings& settings,
                                     uint32_t user_index) {
  if (static_cast<KeyboardMode>(settings.keyboard_mode) !=
      KeyboardMode::Enabled) {
    return false;
  }

  return settings.keyboard_user_index == user_index;
}

bool __inline IsKeyToggled(uint8_t key) {
  return (GetKeyState(key) & 0x1) == 0x1;
}

bool __inline IsKeyDown(uint8_t key) {
  return (GetAsyncKeyState(key) & 0x8000) == 0x8000;
}

bool __inline IsKeyDown(ui::VirtualKey virtual_key) {
  return IsKeyDown(static_cast<uint8_t>(virtual_key));
}

static bool IsMouseVirtualKey(ui::VirtualKey key) {
  return key == ui::VirtualKey::kLButton || key == ui::VirtualKey::kRButton ||
         key == ui::VirtualKey::kMButton || key == ui::VirtualKey::kXButton1 ||
         key == ui::VirtualKey::kXButton2;
}

static bool IsModifierVirtualKey(ui::VirtualKey key) {
  return key == ui::VirtualKey::kShift || key == ui::VirtualKey::kLShift ||
         key == ui::VirtualKey::kRShift || key == ui::VirtualKey::kControl ||
         key == ui::VirtualKey::kLControl || key == ui::VirtualKey::kRControl ||
         key == ui::VirtualKey::kMenu || key == ui::VirtualKey::kLMenu ||
         key == ui::VirtualKey::kRMenu || key == ui::VirtualKey::kLWin ||
         key == ui::VirtualKey::kRWin;
}

static bool ModifiersMatch(bool required_shift, bool required_ctrl,
                           bool required_alt, bool required_super, bool shift,
                           bool ctrl, bool alt, bool super) {
  return (!required_shift || shift) && (!required_ctrl || ctrl) &&
         (!required_alt || alt) && (!required_super || super);
}

static int16_t MouseDeltaToThumb(int64_t delta, double elapsed_seconds,
                                 const WinKeySettings& settings) {
  if (!delta) {
    return 0;
  }

  const double full_scale_velocity =
      std::max(settings.raw_mouse_full_scale_velocity, 1.0);
  const double sensitivity = std::max(settings.raw_mouse_sensitivity, 0.0);
  double normalized =
      (double(delta) / elapsed_seconds) * sensitivity / full_scale_velocity;
  normalized = std::clamp(normalized, -1.0, 1.0);

  const double response_curve =
      std::max(settings.raw_mouse_response_curve, 0.01);
  normalized =
      std::copysign(std::pow(std::abs(normalized), response_curve), normalized);

  if (settings.raw_mouse_deadzone_compensation) {
    const double minimum_response =
        std::clamp(settings.raw_mouse_minimum_response, 0.0, 0.5);
    normalized = std::copysign(
        minimum_response + (1.0 - minimum_response) * std::abs(normalized),
        normalized);
  }

  const long scaled =
      std::lround(normalized * std::numeric_limits<int16_t>::max());
  return static_cast<int16_t>(
      std::clamp(scaled, long(std::numeric_limits<int16_t>::min()),
                 long(std::numeric_limits<int16_t>::max())));
}

static int16_t AddThumbWithSaturation(int16_t left, int16_t right) {
  const int32_t sum = int32_t(left) + int32_t(right);
  return static_cast<int16_t>(
      std::clamp(sum, int32_t(std::numeric_limits<int16_t>::min()),
                 int32_t(std::numeric_limits<int16_t>::max())));
}

void WinKeyInputDriver::ParseKeyBinding(std::vector<KeyBinding>& bindings,
                                        ui::VirtualKey output_key,
                                        const std::string_view description,
                                        const std::string_view source_tokens) {
  for (const std::string_view source_token :
       utf8::split(source_tokens, " ", true)) {
    KeyBinding key_binding;
    key_binding.output_key = output_key;

    std::string_view token = source_token;

    if (utf8::starts_with(token, "_")) {
      key_binding.lowercase = true;
      token = token.substr(1);
    } else if (utf8::starts_with(token, "^")) {
      key_binding.uppercase = true;
      token = token.substr(1);
    }

    WinKeyChord chord;
    if (!ParseWinKeyChord(token, chord)) {
      XELOGW("winkey: failed to parse key \"{}\" for {}.", source_token,
             description);
      continue;
    }
    key_binding.input_key = static_cast<ui::VirtualKey>(chord.virtual_key);
    key_binding.shift = chord.shift;
    key_binding.ctrl = chord.ctrl;
    key_binding.alt = chord.alt;
    key_binding.super = chord.super;

    bindings.push_back(key_binding);
    XELOGI("winkey: \"{}\" binds key 0x{:X} to controller input {}.",
           source_token, static_cast<uint16_t>(key_binding.input_key),
           description);
  }
}

void WinKeyInputDriver::RebuildKeyBindings(const WinKeySettings& settings) {
  std::vector<KeyBinding> bindings;
#define XE_HID_WINKEY_BINDING(button, description, cvar_name,       \
                              cvar_default_value)                   \
  ParseKeyBinding(bindings, xe::ui::VirtualKey::kXInputPad##button, \
                  description, settings.cvar_name);
#include "winkey_binding_table.inc"
#undef XE_HID_WINKEY_BINDING
  key_bindings_ = std::move(bindings);
}

WinKeyInputDriver::WinKeyInputDriver(xe::ui::Window* window,
                                     size_t window_z_order)
    : InputDriver(window, window_z_order),
      window_input_listener_(*this),
      window_listener_(*this),
      raw_mouse_last_sample_time_(std::chrono::steady_clock::now()),
      raw_mouse_last_center_time_(std::chrono::steady_clock::now()) {
  settings_ = GetSettingsFromCvars();
  RebuildKeyBindings(settings_);
  ParseWinKeyChord(settings_.raw_mouse_capture_toggle_key,
                   raw_mouse_capture_toggle_);

  window->AddListener(&window_listener_);
  window->AddInputListener(&window_input_listener_, window_z_order);
}

WinKeyInputDriver::~WinKeyInputDriver() {
  raw_mouse_capture_requested_ = false;
  ReleaseRawMouseCapture();
  UnregisterRawMouse();
  window()->RemoveInputListener(&window_input_listener_);
  window()->RemoveListener(&window_listener_);
}

X_STATUS WinKeyInputDriver::Setup() {
  WinKeySettings settings = GetSettings();
  if (settings.raw_mouse && static_cast<KeyboardMode>(settings.keyboard_mode) ==
                                KeyboardMode::Enabled) {
    RegisterRawMouse();
  }
  if (raw_mouse_registered_ && settings.raw_mouse_capture_on_start) {
    raw_mouse_capture_requested_ = true;
    window()->app_context().CallInUIThreadSynchronous(
        [this]() { ApplyRawMouseCapture(); });
  }
  return X_STATUS_SUCCESS;
}

bool WinKeyInputDriver::RegisterRawMouse(bool exclusive_capture) {
  const DWORD registration_flags =
      exclusive_capture ? RIDEV_NOLEGACY | RIDEV_CAPTUREMOUSE : 0;
  if (raw_mouse_registered_ &&
      raw_mouse_registration_flags_ == registration_flags) {
    return true;
  }
  auto* win32_window = static_cast<ui::Win32Window*>(window());
  RAWINPUTDEVICE raw_mouse_device = {};
  raw_mouse_device.usUsagePage = 0x01;
  raw_mouse_device.usUsage = 0x02;
  raw_mouse_device.dwFlags = registration_flags;
  raw_mouse_device.hwndTarget = win32_window->hwnd();
  if (!raw_mouse_device.hwndTarget ||
      !RegisterRawInputDevices(&raw_mouse_device, 1,
                               sizeof(raw_mouse_device))) {
    XELOGE("winkey: failed to register Raw Input mouse, error {}.",
           GetLastError());
    return false;
  }

  const bool was_registered = raw_mouse_registered_.exchange(true);
  raw_mouse_registration_flags_ = registration_flags;
  if (!was_registered) {
    raw_mouse_last_sample_time_ = std::chrono::steady_clock::now();
  }
  XELOGI("winkey: Raw Input mouse registered for right thumbstick ({}).",
         exclusive_capture ? "exclusive capture" : "foreground");
  return true;
}

void WinKeyInputDriver::UnregisterRawMouse() {
  if (!raw_mouse_registered_) {
    return;
  }
  raw_mouse_capture_requested_ = false;
  ReleaseRawMouseCapture();
  RAWINPUTDEVICE raw_mouse_device = {};
  raw_mouse_device.usUsagePage = 0x01;
  raw_mouse_device.usUsage = 0x02;
  raw_mouse_device.dwFlags = RIDEV_REMOVE;
  raw_mouse_device.hwndTarget = nullptr;
  if (!RegisterRawInputDevices(&raw_mouse_device, 1,
                               sizeof(raw_mouse_device))) {
    XELOGW("winkey: failed to unregister Raw Input mouse, error {}.",
           GetLastError());
  }
  raw_mouse_registered_ = false;
  raw_mouse_registration_flags_ = 0;
  raw_mouse_delta_x_ = 0;
  raw_mouse_delta_y_ = 0;
  raw_mouse_counts_per_second_x_ = 0.0;
  raw_mouse_counts_per_second_y_ = 0.0;
  raw_mouse_thumb_x_ = 0;
  raw_mouse_thumb_y_ = 0;
  XELOGI("winkey: Raw Input mouse unregistered.");
}

WinKeySettings WinKeyInputDriver::GetSettings() const {
  auto lock = settings_critical_region_.Acquire();
  return settings_;
}

void WinKeyInputDriver::ApplySettings(const WinKeySettings& source_settings) {
  WinKeySettings settings = source_settings;
  settings.keyboard_mode = std::clamp(settings.keyboard_mode, 0, 2);
  settings.keyboard_user_index = std::clamp(settings.keyboard_user_index, 0, 3);
  settings.raw_mouse_sensitivity =
      std::clamp(settings.raw_mouse_sensitivity, 0.01, 256.0);
  settings.raw_mouse_full_scale_velocity =
      std::clamp(settings.raw_mouse_full_scale_velocity, 1.0, 1000000.0);
  settings.raw_mouse_response_curve =
      std::clamp(settings.raw_mouse_response_curve, 0.1, 4.0);
  settings.raw_mouse_minimum_response =
      std::clamp(settings.raw_mouse_minimum_response, 0.0, 0.5);

  {
    auto lock = settings_critical_region_.Acquire();
    settings_ = settings;
    RebuildKeyBindings(settings_);
    if (!ParseWinKeyChord(settings_.raw_mouse_capture_toggle_key,
                          raw_mouse_capture_toggle_)) {
      raw_mouse_capture_toggle_ = {};
      if (!settings_.raw_mouse_capture_toggle_key.empty()) {
        XELOGW("winkey: failed to parse Raw Input capture toggle \"{}\".",
               settings_.raw_mouse_capture_toggle_key);
      }
    }
  }
  ApplySettingsToCvars(settings);

  const bool should_register =
      settings.raw_mouse && static_cast<KeyboardMode>(settings.keyboard_mode) ==
                                KeyboardMode::Enabled;
  if (should_register) {
    RegisterRawMouse();
  } else {
    UnregisterRawMouse();
  }
}

WinKeyInputDriver::Diagnostics WinKeyInputDriver::GetDiagnostics() const {
  Diagnostics diagnostics;
  diagnostics.raw_mouse_registered = raw_mouse_registered_;
  diagnostics.capture_requested = raw_mouse_capture_requested_;
  diagnostics.capture_active = raw_mouse_capture_active_;
  diagnostics.raw_counts_per_second_x = raw_mouse_counts_per_second_x_;
  diagnostics.raw_counts_per_second_y = raw_mouse_counts_per_second_y_;
  diagnostics.thumb_x = raw_mouse_thumb_x_;
  diagnostics.thumb_y = raw_mouse_thumb_y_;
  return diagnostics;
}

void WinKeyInputDriver::SetHostInputSuspended(bool suspended) {
  host_input_suspended_ = suspended;
  if (suspended) {
    ReleaseRawMouseCapture();
  } else {
    ApplyRawMouseCapture();
  }
}

void WinKeyInputDriver::SetCaptureStateCallback(CaptureStateCallback callback) {
  capture_state_callback_ = std::move(callback);
  if (capture_state_callback_ && raw_mouse_capture_active_) {
    NotifyCaptureState(true);
  }
}

void WinKeyInputDriver::BeginBindingCapture() {
  auto lock = global_critical_region_.Acquire();
  binding_capture_active_ = true;
  binding_capture_result_ = {};
}

WinKeyInputDriver::BindingCaptureResult
WinKeyInputDriver::ConsumeBindingCaptureResult() {
  auto lock = global_critical_region_.Acquire();
  BindingCaptureResult result = std::move(binding_capture_result_);
  binding_capture_result_ = {};
  return result;
}

void WinKeyInputDriver::CancelBindingCapture() {
  auto lock = global_critical_region_.Acquire();
  binding_capture_active_ = false;
  binding_capture_result_ = {};
}

bool WinKeyInputDriver::CompleteBindingCapture(BindingCaptureStatus status,
                                               std::string value) {
  auto lock = global_critical_region_.Acquire();
  if (!binding_capture_active_) {
    return false;
  }
  binding_capture_active_ = false;
  binding_capture_result_.status = status;
  binding_capture_result_.value = std::move(value);
  return true;
}

X_RESULT WinKeyInputDriver::GetCapabilities(uint32_t user_index, uint32_t flags,
                                            X_INPUT_CAPABILITIES* out_caps) {
  WinKeySettings settings = GetSettings();
  if (!IsKeyboardForUserEnabled(settings, user_index) &&
      !IsPassthroughEnabled(settings)) {
    return X_ERROR_DEVICE_NOT_CONNECTED;
  }

  if (IsPassthroughEnabled(settings)) {
    out_caps->type = X_INPUT_DEVTYPE::XINPUT_DEVTYPE_KEYBOARD;
    out_caps->sub_type = X_INPUT_DEVSUBTYPE::XINPUT_DEVSUBTYPE_USB_KEYBOARD;
    return X_ERROR_SUCCESS;
  }

  out_caps->type = X_INPUT_DEVTYPE::XINPUT_DEVTYPE_GAMEPAD;
  out_caps->sub_type = X_INPUT_DEVSUBTYPE::XINPUT_DEVSUBTYPE_GAMEPAD;
  out_caps->flags = 0;
  out_caps->gamepad.buttons = 0xFFFF;
  out_caps->gamepad.left_trigger = 0xFF;
  out_caps->gamepad.right_trigger = 0xFF;
  out_caps->gamepad.thumb_lx = (int16_t)0xFFFFu;
  out_caps->gamepad.thumb_ly = (int16_t)0xFFFFu;
  out_caps->gamepad.thumb_rx = (int16_t)0xFFFFu;
  out_caps->gamepad.thumb_ry = (int16_t)0xFFFFu;
  out_caps->vibration.left_motor_speed = 0;
  out_caps->vibration.right_motor_speed = 0;
  return X_ERROR_SUCCESS;
}

X_RESULT WinKeyInputDriver::GetState(uint32_t user_index,
                                     X_INPUT_STATE* out_state) {
  auto settings_lock = settings_critical_region_.Acquire();
  const WinKeySettings& settings = settings_;
  if (!IsKeyboardForUserEnabled(settings, user_index)) {
    return X_ERROR_DEVICE_NOT_CONNECTED;
  }

  packet_number_++;

  uint16_t buttons = 0;
  uint8_t left_trigger = 0;
  uint8_t right_trigger = 0;
  int16_t thumb_lx = 0;
  int16_t thumb_ly = 0;
  int16_t thumb_rx = 0;
  int16_t thumb_ry = 0;

  if (window()->HasFocus() && !host_input_suspended_) {
    const bool shift = IsKeyDown(VK_SHIFT);
    const bool ctrl = IsKeyDown(VK_CONTROL);
    const bool alt = IsKeyDown(VK_MENU);
    const bool super = IsKeyDown(VK_LWIN) || IsKeyDown(VK_RWIN);
    const bool capital = IsKeyToggled(VK_CAPITAL) || shift;
    const bool mouse_buttons_enabled =
        raw_mouse_capture_active_ && !host_input_suspended_;
    for (const KeyBinding& b : key_bindings_) {
      if (IsMouseVirtualKey(b.input_key) && !mouse_buttons_enabled) {
        continue;
      }
      if (((b.lowercase == b.uppercase) || (b.lowercase && !capital) ||
           (b.uppercase && capital)) &&
          ModifiersMatch(b.shift, b.ctrl, b.alt, b.super, shift, ctrl, alt,
                         super) &&
          IsKeyDown(b.input_key)) {
        switch (b.output_key) {
          case ui::VirtualKey::kXInputPadA:
            buttons |= X_INPUT_GAMEPAD_A;
            break;
          case ui::VirtualKey::kXInputPadY:
            buttons |= X_INPUT_GAMEPAD_Y;
            break;
          case ui::VirtualKey::kXInputPadB:
            buttons |= X_INPUT_GAMEPAD_B;
            break;
          case ui::VirtualKey::kXInputPadX:
            buttons |= X_INPUT_GAMEPAD_X;
            break;
          case ui::VirtualKey::kXInputPadGuide:
            buttons |= X_INPUT_GAMEPAD_GUIDE;
            break;
          case ui::VirtualKey::kXInputPadDpadLeft:
            buttons |= X_INPUT_GAMEPAD_DPAD_LEFT;
            break;
          case ui::VirtualKey::kXInputPadDpadRight:
            buttons |= X_INPUT_GAMEPAD_DPAD_RIGHT;
            break;
          case ui::VirtualKey::kXInputPadDpadDown:
            buttons |= X_INPUT_GAMEPAD_DPAD_DOWN;
            break;
          case ui::VirtualKey::kXInputPadDpadUp:
            buttons |= X_INPUT_GAMEPAD_DPAD_UP;
            break;
          case ui::VirtualKey::kXInputPadRThumbPress:
            buttons |= X_INPUT_GAMEPAD_RIGHT_THUMB;
            break;
          case ui::VirtualKey::kXInputPadLThumbPress:
            buttons |= X_INPUT_GAMEPAD_LEFT_THUMB;
            break;
          case ui::VirtualKey::kXInputPadBack:
            buttons |= X_INPUT_GAMEPAD_BACK;
            break;
          case ui::VirtualKey::kXInputPadStart:
            buttons |= X_INPUT_GAMEPAD_START;
            break;
          case ui::VirtualKey::kXInputPadLShoulder:
            buttons |= X_INPUT_GAMEPAD_LEFT_SHOULDER;
            break;
          case ui::VirtualKey::kXInputPadRShoulder:
            buttons |= X_INPUT_GAMEPAD_RIGHT_SHOULDER;
            break;
          case ui::VirtualKey::kXInputPadLTrigger:
            left_trigger = 0xFF;
            break;
          case ui::VirtualKey::kXInputPadRTrigger:
            right_trigger = 0xFF;
            break;
          case ui::VirtualKey::kXInputPadLThumbLeft:
            thumb_lx += SHRT_MIN;
            break;
          case ui::VirtualKey::kXInputPadLThumbRight:
            thumb_lx += SHRT_MAX;
            break;
          case ui::VirtualKey::kXInputPadLThumbDown:
            thumb_ly += SHRT_MIN;
            break;
          case ui::VirtualKey::kXInputPadLThumbUp:
            thumb_ly += SHRT_MAX;
            break;
          case ui::VirtualKey::kXInputPadRThumbUp:
            thumb_ry += SHRT_MAX;
            break;
          case ui::VirtualKey::kXInputPadRThumbDown:
            thumb_ry += SHRT_MIN;
            break;
          case ui::VirtualKey::kXInputPadRThumbRight:
            thumb_rx += SHRT_MAX;
            break;
          case ui::VirtualKey::kXInputPadRThumbLeft:
            thumb_rx += SHRT_MIN;
            break;
        }
      }
    }
  }

  if (settings.raw_mouse && raw_mouse_registered_) {
    const auto sample_time = std::chrono::steady_clock::now();
    double elapsed_seconds =
        std::chrono::duration<double>(sample_time - raw_mouse_last_sample_time_)
            .count();
    raw_mouse_last_sample_time_ = sample_time;
    elapsed_seconds = std::clamp(elapsed_seconds, 0.001, 0.1);

    int64_t delta_x = raw_mouse_delta_x_.exchange(0);
    int64_t delta_y = raw_mouse_delta_y_.exchange(0);
    const bool raw_mouse_input_active = window()->HasFocus() &&
                                        raw_mouse_capture_active_ &&
                                        !host_input_suspended_;
    if (!raw_mouse_input_active) {
      delta_x = 0;
      delta_y = 0;
    }

    const int16_t mouse_thumb_x =
        MouseDeltaToThumb(delta_x, elapsed_seconds, settings);
    const int64_t mapped_delta_y =
        settings.raw_mouse_invert_y ? delta_y : -delta_y;
    const int16_t mouse_thumb_y =
        MouseDeltaToThumb(mapped_delta_y, elapsed_seconds, settings);
    if (raw_mouse_input_active) {
      // Keep the last active sample available while a host dialog suspends
      // guest input, so the settings page can be used for calibration.
      raw_mouse_counts_per_second_x_ = double(delta_x) / elapsed_seconds;
      raw_mouse_counts_per_second_y_ = double(delta_y) / elapsed_seconds;
      raw_mouse_thumb_x_ = mouse_thumb_x;
      raw_mouse_thumb_y_ = mouse_thumb_y;
    }
    thumb_rx = AddThumbWithSaturation(thumb_rx, mouse_thumb_x);
    thumb_ry = AddThumbWithSaturation(thumb_ry, mouse_thumb_y);
  }

  out_state->packet_number = packet_number_;
  out_state->gamepad.buttons = buttons;
  out_state->gamepad.left_trigger = left_trigger;
  out_state->gamepad.right_trigger = right_trigger;
  out_state->gamepad.thumb_lx = thumb_lx;
  out_state->gamepad.thumb_ly = thumb_ly;
  out_state->gamepad.thumb_rx = thumb_rx;
  out_state->gamepad.thumb_ry = thumb_ry;

  if (IsPassthroughEnabled(settings)) {
    memset(out_state, 0, sizeof(out_state));
  }

  return X_ERROR_SUCCESS;
}

X_RESULT WinKeyInputDriver::SetState(uint32_t user_index,
                                     X_INPUT_VIBRATION* vibration) {
  WinKeySettings settings = GetSettings();
  if (!IsKeyboardForUserEnabled(settings, user_index) &&
      !IsPassthroughEnabled(settings)) {
    return X_ERROR_DEVICE_NOT_CONNECTED;
  }

  return X_ERROR_SUCCESS;
}

X_RESULT WinKeyInputDriver::GetKeystroke(uint32_t user_index, uint32_t flags,
                                         X_INPUT_KEYSTROKE* out_keystroke) {
  auto settings_lock = settings_critical_region_.Acquire();
  const WinKeySettings& settings = settings_;
  if (!IsKeyboardForUserEnabled(settings, user_index) &&
      !IsPassthroughEnabled(settings)) {
    return X_ERROR_DEVICE_NOT_CONNECTED;
  }
  // Pop from the queue.
  KeyEvent evt;
  {
    auto global_lock = global_critical_region_.Acquire();
    if (key_events_.empty()) {
      // No keys!
      return X_ERROR_EMPTY;
    }
    evt = key_events_.front();
    key_events_.pop();
  }

  X_RESULT result = X_ERROR_EMPTY;

  ui::VirtualKey xinput_virtual_key = ui::VirtualKey::kNone;
  uint16_t unicode = 0;
  uint16_t keystroke_flags = 0;
  uint8_t hid_code = 0;

  const bool capital = IsKeyToggled(VK_CAPITAL) || evt.shift;

  if (!IsPassthroughEnabled(settings)) {
    if (IsKeyboardForUserEnabled(settings, user_index)) {
      for (const KeyBinding& b : key_bindings_) {
        if (b.input_key == evt.virtual_key &&
            ((b.lowercase == b.uppercase) || (b.lowercase && !capital) ||
             (b.uppercase && capital)) &&
            ModifiersMatch(b.shift, b.ctrl, b.alt, b.super, evt.shift, evt.ctrl,
                           evt.alt, evt.super)) {
          xinput_virtual_key = b.output_key;
        }
      }
    }
  } else {
    xinput_virtual_key = evt.virtual_key;

    if (evt.shift) {
      keystroke_flags |= 0x0008;  // XINPUT_KEYSTROKE_SHIFT
    }

    if (evt.ctrl) {
      keystroke_flags |= 0x0010;  // XINPUT_KEYSTROKE_CTRL
    }

    if (evt.alt) {
      keystroke_flags |= 0x0020;  // XINPUT_KEYSTROKE_ALT
    }
  }

  if (xinput_virtual_key != ui::VirtualKey::kNone) {
    if (evt.transition == true) {
      keystroke_flags |= 0x0001;  // XINPUT_KEYSTROKE_KEYDOWN
      if (evt.prev_state == evt.transition) {
        keystroke_flags |= 0x0004;  // XINPUT_KEYSTROKE_REPEAT
      }
    } else if (evt.transition == false) {
      keystroke_flags |= 0x0002;  // XINPUT_KEYSTROKE_KEYUP
    }

    if (IsPassthroughEnabled(settings)) {
      const UINT vk = static_cast<UINT>(xinput_virtual_key);
      hid_code = VirtualKeyToHIDUsage(vk);
      if (GetKeyboardState(key_map_)) {
        const UINT sc = MapVirtualKey(vk, MAPVK_VK_TO_VSC);
        WCHAR buf;
        if (ToUnicode(vk, sc, key_map_, &buf, 1, 0) == 1) {
          keystroke_flags |= 0x1000;  // XINPUT_KEYSTROKE_VALIDUNICODE
          unicode = buf;
        }
      }
    }

    result = X_ERROR_SUCCESS;
  }

  out_keystroke->virtual_key = uint16_t(xinput_virtual_key);
  out_keystroke->unicode = unicode;
  out_keystroke->flags = keystroke_flags;
  out_keystroke->user_index = user_index;
  out_keystroke->hid_code = hid_code;

  // X_ERROR_EMPTY if no new keys
  // X_ERROR_DEVICE_NOT_CONNECTED if no device
  // X_ERROR_SUCCESS if key
  return result;
}

void WinKeyInputDriver::WinKeyWindowInputListener::OnKeyDown(ui::KeyEvent& e) {
  driver_.OnKey(e, true);
}

void WinKeyInputDriver::WinKeyWindowInputListener::OnKeyUp(ui::KeyEvent& e) {
  driver_.OnKey(e, false);
}

void WinKeyInputDriver::WinKeyWindowInputListener::OnMouseDown(
    ui::MouseEvent& e) {
  driver_.OnMouseDown(e);
}

void WinKeyInputDriver::WinKeyWindowInputListener::OnRawMouseMove(
    ui::RawMouseMoveEvent& e) {
  driver_.OnRawMouseMove(e);
}

void WinKeyInputDriver::WinKeyWindowListener::OnClosing(ui::UIEvent& e) {
  driver_.raw_mouse_capture_requested_ = false;
  driver_.ReleaseRawMouseCapture();
}

void WinKeyInputDriver::WinKeyWindowListener::OnResize(ui::UISetupEvent& e) {
  if (driver_.raw_mouse_capture_active_) {
    driver_.RefreshRawMouseCapture();
  }
}

void WinKeyInputDriver::WinKeyWindowListener::OnGotFocus(ui::UISetupEvent& e) {
  driver_.ApplyRawMouseCapture();
}

void WinKeyInputDriver::WinKeyWindowListener::OnLostFocus(ui::UISetupEvent& e) {
  driver_.ReleaseRawMouseCapture();
}

void WinKeyInputDriver::OnKey(ui::KeyEvent& e, bool is_down) {
  const bool is_modifier = IsModifierVirtualKey(e.virtual_key());
  if (is_down && !e.prev_state() && !is_modifier) {
    if (e.virtual_key() == ui::VirtualKey::kEscape) {
      if (CompleteBindingCapture(BindingCaptureStatus::kCancelled)) {
        e.set_handled(true);
        return;
      }
    } else {
      WinKeyChord chord;
      chord.virtual_key = static_cast<uint16_t>(e.virtual_key());
      chord.shift = e.is_shift_pressed();
      chord.ctrl = e.is_ctrl_pressed();
      chord.alt = e.is_alt_pressed();
      chord.super = IsKeyDown(VK_LWIN) || IsKeyDown(VK_RWIN);
      if (CompleteBindingCapture(BindingCaptureStatus::kCaptured,
                                 FormatWinKeyChord(chord))) {
        e.set_handled(true);
        return;
      }
    }
  } else if (!is_down && is_modifier) {
    WinKeyChord chord;
    chord.virtual_key = static_cast<uint16_t>(e.virtual_key());
    if (CompleteBindingCapture(BindingCaptureStatus::kCaptured,
                               FormatWinKeyChord(chord))) {
      e.set_handled(true);
      return;
    }
  }

  WinKeySettings settings;
  WinKeyChord capture_toggle;
  {
    auto lock = settings_critical_region_.Acquire();
    settings = settings_;
    capture_toggle = raw_mouse_capture_toggle_;
  }
  if (settings.raw_mouse && raw_mouse_registered_ && !host_input_suspended_ &&
      capture_toggle.virtual_key &&
      e.virtual_key() ==
          static_cast<ui::VirtualKey>(capture_toggle.virtual_key) &&
      ModifiersMatch(capture_toggle.shift, capture_toggle.ctrl,
                     capture_toggle.alt, capture_toggle.super,
                     e.is_shift_pressed(), e.is_ctrl_pressed(),
                     e.is_alt_pressed(), IsKeyDown(VK_LWIN) ||
                                             IsKeyDown(VK_RWIN))) {
    e.set_handled(true);
    if (is_down && !e.prev_state()) {
      ToggleRawMouseCapture();
    }
    return;
  }

  if (IsMouseVirtualKey(e.virtual_key()) &&
      (!raw_mouse_capture_active_ || host_input_suspended_)) {
    return;
  }

  if (static_cast<KeyboardMode>(settings.keyboard_mode) ==
          KeyboardMode::Disabled ||
      host_input_suspended_) {
    return;
  }

  KeyEvent key;
  key.virtual_key = e.virtual_key();
  key.transition = is_down;
  key.prev_state = e.prev_state();
  key.repeat_count = e.repeat_count();
  key.shift = e.is_shift_pressed();
  key.ctrl = e.is_ctrl_pressed();
  key.alt = e.is_alt_pressed();
  key.super = IsKeyDown(VK_LWIN) || IsKeyDown(VK_RWIN);

  auto global_lock = global_critical_region_.Acquire();
  key_events_.push(key);
}

void WinKeyInputDriver::OnMouseDown(ui::MouseEvent& e) {
  uint16_t virtual_key = 0;
  switch (e.button()) {
    case ui::MouseEvent::Button::kLeft:
      virtual_key = VK_LBUTTON;
      break;
    case ui::MouseEvent::Button::kRight:
      virtual_key = VK_RBUTTON;
      break;
    case ui::MouseEvent::Button::kMiddle:
      virtual_key = VK_MBUTTON;
      break;
    case ui::MouseEvent::Button::kX1:
      virtual_key = VK_XBUTTON1;
      break;
    case ui::MouseEvent::Button::kX2:
      virtual_key = VK_XBUTTON2;
      break;
    default:
      return;
  }

  WinKeyChord chord;
  chord.virtual_key = virtual_key;
  chord.shift = IsKeyDown(VK_SHIFT);
  chord.ctrl = IsKeyDown(VK_CONTROL);
  chord.alt = IsKeyDown(VK_MENU);
  chord.super = IsKeyDown(VK_LWIN) || IsKeyDown(VK_RWIN);
  if (CompleteBindingCapture(BindingCaptureStatus::kCaptured,
                             FormatWinKeyChord(chord))) {
    e.set_handled(true);
  }
}

void WinKeyInputDriver::OnRawMouseMove(ui::RawMouseMoveEvent& e) {
  WinKeySettings settings = GetSettings();
  if (!settings.raw_mouse || !raw_mouse_registered_ ||
      static_cast<KeyboardMode>(settings.keyboard_mode) !=
          KeyboardMode::Enabled ||
      !raw_mouse_capture_active_ || host_input_suspended_ ||
      !window()->HasFocus()) {
    return;
  }

  raw_mouse_delta_x_.fetch_add(e.delta_x(), std::memory_order_relaxed);
  raw_mouse_delta_y_.fetch_add(e.delta_y(), std::memory_order_relaxed);

  const auto now = std::chrono::steady_clock::now();
  if (now - raw_mouse_last_center_time_ >= std::chrono::milliseconds(50)) {
    // Reassert the Win32 capture, cursor state and clip. Fullscreen changes or
    // injected overlays may disturb any of them while Xenia still has focus.
    RefreshRawMouseCapture();
    raw_mouse_last_center_time_ = now;
  }
}

void WinKeyInputDriver::ToggleRawMouseCapture() {
  raw_mouse_capture_requested_ = !raw_mouse_capture_requested_;
  if (raw_mouse_capture_requested_) {
    ApplyRawMouseCapture();
  } else {
    ReleaseRawMouseCapture();
  }
}

void WinKeyInputDriver::ApplyRawMouseCapture() {
  if (!raw_mouse_capture_requested_ || raw_mouse_capture_active_ ||
      !raw_mouse_registered_ || host_input_suspended_ ||
      !window()->HasFocus()) {
    return;
  }

  auto* win32_window = static_cast<ui::Win32Window*>(window());
  if (!win32_window->hwnd()) {
    return;
  }

  // Suppress legacy mouse messages and prevent mouse buttons from activating
  // another window while captured. The registration is restored to normal in
  // ReleaseRawMouseCapture so the host UI works normally when released.
  if (!RegisterRawMouse(true)) {
    XELOGW(
        "winkey: exclusive Raw Input registration failed; mouse capture "
        "was not activated.");
    return;
  }

  raw_mouse_previous_cursor_visibility_ = window()->GetCursorVisibility();
  window()->CaptureMouse();
  window()->SetCursorVisibility(ui::Window::CursorVisibility::kHidden);
  if (!UpdateRawMouseClipRectangle()) {
    window()->SetCursorVisibility(raw_mouse_previous_cursor_visibility_);
    window()->ReleaseMouse();
    RegisterRawMouse(false);
    XELOGW("winkey: Raw Input mouse capture was not activated.");
    return;
  }

  raw_mouse_last_center_time_ = std::chrono::steady_clock::now();
  raw_mouse_capture_active_ = true;
  XELOGI("winkey: Raw Input mouse captured.");
  NotifyCaptureState(true);
}

void WinKeyInputDriver::ReleaseRawMouseCapture() {
  if (!raw_mouse_capture_active_) {
    return;
  }

  ClipCursor(nullptr);
  window()->SetCursorVisibility(raw_mouse_previous_cursor_visibility_);
  window()->ReleaseMouse();
  raw_mouse_capture_active_ = false;
  RegisterRawMouse(false);
  XELOGI("winkey: Raw Input mouse released.");
  NotifyCaptureState(false);
}

void WinKeyInputDriver::RefreshRawMouseCapture() {
  if (!raw_mouse_capture_requested_) {
    return;
  }
  if (!raw_mouse_capture_active_) {
    ApplyRawMouseCapture();
    return;
  }
  if (!raw_mouse_registered_ || host_input_suspended_ ||
      !window()->HasFocus()) {
    return;
  }

  auto* win32_window = static_cast<ui::Win32Window*>(window());
  HWND hwnd = win32_window->hwnd();
  if (!hwnd) {
    return;
  }

  bool repaired_native_capture = false;
  if (GetCapture() != hwnd) {
    SetCapture(hwnd);
    if (GetCapture() != hwnd) {
      XELOGW("winkey: failed to restore Win32 mouse capture, error {}.",
             GetLastError());
      return;
    }
    repaired_native_capture = true;
  }

  RegisterRawMouse(true);
  window()->SetCursorVisibility(ui::Window::CursorVisibility::kHidden);
  if (!UpdateRawMouseClipRectangle()) {
    return;
  }
  if (repaired_native_capture) {
    XELOGI("winkey: restored Win32 mouse capture after a host window change.");
  }
}

void WinKeyInputDriver::NotifyCaptureState(bool active) {
  if (capture_state_callback_) {
    capture_state_callback_(active, GetSettings().raw_mouse_capture_toggle_key);
  }
}

bool WinKeyInputDriver::UpdateRawMouseClipRectangle() {
  auto* win32_window = static_cast<ui::Win32Window*>(window());
  HWND hwnd = win32_window->hwnd();
  RECT client_rect;
  if (!hwnd || !GetClientRect(hwnd, &client_rect)) {
    XELOGW(
        "winkey: failed to get the window rectangle for mouse capture, "
        "error {}.",
        GetLastError());
    return false;
  }

  POINT corners[] = {{client_rect.left, client_rect.top},
                     {client_rect.right, client_rect.bottom}};
  // MapWindowPoints also returns zero when the mapped origin is unchanged,
  // which is normal for a borderless fullscreen window at (0, 0). Clear the
  // thread's stale last-error value so zero can be distinguished from failure.
  SetLastError(ERROR_SUCCESS);
  if (!MapWindowPoints(hwnd, nullptr, corners, 2)) {
    const DWORD error = GetLastError();
    if (error) {
      XELOGW("winkey: failed to map the mouse capture rectangle, error {}.",
             error);
      return false;
    }
  }

  RECT clip_rect = {corners[0].x, corners[0].y, corners[1].x, corners[1].y};
  if (!ClipCursor(&clip_rect)) {
    XELOGW("winkey: failed to clip the mouse cursor, error {}.",
           GetLastError());
    return false;
  }
  return CenterRawMouseCursor();
}

bool WinKeyInputDriver::CenterRawMouseCursor() {
  auto* win32_window = static_cast<ui::Win32Window*>(window());
  HWND hwnd = win32_window->hwnd();
  RECT client_rect;
  if (!hwnd || !GetClientRect(hwnd, &client_rect)) {
    return false;
  }

  POINT center = {(client_rect.left + client_rect.right) / 2,
                  (client_rect.top + client_rect.bottom) / 2};
  if (!ClientToScreen(hwnd, &center) || !SetCursorPos(center.x, center.y)) {
    XELOGW("winkey: failed to center the captured mouse cursor, error {}.",
           GetLastError());
    return false;
  }
  return true;
}

InputType WinKeyInputDriver::GetInputType() const {
  WinKeySettings settings = GetSettings();
  switch (static_cast<KeyboardMode>(settings.keyboard_mode)) {
    case KeyboardMode::Disabled:
      return InputType::None;
    case KeyboardMode::Enabled:
      return InputType::Controller;
    case KeyboardMode::Passthrough:
      return InputType::Keyboard;
    default:
      break;
  }
  return InputType::Controller;
}

}  // namespace winkey
}  // namespace hid
}  // namespace xe
