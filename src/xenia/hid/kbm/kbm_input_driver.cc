/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2022 Ben Vanik. All rights reserved.                             *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#include "xenia/hid/kbm/kbm_input_driver.h"

#include <algorithm>
#include <bitset>
#include <cmath>

#include "xenia/base/logging.h"
#include "xenia/base/platform_win.h"
#include "xenia/hid/input_system.h"
#include "xenia/hid/kbm/kbm_input_adapter.h"
#include "xenia/hid/kbm/kbm_input_report_writer_win.h"
#include "xenia/ui/virtual_key.h"
#include "xenia/ui/window.h"
#include "xenia/ui/windowed_app_context.h"

namespace xe {
namespace hid {
namespace kbm {

bool static IsKbmForUserEnabled(const KbmSettings& settings,
                                uint32_t user_index) {
  return settings.enabled && settings.user_index == user_index;
}

KbmMouseSettings GetMouseSettings(const KbmSettings& settings) {
  return {settings.raw_mouse_dpi, settings.raw_mouse_full_stick_speed_cm_s,
          settings.raw_mouse_deadzone_compensation,
          settings.raw_mouse_minimum_response, settings.raw_mouse_invert_y};
}

static bool ModifiersMatch(bool required_shift, bool required_ctrl,
                           bool required_alt, bool required_super, bool shift,
                           bool ctrl, bool alt, bool super) {
  return (!required_shift || shift) && (!required_ctrl || ctrl) &&
         (!required_alt || alt) && (!required_super || super);
}

static bool AreSettingsEqual(const KbmSettings& lhs, const KbmSettings& rhs) {
  if (lhs.enabled != rhs.enabled || lhs.user_index != rhs.user_index ||
      lhs.raw_mouse != rhs.raw_mouse ||
      lhs.raw_mouse_dpi != rhs.raw_mouse_dpi ||
      lhs.raw_mouse_full_stick_speed_cm_s !=
          rhs.raw_mouse_full_stick_speed_cm_s ||
      lhs.raw_mouse_deadzone_compensation !=
          rhs.raw_mouse_deadzone_compensation ||
      lhs.raw_mouse_minimum_response != rhs.raw_mouse_minimum_response ||
      lhs.raw_mouse_invert_y != rhs.raw_mouse_invert_y ||
      lhs.raw_mouse_capture_toggle_key != rhs.raw_mouse_capture_toggle_key ||
      lhs.raw_mouse_capture_on_start != rhs.raw_mouse_capture_on_start) {
    return false;
  }
#define XE_HID_KBM_BINDING(button, description, cvar_name, cvar_default_value) \
  if (lhs.cvar_name != rhs.cvar_name) {                                        \
    return false;                                                              \
  }
#include "xenia/hid/kbm/kbm_binding_table.inc"
#undef XE_HID_KBM_BINDING
  return true;
}

void KbmInputDriver::ParseKeyBinding(std::vector<KeyBinding>& bindings,
                                     KbmControl output_control,
                                     const std::string_view description,
                                     const std::string_view source_tokens) {
  for (const std::string_view source_token :
       utf8::split(source_tokens, " ", true)) {
    KeyBinding key_binding;
    key_binding.output_control = output_control;

    std::string_view token = source_token;

    if (utf8::starts_with(token, "_")) {
      key_binding.lowercase = true;
      token = token.substr(1);
    } else if (utf8::starts_with(token, "^")) {
      key_binding.uppercase = true;
      token = token.substr(1);
    }

    KbmChord chord;
    if (!ParseKbmChord(token, chord)) {
      XELOGW("kbm: failed to parse key \"{}\" for {}.", source_token,
             description);
      continue;
    }
    key_binding.input_code = chord.input;
    key_binding.shift = chord.shift;
    key_binding.ctrl = chord.ctrl;
    key_binding.alt = chord.alt;
    key_binding.super = chord.super;

    bindings.push_back(key_binding);
    XELOGI("kbm: \"{}\" binds {} to controller input {}.", source_token,
           FormatKbmInputCode(key_binding.input_code), description);
  }
}

void KbmInputDriver::RebuildKeyBindings(const KbmSettings& settings) {
  std::vector<KeyBinding> bindings;
#define XE_HID_KBM_BINDING(button, description, cvar_name, cvar_default_value) \
  ParseKeyBinding(bindings, KbmControl::k##button, description,                \
                  settings.cvar_name);
#include "kbm_binding_table.inc"
#undef XE_HID_KBM_BINDING
  key_bindings_ = std::move(bindings);
}

KbmInputDriver::KbmInputDriver(xe::ui::Window* window, size_t window_z_order)
    : KeyboardInputDriver(window, window_z_order, this),
      platform_input_(
          *window,
          {.reset_raw_mouse_motion = [this]() { ResetRawMouseMotion(); },
           .reset_mouse_buttons = [this]() { ResetMouseInputState(); },
           .reset_input_state = [this]() { ResetInputState(); },
           .capture_state_changed =
               [this](bool active) { NotifyCaptureState(active); }}),
      input_sampling_(WriteKbmInputSamplingReport) {
  settings_ = GetSettingsFromCvars();
  RebuildKeyBindings(settings_);
  ParseKbmChord(settings_.raw_mouse_capture_toggle_key,
                raw_mouse_capture_toggle_);
}

KbmInputDriver::~KbmInputDriver() {
  StopMousePreview();
  platform_input_.SetCaptureRequested(false);
  platform_input_.ReleaseCapture();
  platform_input_.UnregisterRawMouse();
}

X_STATUS KbmInputDriver::Setup() {
  KbmSettings settings = GetSettings();
  if (settings.raw_mouse && settings.enabled) {
    platform_input_.RegisterRawMouse();
  }
  if (platform_input_.raw_mouse_registered() &&
      settings.raw_mouse_capture_on_start) {
    platform_input_.SetCaptureRequested(true);
    window()->app_context().CallInUIThreadSynchronous(
        [this]() { platform_input_.ApplyCapture(); });
  }
  return X_STATUS_SUCCESS;
}

KbmSettings KbmInputDriver::GetSettings() const {
  auto lock = settings_critical_region_.Acquire();
  return settings_;
}

void KbmInputDriver::ApplySettings(const KbmSettings& source_settings) {
  KbmSettings settings = NormalizeSettings(source_settings);
  bool settings_changed = false;
  {
    auto lock = settings_critical_region_.Acquire();
    settings_changed = !AreSettingsEqual(settings_, settings);
  }
  // Saving an unchanged draft should not end an active report.
  if (settings_changed) {
    StopInputSampling();
  }

  {
    auto lock = settings_critical_region_.Acquire();
    settings_ = settings;
    RebuildKeyBindings(settings_);
    if (!ParseKbmChord(settings_.raw_mouse_capture_toggle_key,
                       raw_mouse_capture_toggle_)) {
      raw_mouse_capture_toggle_ = {};
      if (!settings_.raw_mouse_capture_toggle_key.empty()) {
        XELOGW("kbm: failed to parse Raw Input capture toggle \"{}\".",
               settings_.raw_mouse_capture_toggle_key);
      }
    }
  }
  if (settings_changed) {
    ResetRawMouseMotion();
  }
  ApplySettingsToCvars(settings);

  StopMousePreview();
  const bool should_register = settings.raw_mouse && settings.enabled;
  if (should_register) {
    platform_input_.RegisterRawMouse();
  } else {
    platform_input_.UnregisterRawMouse();
  }
}

KbmInputDriver::Diagnostics KbmInputDriver::GetDiagnostics() const {
  Diagnostics diagnostics;
  const KbmSettings settings = GetSettings();
  diagnostics.raw_mouse_requested = settings.enabled && settings.raw_mouse;
  diagnostics.raw_mouse_registered = platform_input_.raw_mouse_registered();
  diagnostics.capture_requested = platform_input_.capture_requested();
  diagnostics.capture_active = platform_input_.capture_active();
  diagnostics.raw_counts_per_second_x = raw_mouse_counts_per_second_x_;
  diagnostics.raw_counts_per_second_y = raw_mouse_counts_per_second_y_;
  diagnostics.thumb_x = raw_mouse_thumb_x_;
  diagnostics.thumb_y = raw_mouse_thumb_y_;
  return diagnostics;
}

bool KbmInputDriver::StartInputSampling() {
  auto report = input_sampling_.Prepare();
  if (!report) {
    return false;
  }
  bool started;
  {
    auto settings_lock = settings_critical_region_.Acquire();
    auto motion_lock = raw_mouse_motion_critical_region_.Acquire();
    const auto now = std::chrono::steady_clock::now();
    report->started = now;
    report->settings = {
        settings_.raw_mouse_dpi, settings_.raw_mouse_full_stick_speed_cm_s,
        settings_.raw_mouse_deadzone_compensation,
        settings_.raw_mouse_minimum_response, settings_.raw_mouse_invert_y};
    if (!ConfigPath().empty()) {
      report->directory = ConfigPath().parent_path();
    }
    RawInputSample initial;
    initial.time = now;
    initial.kind = 2;
    initial.sequence = raw_mouse_processor_.event_sequence();
    initial.reset_pending = raw_mouse_processor_.reset_pending();
    initial.delta_x = raw_mouse_processor_.pending_delta_x();
    initial.delta_y = raw_mouse_processor_.pending_delta_y();
    const auto velocity = raw_mouse_processor_.velocity();
    initial.velocity_x = velocity.x;
    initial.velocity_y = velocity.y;
    initial.previous_poll_offset_seconds =
        std::chrono::duration<double>(raw_mouse_processor_.last_sample_time() -
                                      now)
            .count();
    initial.capture_active = platform_input_.capture_active();
    initial.suspended = host_input_suspended_;
    report->events.push_back(initial);
    started = input_sampling_.Start(report);
  }
  if (started) {
    XELOGI("kbm: started a 60 second input sampling session.");
  }
  return started;
}

bool KbmInputDriver::StopInputSampling() {
  // Keep the manual cutoff ordered with in-flight controller/raw samples.
  // The worker does not acquire this region and performs all report I/O.
  auto lock = global_critical_region_.Acquire();
  return input_sampling_.Finish();
}

void KbmInputDriver::CancelInputSampling() {
  if (input_sampling_.Cancel()) {
    XELOGI("kbm: cancelled the input sampling session.");
  }
}

KbmInputDriver::InputSamplingStatus KbmInputDriver::GetInputSamplingStatus()
    const {
  return input_sampling_.GetStatus();
}

void KbmInputDriver::ShutdownInputSampling() {
  StopMousePreview();
  input_sampling_.Shutdown();
  XELOGI("kbm: input sampling shut down.");
}

bool KbmInputDriver::StartMousePreview() {
  CancelBindingCapture();
  if (!host_input_suspended_ || !platform_input_.BeginPreviewCapture()) {
    return false;
  }
  mouse_preview_.Start(std::chrono::steady_clock::now());
  return true;
}

void KbmInputDriver::StopMousePreview() {
  platform_input_.EndPreviewCapture();
  mouse_preview_.Stop();
}

void KbmInputDriver::ResetMousePreview() {
  mouse_preview_.Reset(std::chrono::steady_clock::now());
}

const KbmMousePreview& KbmInputDriver::SampleMousePreview(
    const KbmSettings& draft) {
  if (!platform_input_.preview_capture_active()) {
    mouse_preview_.Stop();
  }
  mouse_preview_.Sample(GetMouseSettings(NormalizeSettings(draft)),
                        std::chrono::steady_clock::now());
  return mouse_preview_;
}

void KbmInputDriver::SetHostInputSuspended(bool suspended) {
  if (!suspended) {
    StopMousePreview();
  }
  bool was_suspended;
  {
    auto lock = global_critical_region_.Acquire();
    was_suspended = host_input_suspended_.exchange(suspended);
    if (suspended || was_suspended) {
      // Host UI key events still reach the driver for binding capture. Clear
      // those keys before a controller poll can observe resumed input.
      ResetInputState();
    }
  }
  if (was_suspended != suspended) {
    XELOGI("kbm: host UI input suspended = {}.", suspended);
  }
  platform_input_.SetInputSuspended(suspended);
}

void KbmInputDriver::SetCaptureStateCallback(CaptureStateCallback callback) {
  capture_state_callback_ = std::move(callback);
  if (capture_state_callback_ && platform_input_.capture_active()) {
    NotifyCaptureState(true);
  }
}

void KbmInputDriver::RefreshRawMouseCapture() {
  platform_input_.RefreshCapture();
}

void KbmInputDriver::BeginBindingCapture(bool allow_mouse) {
  StopMousePreview();
  auto lock = global_critical_region_.Acquire();
  binding_capture_active_ = true;
  binding_capture_allow_mouse_ = allow_mouse;
  binding_capture_result_ = {};
}

KbmInputDriver::BindingCaptureResult
KbmInputDriver::ConsumeBindingCaptureResult() {
  auto lock = global_critical_region_.Acquire();
  BindingCaptureResult result = std::move(binding_capture_result_);
  binding_capture_result_ = {};
  return result;
}

void KbmInputDriver::CancelBindingCapture() {
  auto lock = global_critical_region_.Acquire();
  binding_capture_active_ = false;
  binding_capture_result_ = {};
}

bool KbmInputDriver::CompleteBindingCapture(BindingCaptureStatus status,
                                            std::string value) {
  auto lock = global_critical_region_.Acquire();
  if (!binding_capture_active_) {
    return false;
  }
  if (status == BindingCaptureStatus::kCaptured) {
    KbmChord chord;
    if (!ParseKbmChord(value, chord) ||
        (!binding_capture_allow_mouse_ && IsMouseInputCode(chord.input))) {
      return false;
    }
  }
  binding_capture_active_ = false;
  binding_capture_result_.status = status;
  binding_capture_result_.value = std::move(value);
  return true;
}

bool KbmInputDriver::IsControllerForUserEnabled(uint32_t user_index) const {
  return IsKbmForUserEnabled(GetSettings(), user_index);
}

X_RESULT KbmInputDriver::GetKeystroke(uint32_t user_index, uint32_t,
                                      X_INPUT_KEYSTROKE* out_keystroke) {
  UpdateControllerKeystrokes();
  auto settings_lock = settings_critical_region_.Acquire();
  if (!IsKbmForUserEnabled(settings_, user_index)) {
    return X_ERROR_DEVICE_NOT_CONNECTED;
  }
  auto lock = global_critical_region_.Acquire();
  if (host_input_suspended_ || !window()->HasFocus()) {
    controller_keystrokes_.clear();
  }
  if (controller_keystrokes_.empty()) {
    return X_ERROR_EMPTY;
  }
  *out_keystroke = controller_keystrokes_.front();
  controller_keystrokes_.pop_front();
  return X_ERROR_SUCCESS;
}

void KbmInputDriver::ApplyGamepadState(uint32_t, X_INPUT_STATE* out_state) {
  UpdateControllerKeystrokes();
  auto settings_lock = settings_critical_region_.Acquire();
  const KbmSettings& settings = settings_;

  KbmControllerState controller_state;

  if (window()->HasFocus() && !host_input_suspended_) {
    const bool mouse_buttons_enabled =
        platform_input_.capture_active() && !host_input_suspended_;
    std::bitset<256> applied_outputs;
    for (const KeyBinding& b : key_bindings_) {
      if (IsMouseInputCode(b.input_code) && !mouse_buttons_enabled) {
        continue;
      }
      if (b.pressed &&
          !applied_outputs.test(static_cast<uint8_t>(b.output_control))) {
        applied_outputs.set(static_cast<uint8_t>(b.output_control));
        ApplyKbmControl(controller_state, b.output_control);
      }
    }
  }

  InputSample input_sample;
  input_sample.time = std::chrono::steady_clock::now();
  input_sample.capture_active = platform_input_.capture_active();
  input_sample.input_suspended = host_input_suspended_;
  if (settings.raw_mouse && platform_input_.raw_mouse_registered() &&
      !platform_input_.preview_capture_active()) {
    auto motion_lock = raw_mouse_motion_critical_region_.Acquire();
    const bool raw_mouse_input_active = window()->HasFocus() &&
                                        platform_input_.capture_active() &&
                                        !host_input_suspended_;
    const KbmMouseSample mouse_sample = raw_mouse_processor_.Consume(
        GetMouseSettings(settings), raw_mouse_input_active,
        std::chrono::steady_clock::now());
    input_sample.time = mouse_sample.time;
    input_sample.event_sequence = mouse_sample.event_sequence;
    input_sample.elapsed_seconds = mouse_sample.elapsed_seconds;
    input_sample.raw_delta_x = mouse_sample.raw_delta_x;
    input_sample.raw_delta_y = mouse_sample.raw_delta_y;
    input_sample.velocity_x = mouse_sample.velocity_x;
    input_sample.velocity_y = mouse_sample.velocity_y;
    input_sample.thumb_x = mouse_sample.thumb_x;
    input_sample.thumb_y = mouse_sample.thumb_y;
    input_sample.input_active = mouse_sample.input_active;
    input_sample.reset_sample = mouse_sample.reset_sample;
    input_sample.stale_sample = mouse_sample.stale_sample;
    if (raw_mouse_input_active) {
      // Keep the last active sample available while a host dialog suspends
      // guest input, so the settings page can be used for calibration.
      if (mouse_sample.elapsed_seconds > 0.0) {
        raw_mouse_counts_per_second_x_ =
            double(mouse_sample.raw_delta_x) / mouse_sample.elapsed_seconds;
        raw_mouse_counts_per_second_y_ =
            double(mouse_sample.raw_delta_y) / mouse_sample.elapsed_seconds;
      }
      raw_mouse_thumb_x_ = mouse_sample.thumb_x;
      raw_mouse_thumb_y_ = mouse_sample.thumb_y;
    }
    AddKbmThumb(controller_state.thumb_rx, mouse_sample.thumb_x);
    AddKbmThumb(controller_state.thumb_ry, mouse_sample.thumb_y);
  }
  input_sampling_.Record(input_sample);

  out_state->gamepad.buttons = controller_state.buttons;
  out_state->gamepad.left_trigger = controller_state.left_trigger;
  out_state->gamepad.right_trigger = controller_state.right_trigger;
  out_state->gamepad.thumb_lx = controller_state.thumb_lx;
  out_state->gamepad.thumb_ly = controller_state.thumb_ly;
  out_state->gamepad.thumb_rx = controller_state.thumb_rx;
  out_state->gamepad.thumb_ry = controller_state.thumb_ry;
}

void KbmInputDriver::OnKey(ui::KeyEvent& e, bool is_down) {
  if (is_down && !e.prev_state() &&
      e.virtual_key() == ui::VirtualKey::kEscape &&
      platform_input_.preview_capture_active()) {
    StopMousePreview();
    e.set_handled(true);
    return;
  }
  const KbmInputCode input_code = ToKbmInputCode(e);
  const bool is_modifier = IsModifierInputCode(input_code);
  SetInputPressed(input_code, is_down);
  SetInputCapsLock(e.is_capital_pressed());
  const KbmModifiers modifiers = GetInputStateSnapshot().modifiers();
  if (is_down && !e.prev_state() && !is_modifier) {
    if (input_code == KbmInputCode::kEscape) {
      if (CompleteBindingCapture(BindingCaptureStatus::kCancelled)) {
        e.set_handled(true);
        return;
      }
    } else {
      KbmChord chord;
      chord.input = input_code;
      chord.shift = modifiers.shift;
      chord.ctrl = modifiers.ctrl;
      chord.alt = modifiers.alt;
      chord.super = modifiers.super;
      if (CompleteBindingCapture(BindingCaptureStatus::kCaptured,
                                 FormatKbmChord(chord))) {
        e.set_handled(true);
        return;
      }
    }
  } else if (!is_down && is_modifier) {
    KbmChord chord;
    chord.input = input_code;
    if (CompleteBindingCapture(BindingCaptureStatus::kCaptured,
                               FormatKbmChord(chord))) {
      e.set_handled(true);
      return;
    }
  }

  KbmSettings settings;
  KbmChord capture_toggle;
  {
    auto lock = settings_critical_region_.Acquire();
    settings = settings_;
    capture_toggle = raw_mouse_capture_toggle_;
  }
  if (is_down && !e.prev_state() &&
      capture_toggle.input != KbmInputCode::kNone &&
      input_code == capture_toggle.input) {
    XELOGI(
        "kbm: capture hotkey received; enabled={}, registered={}, "
        "UI suspended={}, shift={}, ctrl={}, alt={}.",
        settings.enabled, platform_input_.raw_mouse_registered(),
        host_input_suspended_.load(), modifiers.shift, modifiers.ctrl,
        modifiers.alt);
  }
  if (settings.raw_mouse && platform_input_.raw_mouse_registered() &&
      !host_input_suspended_ && capture_toggle.input != KbmInputCode::kNone &&
      input_code == capture_toggle.input &&
      ModifiersMatch(capture_toggle.shift, capture_toggle.ctrl,
                     capture_toggle.alt, capture_toggle.super, modifiers.shift,
                     modifiers.ctrl, modifiers.alt, modifiers.super)) {
    e.set_handled(true);
    if (is_down && !e.prev_state()) {
      ToggleRawMouseCapture();
    }
    return;
  }

  if (IsMouseInputCode(input_code) &&
      (!platform_input_.capture_active() || host_input_suspended_)) {
    return;
  }

  if (!settings.enabled || host_input_suspended_) {
    return;
  }
  UpdateControllerKeystrokes(input_code, is_down, e.prev_state());
}

void KbmInputDriver::UpdateControllerKeystrokes(KbmInputCode changed_code,
                                                bool is_down, bool repeated) {
  auto settings_lock = settings_critical_region_.Acquire();
  auto lock = global_critical_region_.Acquire();
  // Keep the snapshot and binding update together across lifecycle resets.
  const KbmInputState input_state = GetInputStateSnapshot();
  const KbmModifiers modifiers = input_state.modifiers();
  const bool active =
      settings_.enabled && !host_input_suspended_ && window()->HasFocus();
  const bool capital = modifiers.caps_lock || modifiers.shift;
  std::bitset<256> before, after, repeats;
  for (const auto& binding : key_bindings_) {
    if (binding.pressed) {
      before.set(static_cast<uint8_t>(binding.output_control));
    }
  }
  for (KeyBinding& binding : key_bindings_) {
    const bool modifiers_match = ModifiersMatch(
        binding.shift, binding.ctrl, binding.alt, binding.super,
        modifiers.shift, modifiers.ctrl, modifiers.alt, modifiers.super);
    const bool key_down = input_state.IsPressed(binding.input_code);
    binding.pressed =
        active && key_down && modifiers_match &&
        (!IsMouseInputCode(binding.input_code) ||
         platform_input_.capture_active()) &&
        ((binding.lowercase == binding.uppercase) ||
         (binding.lowercase && !capital) || (binding.uppercase && capital));
    const auto output = static_cast<uint8_t>(binding.output_control);
    if (binding.pressed) {
      after.set(output);
    }
    if (binding.pressed && changed_code == binding.input_code && is_down &&
        repeated) {
      repeats.set(output);
    }
  }
  for (size_t output = 0; output < before.size(); ++output) {
    if (before[output] == after[output] &&
        !(after[output] && repeats[output])) {
      continue;
    }
    X_INPUT_KEYSTROKE stroke = {};
    stroke.virtual_key = uint16_t(0x5800 + output);
    stroke.user_index = uint8_t(settings_.user_index);
    stroke.flags = !after[output] ? X_INPUT_KEYSTROKE_KEYUP
                   : before[output]
                       ? X_INPUT_KEYSTROKE_KEYDOWN | X_INPUT_KEYSTROKE_REPEAT
                       : X_INPUT_KEYSTROKE_KEYDOWN;
    if (controller_keystrokes_.size() >= 256) {
      controller_keystrokes_.pop_front();
    }
    controller_keystrokes_.push_back(stroke);
  }
}

void KbmInputDriver::OnMouseDown(ui::MouseEvent& e) {
  KbmInputCode input_code = KbmInputCode::kNone;
  switch (e.button()) {
    case ui::MouseEvent::Button::kLeft:
      input_code = KbmInputCode::kMouseLeft;
      break;
    case ui::MouseEvent::Button::kRight:
      input_code = KbmInputCode::kMouseRight;
      break;
    case ui::MouseEvent::Button::kMiddle:
      input_code = KbmInputCode::kMouseMiddle;
      break;
    case ui::MouseEvent::Button::kX1:
      input_code = KbmInputCode::kMouseX1;
      break;
    case ui::MouseEvent::Button::kX2:
      input_code = KbmInputCode::kMouseX2;
      break;
    default:
      return;
  }

  KbmChord chord;
  chord.input = input_code;
  const KbmModifiers modifiers = GetInputStateSnapshot().modifiers();
  chord.shift = modifiers.shift;
  chord.ctrl = modifiers.ctrl;
  chord.alt = modifiers.alt;
  chord.super = modifiers.super;
  if (CompleteBindingCapture(BindingCaptureStatus::kCaptured,
                             FormatKbmChord(chord))) {
    e.set_handled(true);
  }
}

void KbmInputDriver::OnRawMouseMove(ui::RawMouseMoveEvent& e) {
  if (platform_input_.preview_capture_active()) {
    mouse_preview_.AddDelta(e.delta_x(), e.delta_y(),
                            std::chrono::steady_clock::now());
    platform_input_.RefreshCaptureIfDue(std::chrono::steady_clock::now());
    return;
  }
  if (platform_input_.capture_active() && !host_input_suspended_) {
    const KbmInputCode buttons[] = {
        KbmInputCode::kMouseLeft, KbmInputCode::kMouseRight,
        KbmInputCode::kMouseMiddle, KbmInputCode::kMouseX1,
        KbmInputCode::kMouseX2};
    for (size_t i = 0; i < 5; ++i) {
      for (size_t up = 0; up < 2; ++up) {
        if (e.button_transitions() & (1u << (i * 2 + up))) {
          SetInputPressed(buttons[i], up == 0);
          UpdateControllerKeystrokes(buttons[i], up == 0, false);
        }
      }
    }
  }
  KbmSettings settings = GetSettings();
  if (!settings.raw_mouse || !settings.enabled ||
      !platform_input_.raw_mouse_registered() ||
      !platform_input_.capture_active() || host_input_suspended_ ||
      !window()->HasFocus()) {
    return;
  }

  {
    auto motion_lock = raw_mouse_motion_critical_region_.Acquire();
    // Release/reset can occur after the earlier eligibility check.
    if (!platform_input_.capture_active() || host_input_suspended_) {
      return;
    }
    RawInputSample sample;
    sample.time = std::chrono::steady_clock::now();
    raw_mouse_processor_.AddDelta(e.delta_x(), e.delta_y(), sample.time);
    sample.sequence = raw_mouse_processor_.event_sequence();
    sample.delta_x = e.delta_x();
    sample.delta_y = e.delta_y();
    sample.capture_active = platform_input_.capture_active();
    sample.suspended = host_input_suspended_;
    input_sampling_.Record(sample);
  }

  platform_input_.RefreshCaptureIfDue(std::chrono::steady_clock::now());
}

void KbmInputDriver::ToggleRawMouseCapture() {
  const bool requested = !platform_input_.capture_requested();
  platform_input_.SetCaptureRequested(requested);
  if (requested) {
    platform_input_.ApplyCapture();
  } else {
    platform_input_.ReleaseCapture();
  }
}

void KbmInputDriver::ResetRawMouseMotion() {
  auto motion_lock = raw_mouse_motion_critical_region_.Acquire();
  RawInputSample sample;
  sample.time = std::chrono::steady_clock::now();
  raw_mouse_processor_.Reset(sample.time);
  sample.kind = 1;
  sample.sequence = raw_mouse_processor_.event_sequence();
  sample.reset_pending = true;
  sample.capture_active = platform_input_.capture_active();
  sample.suspended = host_input_suspended_;
  input_sampling_.Record(sample);
}

void KbmInputDriver::ResetMouseInputState() {
  {
    auto input_lock = input_state_critical_region_.Acquire();
    input_state_.ResetMouseButtons();
  }
  UpdateControllerKeystrokes();
}

void KbmInputDriver::ResetInputState() {
  {
    auto settings_lock = settings_critical_region_.Acquire();
    auto lock = global_critical_region_.Acquire();
    controller_keystrokes_.clear();
    for (auto& binding : key_bindings_) {
      binding.pressed = false;
    }
  }
  auto input_lock = input_state_critical_region_.Acquire();
  input_state_.Reset();
}

KbmInputState KbmInputDriver::GetInputStateSnapshot() const {
  auto lock = input_state_critical_region_.Acquire();
  return input_state_;
}

void KbmInputDriver::SetInputPressed(KbmInputCode code, bool pressed) {
  auto lock = input_state_critical_region_.Acquire();
  input_state_.SetPressed(code, pressed);
}

void KbmInputDriver::SetInputCapsLock(bool enabled) {
  auto lock = input_state_critical_region_.Acquire();
  input_state_.SetCapsLock(enabled);
}

void KbmInputDriver::NotifyCaptureState(bool active) {
  if (capture_state_callback_) {
    capture_state_callback_(active, GetSettings().raw_mouse_capture_toggle_key);
  }
}

}  // namespace kbm
}  // namespace hid
}  // namespace xe
