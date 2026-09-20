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
#include <filesystem>
#include <limits>

#include "xenia/base/filesystem.h"
#include "xenia/base/logging.h"
#include "xenia/base/platform_win.h"
#include "xenia/hid/hid_flags.h"
#include "xenia/hid/input_system.h"
#include "xenia/ui/virtual_key.h"
#include "xenia/ui/window.h"
#include "xenia/ui/window_win.h"
#include "xenia/ui/windowed_app_context.h"

#define XE_HID_KBM_BINDING(button, description, cvar_name, cvar_default_value) \
  DEFINE_transient_string(kbm_##cvar_name, cvar_default_value,                 \
                          "Keys or chords bound to " description               \
                          ", with alternatives separated by spaces",           \
                          "HID.KBM")
#include "kbm_binding_table.inc"
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

DEFINE_transient_double(raw_mouse_sensitivity, 10.0,
                        "Raw mouse sensitivity multiplier.", "HID.KBM");

DEFINE_transient_double(
    raw_mouse_full_scale_velocity, 24000.0,
    "Raw Input counts per second that produce full right-stick deflection "
    "before applying the response curve.",
    "HID.KBM");

DEFINE_transient_double(
    raw_mouse_response_curve, 0.8,
    "Raw mouse response exponent. 1 is linear, values above 1 add precision "
    "near the center, and values below 1 boost low speeds.",
    "HID.KBM");

DEFINE_transient_double(
    raw_mouse_smoothing_time_ms, 8.0,
    "Time-based smoothing applied to Raw Input mouse velocity. Zero disables "
    "smoothing.",
    "HID.KBM");

DEFINE_transient_bool(
    raw_mouse_deadzone_compensation, false,
    "Raise non-zero Raw Input mouse output above a configurable minimum "
    "response. Off preserves the original mouse-to-stick translation.",
    "HID.KBM");

DEFINE_transient_double(
    raw_mouse_minimum_response, 0.30,
    "Minimum absolute right-stick response applied only while Raw Input mouse "
    "movement is non-zero and deadzone compensation is enabled.",
    "HID.KBM");

DEFINE_transient_bool(raw_mouse_invert_y, false,
                      "Invert Raw Input mouse Y movement.", "HID.KBM");

DEFINE_transient_string(
    raw_mouse_capture_toggle_key, "F8",
    "Key or modifier chord used to toggle Raw Input mouse capture. Leave "
    "empty to disable the hotkey.",
    "HID.KBM");

DEFINE_transient_bool(
    raw_mouse_capture_on_start, false,
    "Capture and hide the mouse when Raw Input is initialized. The mouse is "
    "automatically released when Xenia loses focus.",
    "HID.KBM");

namespace xe {
namespace hid {
namespace kbm {

constexpr auto kInputSamplingDuration = std::chrono::seconds(60);
constexpr size_t kInputSamplingCapacity = 120000;
constexpr size_t kInputEventSamplingCapacity = 600000;

bool static IsKbmForUserEnabled(const KbmSettings& settings,
                                uint32_t user_index) {
  return settings.enabled && settings.user_index == user_index;
}

KbmMouseSettings GetMouseSettings(const KbmSettings& settings) {
  return {settings.raw_mouse_sensitivity,
          settings.raw_mouse_full_scale_velocity,
          settings.raw_mouse_response_curve,
          settings.raw_mouse_smoothing_time_ms,
          settings.raw_mouse_deadzone_compensation,
          settings.raw_mouse_minimum_response,
          settings.raw_mouse_invert_y};
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

static bool AreSettingsEqual(const KbmSettings& lhs, const KbmSettings& rhs) {
  if (lhs.enabled != rhs.enabled || lhs.user_index != rhs.user_index ||
      lhs.raw_mouse != rhs.raw_mouse ||
      lhs.raw_mouse_sensitivity != rhs.raw_mouse_sensitivity ||
      lhs.raw_mouse_full_scale_velocity != rhs.raw_mouse_full_scale_velocity ||
      lhs.raw_mouse_response_curve != rhs.raw_mouse_response_curve ||
      lhs.raw_mouse_smoothing_time_ms != rhs.raw_mouse_smoothing_time_ms ||
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

static int16_t AddThumbWithSaturation(int16_t left, int16_t right) {
  const int32_t sum = int32_t(left) + int32_t(right);
  return static_cast<int16_t>(
      std::clamp(sum, int32_t(std::numeric_limits<int16_t>::min()),
                 int32_t(std::numeric_limits<int16_t>::max())));
}

void KbmInputDriver::ParseKeyBinding(std::vector<KeyBinding>& bindings,
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

    KbmChord chord;
    if (!ParseKbmChord(token, chord)) {
      XELOGW("kbm: failed to parse key \"{}\" for {}.", source_token,
             description);
      continue;
    }
    key_binding.input_key = static_cast<ui::VirtualKey>(chord.virtual_key);
    key_binding.shift = chord.shift;
    key_binding.ctrl = chord.ctrl;
    key_binding.alt = chord.alt;
    key_binding.super = chord.super;

    bindings.push_back(key_binding);
    XELOGI("kbm: \"{}\" binds key 0x{:X} to controller input {}.", source_token,
           static_cast<uint16_t>(key_binding.input_key), description);
  }
}

void KbmInputDriver::RebuildKeyBindings(const KbmSettings& settings) {
  std::vector<KeyBinding> bindings;
#define XE_HID_KBM_BINDING(button, description, cvar_name, cvar_default_value) \
  ParseKeyBinding(bindings, xe::ui::VirtualKey::kXInputPad##button,            \
                  description, settings.cvar_name);
#include "kbm_binding_table.inc"
#undef XE_HID_KBM_BINDING
  key_bindings_ = std::move(bindings);
}

KbmInputDriver::KbmInputDriver(xe::ui::Window* window, size_t window_z_order)
    : KeyboardInputDriver(window, window_z_order, this),
      window_listener_(*this),
      raw_mouse_last_center_time_(std::chrono::steady_clock::now()) {
  settings_ = GetSettingsFromCvars();
  RebuildKeyBindings(settings_);
  ParseKbmChord(settings_.raw_mouse_capture_toggle_key,
                raw_mouse_capture_toggle_);

  window->AddListener(&window_listener_);
}

KbmInputDriver::~KbmInputDriver() {
  raw_mouse_capture_requested_ = false;
  ReleaseRawMouseCapture();
  UnregisterRawMouse();
  window()->RemoveListener(&window_listener_);
}

X_STATUS KbmInputDriver::Setup() {
  KbmSettings settings = GetSettings();
  if (settings.raw_mouse && settings.enabled) {
    RegisterRawMouse();
  }
  if (raw_mouse_registered_ && settings.raw_mouse_capture_on_start) {
    raw_mouse_capture_requested_ = true;
    window()->app_context().CallInUIThreadSynchronous(
        [this]() { ApplyRawMouseCapture(); });
  }
  return X_STATUS_SUCCESS;
}

bool KbmInputDriver::RegisterRawMouse(bool exclusive_capture) {
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
    XELOGE("kbm: failed to register Raw Input mouse, error {}.",
           GetLastError());
    return false;
  }

  const bool was_registered = raw_mouse_registered_.exchange(true);
  raw_mouse_registration_flags_ = registration_flags;
  if (!was_registered) {
    DiscardPendingRawMouseMotion();
  }
  XELOGI("kbm: Raw Input mouse registered for right thumbstick ({}).",
         exclusive_capture ? "exclusive capture" : "foreground");
  return true;
}

void KbmInputDriver::UnregisterRawMouse() {
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
    XELOGW("kbm: failed to unregister Raw Input mouse, error {}.",
           GetLastError());
  }
  raw_mouse_registered_ = false;
  raw_mouse_registration_flags_ = 0;
  DiscardPendingRawMouseMotion();
  raw_mouse_counts_per_second_x_ = 0.0;
  raw_mouse_counts_per_second_y_ = 0.0;
  raw_mouse_thumb_x_ = 0;
  raw_mouse_thumb_y_ = 0;
  XELOGI("kbm: Raw Input mouse unregistered.");
}

KbmSettings KbmInputDriver::GetSettings() const {
  auto lock = settings_critical_region_.Acquire();
  return settings_;
}

void KbmInputDriver::ApplySettings(const KbmSettings& source_settings) {
  KbmSettings settings = NormalizeSettings(source_settings);
  settings.user_index = std::clamp(settings.user_index, 0, 3);
  settings.raw_mouse_sensitivity =
      std::clamp(settings.raw_mouse_sensitivity, 0.01, 256.0);
  settings.raw_mouse_full_scale_velocity =
      std::clamp(settings.raw_mouse_full_scale_velocity, 1.0, 1000000.0);
  settings.raw_mouse_response_curve =
      std::clamp(settings.raw_mouse_response_curve, 0.1, 4.0);
  settings.raw_mouse_smoothing_time_ms =
      std::clamp(settings.raw_mouse_smoothing_time_ms, 0.0, 20.0);
  settings.raw_mouse_minimum_response =
      std::clamp(settings.raw_mouse_minimum_response, 0.0, 0.5);

  bool settings_changed = false;
  {
    auto lock = settings_critical_region_.Acquire();
    settings_changed = !AreSettingsEqual(settings_, settings);
  }
  // The dialog reapplies identical settings when it closes. A report should
  // end only when its mapping actually changes.
  if (settings_changed) {
    FinishInputSampling();
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
  ApplySettingsToCvars(settings);

  const bool should_register = settings.raw_mouse && settings.enabled;
  if (should_register) {
    RegisterRawMouse();
  } else {
    UnregisterRawMouse();
  }
}

KbmInputDriver::Diagnostics KbmInputDriver::GetDiagnostics() const {
  Diagnostics diagnostics;
  const KbmSettings settings = GetSettings();
  diagnostics.raw_mouse_requested = settings.enabled && settings.raw_mouse;
  diagnostics.raw_mouse_registered = raw_mouse_registered_;
  diagnostics.capture_requested = raw_mouse_capture_requested_;
  diagnostics.capture_active = raw_mouse_capture_active_;
  diagnostics.raw_counts_per_second_x = raw_mouse_counts_per_second_x_;
  diagnostics.raw_counts_per_second_y = raw_mouse_counts_per_second_y_;
  diagnostics.thumb_x = raw_mouse_thumb_x_;
  diagnostics.thumb_y = raw_mouse_thumb_y_;
  return diagnostics;
}

void KbmInputDriver::StartInputSampling() {
  std::vector<InputSample> samples;
  std::vector<RawInputSample> events;
  samples.reserve(kInputSamplingCapacity);
  events.reserve(kInputEventSamplingCapacity);
  auto settings_lock = settings_critical_region_.Acquire();
  auto motion_lock = raw_mouse_motion_critical_region_.Acquire();
  const auto now = std::chrono::steady_clock::now();
  const KbmSettings settings = settings_;
  auto lock = input_sampling_critical_region_.Acquire();
  input_sampling_active_ = true;
  input_sampling_report_write_failed_ = false;
  input_sampling_started_ = now;
  input_sampling_deadline_ = now + kInputSamplingDuration;
  input_sampling_settings_ = settings;
  input_samples_.swap(samples);
  input_events_.swap(events);
  input_events_dropped_count_ = 0;
  RawInputSample initial;
  initial.time = now;
  initial.kind = 2;
  initial.sequence = raw_mouse_processor_.event_sequence();
  initial.reset_pending = raw_mouse_processor_.reset_pending();
  initial.delta_x = raw_mouse_processor_.pending_delta_x();
  initial.delta_y = raw_mouse_processor_.pending_delta_y();
  const auto filtered_velocity = raw_mouse_processor_.filtered_velocity();
  initial.velocity_x = filtered_velocity.x;
  initial.velocity_y = filtered_velocity.y;
  initial.previous_poll_offset_seconds =
      std::chrono::duration<double>(raw_mouse_processor_.last_sample_time() -
                                    now)
          .count();
  initial.capture_active = raw_mouse_capture_active_;
  initial.suspended = host_input_suspended_;
  input_events_.push_back(initial);
  input_sampling_dropped_sample_count_ = 0;
  input_sampling_report_path_.clear();
  XELOGI(
      "kbm: started a {} second input sampling session.",
      std::chrono::duration_cast<std::chrono::seconds>(kInputSamplingDuration)
          .count());
}

bool KbmInputDriver::StopInputSampling() { return FinishInputSampling(); }

void KbmInputDriver::CancelInputSampling() {
  auto lock = input_sampling_critical_region_.Acquire();
  if (!input_sampling_active_) {
    return;
  }
  input_sampling_active_ = false;
  input_samples_.clear();
  input_events_.clear();
  input_events_dropped_count_ = 0;
  input_sampling_dropped_sample_count_ = 0;
  XELOGI("kbm: cancelled the input sampling session.");
}

KbmInputDriver::InputSamplingStatus KbmInputDriver::GetInputSamplingStatus()
    const {
  InputSamplingStatus status;
  auto lock = input_sampling_critical_region_.Acquire();
  status.active = input_sampling_active_;
  status.report_write_failed = input_sampling_report_write_failed_;
  status.sample_count = input_samples_.size();
  status.dropped_sample_count = input_sampling_dropped_sample_count_;
  status.report_path = input_sampling_report_path_;
  if (status.active) {
    status.seconds_remaining = std::max(
        0.0, std::chrono::duration<double>(input_sampling_deadline_ -
                                           std::chrono::steady_clock::now())
                 .count());
  }
  return status;
}

void KbmInputDriver::RecordInputSample(const InputSample& sample) {
  bool should_finish = false;
  {
    auto lock = input_sampling_critical_region_.Acquire();
    if (!input_sampling_active_ || sample.time < input_sampling_started_) {
      return;
    }
    if (input_samples_.size() < kInputSamplingCapacity) {
      input_samples_.push_back(sample);
    } else {
      ++input_sampling_dropped_sample_count_;
    }
    should_finish = sample.time >= input_sampling_deadline_;
  }
  if (should_finish) {
    FinishInputSampling();
  }
}

void KbmInputDriver::RecordRawInputSample(const RawInputSample& sample) {
  auto lock = input_sampling_critical_region_.Acquire();
  if (!input_sampling_active_ || sample.time < input_sampling_started_) {
    return;
  }
  if (input_events_.size() < kInputEventSamplingCapacity) {
    input_events_.push_back(sample);
  } else {
    ++input_events_dropped_count_;
  }
}

bool KbmInputDriver::FinishInputSampling() {
  std::vector<InputSample> samples;
  std::vector<RawInputSample> events;
  size_t dropped_event_count = 0;
  KbmSettings settings;
  std::chrono::steady_clock::time_point started;
  size_t dropped_sample_count = 0;
  {
    auto lock = input_sampling_critical_region_.Acquire();
    if (!input_sampling_active_) {
      return false;
    }
    input_sampling_active_ = false;
    samples = input_samples_;
    events = input_events_;
    dropped_event_count = input_events_dropped_count_;
    settings = input_sampling_settings_;
    started = input_sampling_started_;
    dropped_sample_count = input_sampling_dropped_sample_count_;
  }

  std::filesystem::path report_path;
  const bool written =
      WriteInputSamplingReport(samples, events, dropped_event_count, settings,
                               started, dropped_sample_count, &report_path);
  {
    auto lock = input_sampling_critical_region_.Acquire();
    if (input_sampling_started_ == started) {
      input_sampling_report_write_failed_ = !written;
      input_sampling_report_path_ =
          written ? xe::path_to_utf8(report_path) : "";
    }
  }
  if (written) {
    XELOGI("kbm: saved input sampling report '{}'.", report_path);
  }
  return written;
}

bool KbmInputDriver::WriteInputSamplingReport(
    const std::vector<InputSample>& samples,
    const std::vector<RawInputSample>& events, size_t dropped_event_count,
    const KbmSettings& settings, std::chrono::steady_clock::time_point started,
    size_t dropped_sample_count, std::filesystem::path* report_path) const {
  if (ConfigPath().empty() || !report_path) {
    return false;
  }

  const auto timestamp =
      std::chrono::duration_cast<std::chrono::microseconds>(
          std::chrono::system_clock::now().time_since_epoch())
          .count();
  *report_path = ConfigPath().parent_path() /
                 fmt::format("kbm-input-report-{}.csv", timestamp);

  const KbmInputReportContents report = SerializeKbmInputReport(
      samples, events, dropped_sample_count, dropped_event_count,
      {settings.raw_mouse_sensitivity, settings.raw_mouse_full_scale_velocity,
       settings.raw_mouse_response_curve, settings.raw_mouse_smoothing_time_ms,
       settings.raw_mouse_deadzone_compensation,
       settings.raw_mouse_minimum_response, settings.raw_mouse_invert_y},
      started);

  xe::filesystem::CreateParentFolder(*report_path);
  auto events_path = *report_path;
  events_path.replace_extension(".events.csv");
  auto events_temporary_path = events_path;
  events_temporary_path += ".tmp";
  FILE* event_file = xe::filesystem::OpenFile(events_temporary_path, "wb");
  if (!event_file) {
    return false;
  }
  const bool events_written =
      fwrite(report.events_csv.data(), 1, report.events_csv.size(),
             event_file) == report.events_csv.size();
  const bool events_closed = fclose(event_file) == 0;
  if (!events_written || !events_closed) {
    return false;
  }
  auto temporary_path = *report_path;
  temporary_path += ".tmp";
  FILE* file = xe::filesystem::OpenFile(temporary_path, "wb");
  if (!file) {
    XELOGE("kbm: failed to open input sampling report '{}' for writing.",
           *report_path);
    return false;
  }
  const bool written = fwrite(report.poll_csv.data(), 1, report.poll_csv.size(),
                              file) == report.poll_csv.size();
  const bool closed = fclose(file) == 0;
  // Publish the poll report last; it is the completion marker for the pair.
  if (!written || !closed ||
      !MoveFileExW(events_temporary_path.c_str(), events_path.c_str(),
                   MOVEFILE_WRITE_THROUGH) ||
      !MoveFileExW(temporary_path.c_str(), report_path->c_str(),
                   MOVEFILE_WRITE_THROUGH)) {
    std::error_code ignored;
    std::filesystem::remove(temporary_path, ignored);
    XELOGE("kbm: failed to write input sampling report '{}'.", *report_path);
    return false;
  }
  return true;
}

void KbmInputDriver::SetHostInputSuspended(bool suspended) {
  if (host_input_suspended_.exchange(suspended) != suspended) {
    XELOGI("kbm: host UI input suspended = {}.", suspended);
  }
  if (suspended) {
    {
      auto settings_lock = settings_critical_region_.Acquire();
      auto lock = global_critical_region_.Acquire();
      controller_keystrokes_.clear();
      for (auto& binding : key_bindings_) {
        binding.pressed = false;
      }
    }
    ReleaseRawMouseCapture();
  } else {
    ApplyRawMouseCapture();
  }
}

void KbmInputDriver::SetCaptureStateCallback(CaptureStateCallback callback) {
  capture_state_callback_ = std::move(callback);
  if (capture_state_callback_ && raw_mouse_capture_active_) {
    NotifyCaptureState(true);
  }
}

void KbmInputDriver::BeginBindingCapture() {
  auto lock = global_critical_region_.Acquire();
  binding_capture_active_ = true;
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

  uint16_t buttons = 0;
  uint8_t left_trigger = 0;
  uint8_t right_trigger = 0;
  int16_t thumb_lx = 0;
  int16_t thumb_ly = 0;
  int16_t thumb_rx = 0;
  int16_t thumb_ry = 0;

  if (window()->HasFocus() && !host_input_suspended_) {
    const bool mouse_buttons_enabled =
        raw_mouse_capture_active_ && !host_input_suspended_;
    std::bitset<256> applied_outputs;
    for (const KeyBinding& b : key_bindings_) {
      if (IsMouseVirtualKey(b.input_key) && !mouse_buttons_enabled) {
        continue;
      }
      if (b.pressed &&
          !applied_outputs.test(static_cast<uint16_t>(b.output_key) & 255)) {
        applied_outputs.set(static_cast<uint16_t>(b.output_key) & 255);
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

  InputSample input_sample;
  input_sample.time = std::chrono::steady_clock::now();
  input_sample.capture_active = raw_mouse_capture_active_;
  input_sample.input_suspended = host_input_suspended_;
  if (settings.raw_mouse && raw_mouse_registered_) {
    auto motion_lock = raw_mouse_motion_critical_region_.Acquire();
    const bool raw_mouse_input_active = window()->HasFocus() &&
                                        raw_mouse_capture_active_ &&
                                        !host_input_suspended_;
    const KbmMouseSample mouse_sample = raw_mouse_processor_.Consume(
        GetMouseSettings(settings), raw_mouse_input_active,
        std::chrono::steady_clock::now());
    input_sample.time = mouse_sample.time;
    input_sample.event_sequence = mouse_sample.event_sequence;
    input_sample.elapsed_seconds = mouse_sample.elapsed_seconds;
    input_sample.raw_delta_x = mouse_sample.raw_delta_x;
    input_sample.raw_delta_y = mouse_sample.raw_delta_y;
    input_sample.filtered_velocity_x = mouse_sample.filtered_velocity_x;
    input_sample.filtered_velocity_y = mouse_sample.filtered_velocity_y;
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
    thumb_rx = AddThumbWithSaturation(thumb_rx, mouse_sample.thumb_x);
    thumb_ry = AddThumbWithSaturation(thumb_ry, mouse_sample.thumb_y);
  }
  RecordInputSample(input_sample);

  out_state->gamepad.buttons = buttons;
  out_state->gamepad.left_trigger = left_trigger;
  out_state->gamepad.right_trigger = right_trigger;
  out_state->gamepad.thumb_lx = thumb_lx;
  out_state->gamepad.thumb_ly = thumb_ly;
  out_state->gamepad.thumb_rx = thumb_rx;
  out_state->gamepad.thumb_ry = thumb_ry;
}

void KbmInputDriver::KbmWindowListener::OnClosing(ui::UIEvent& e) {
  driver_.raw_mouse_capture_requested_ = false;
  driver_.ReleaseRawMouseCapture();
}

void KbmInputDriver::KbmWindowListener::OnResize(ui::UISetupEvent& e) {
  if (driver_.raw_mouse_capture_active_) {
    driver_.RefreshRawMouseCapture();
  }
}

void KbmInputDriver::KbmWindowListener::OnGotFocus(ui::UISetupEvent& e) {
  driver_.ApplyRawMouseCapture();
}

void KbmInputDriver::KbmWindowListener::OnLostFocus(ui::UISetupEvent& e) {
  {
    auto settings_lock = driver_.settings_critical_region_.Acquire();
    auto lock = driver_.global_critical_region_.Acquire();
    driver_.controller_keystrokes_.clear();
    for (auto& binding : driver_.key_bindings_) {
      binding.pressed = false;
    }
  }
  driver_.ReleaseRawMouseCapture();
}

void KbmInputDriver::OnKey(ui::KeyEvent& e, bool is_down) {
  const bool is_modifier = IsModifierVirtualKey(e.virtual_key());
  if (is_down && !e.prev_state() && !is_modifier) {
    if (e.virtual_key() == ui::VirtualKey::kEscape) {
      if (CompleteBindingCapture(BindingCaptureStatus::kCancelled)) {
        e.set_handled(true);
        return;
      }
    } else {
      KbmChord chord;
      chord.virtual_key = static_cast<uint16_t>(e.virtual_key());
      chord.shift = e.is_shift_pressed();
      chord.ctrl = e.is_ctrl_pressed();
      chord.alt = e.is_alt_pressed();
      chord.super = IsKeyDown(VK_LWIN) || IsKeyDown(VK_RWIN);
      if (CompleteBindingCapture(BindingCaptureStatus::kCaptured,
                                 FormatKbmChord(chord))) {
        e.set_handled(true);
        return;
      }
    }
  } else if (!is_down && is_modifier) {
    KbmChord chord;
    chord.virtual_key = static_cast<uint16_t>(e.virtual_key());
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
  if (is_down && !e.prev_state() && capture_toggle.virtual_key &&
      static_cast<uint16_t>(e.virtual_key()) == capture_toggle.virtual_key) {
    XELOGI(
        "kbm: capture hotkey received; enabled={}, registered={}, "
        "UI suspended={}, shift={}, ctrl={}, alt={}.",
        settings.enabled, raw_mouse_registered_.load(),
        host_input_suspended_.load(), e.is_shift_pressed(), e.is_ctrl_pressed(),
        e.is_alt_pressed());
  }
  if (settings.raw_mouse && raw_mouse_registered_ && !host_input_suspended_ &&
      capture_toggle.virtual_key &&
      e.virtual_key() ==
          static_cast<ui::VirtualKey>(capture_toggle.virtual_key) &&
      ModifiersMatch(
          capture_toggle.shift, capture_toggle.ctrl, capture_toggle.alt,
          capture_toggle.super, e.is_shift_pressed(), e.is_ctrl_pressed(),
          e.is_alt_pressed(), IsKeyDown(VK_LWIN) || IsKeyDown(VK_RWIN))) {
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

  if (!settings.enabled || host_input_suspended_) {
    return;
  }
  UpdateControllerKeystrokes(&e, is_down);
}

void KbmInputDriver::UpdateControllerKeystrokes(ui::KeyEvent* event,
                                                bool is_down) {
  auto settings_lock = settings_critical_region_.Acquire();
  auto lock = global_critical_region_.Acquire();
  const bool shift = event ? event->is_shift_pressed() : IsKeyDown(VK_SHIFT);
  const bool ctrl = event ? event->is_ctrl_pressed() : IsKeyDown(VK_CONTROL);
  const bool alt = event ? event->is_alt_pressed() : IsKeyDown(VK_MENU);
  const bool active =
      settings_.enabled && !host_input_suspended_ && window()->HasFocus();
  const bool capital = IsKeyToggled(VK_CAPITAL) || shift;
  std::bitset<256> before, after, repeats;
  for (const auto& binding : key_bindings_) {
    if (binding.pressed) {
      before.set(static_cast<uint16_t>(binding.output_key) & 255);
    }
  }
  for (KeyBinding& binding : key_bindings_) {
    const bool modifiers_match = ModifiersMatch(
        binding.shift, binding.ctrl, binding.alt, binding.super, shift, ctrl,
        alt, IsKeyDown(VK_LWIN) || IsKeyDown(VK_RWIN));
    const bool key_down = event && binding.input_key == event->virtual_key()
                              ? is_down
                              : IsKeyDown(binding.input_key);
    binding.pressed =
        active && key_down && modifiers_match &&
        (!IsMouseVirtualKey(binding.input_key) || raw_mouse_capture_active_) &&
        ((binding.lowercase == binding.uppercase) ||
         (binding.lowercase && !capital) || (binding.uppercase && capital));
    const auto output = static_cast<uint16_t>(binding.output_key) & 255;
    if (binding.pressed) {
      after.set(output);
    }
    if (binding.pressed && event && is_down && event->prev_state() &&
        binding.input_key == event->virtual_key()) {
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

  KbmChord chord;
  chord.virtual_key = virtual_key;
  chord.shift = IsKeyDown(VK_SHIFT);
  chord.ctrl = IsKeyDown(VK_CONTROL);
  chord.alt = IsKeyDown(VK_MENU);
  chord.super = IsKeyDown(VK_LWIN) || IsKeyDown(VK_RWIN);
  if (CompleteBindingCapture(BindingCaptureStatus::kCaptured,
                             FormatKbmChord(chord))) {
    e.set_handled(true);
  }
}

void KbmInputDriver::OnRawMouseMove(ui::RawMouseMoveEvent& e) {
  if (raw_mouse_capture_active_ && !host_input_suspended_) {
    const ui::VirtualKey buttons[] = {
        ui::VirtualKey::kLButton, ui::VirtualKey::kRButton,
        ui::VirtualKey::kMButton, ui::VirtualKey::kXButton1,
        ui::VirtualKey::kXButton2};
    for (size_t i = 0; i < 5; ++i) {
      for (size_t up = 0; up < 2; ++up) {
        if (e.button_transitions() & (1u << (i * 2 + up))) {
          ui::KeyEvent button_event(window(), buttons[i], 1, up != 0,
                                    IsKeyDown(VK_SHIFT), IsKeyDown(VK_CONTROL),
                                    IsKeyDown(VK_MENU),
                                    IsKeyToggled(VK_CAPITAL));
          UpdateControllerKeystrokes(&button_event, up == 0);
        }
      }
    }
  }
  KbmSettings settings = GetSettings();
  if (!settings.raw_mouse || !settings.enabled || !raw_mouse_registered_ ||
      !raw_mouse_capture_active_ || host_input_suspended_ ||
      !window()->HasFocus()) {
    return;
  }

  {
    auto motion_lock = raw_mouse_motion_critical_region_.Acquire();
    // Release/reset can occur after the earlier eligibility check.
    if (!raw_mouse_capture_active_ || host_input_suspended_) {
      return;
    }
    RawInputSample sample;
    sample.time = std::chrono::steady_clock::now();
    raw_mouse_processor_.AddDelta(e.delta_x(), e.delta_y(), sample.time);
    sample.sequence = raw_mouse_processor_.event_sequence();
    sample.delta_x = e.delta_x();
    sample.delta_y = e.delta_y();
    sample.capture_active = raw_mouse_capture_active_;
    sample.suspended = host_input_suspended_;
    RecordRawInputSample(sample);
  }

  const auto now = std::chrono::steady_clock::now();
  if (now - raw_mouse_last_center_time_ >= std::chrono::milliseconds(50)) {
    // Reassert the Win32 capture, cursor state and clip. Fullscreen changes or
    // injected overlays may disturb any of them while Xenia still has focus.
    RefreshRawMouseCapture();
    raw_mouse_last_center_time_ = now;
  }
}

void KbmInputDriver::ToggleRawMouseCapture() {
  raw_mouse_capture_requested_ = !raw_mouse_capture_requested_;
  if (raw_mouse_capture_requested_) {
    ApplyRawMouseCapture();
  } else {
    ReleaseRawMouseCapture();
  }
}

void KbmInputDriver::ApplyRawMouseCapture() {
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
        "kbm: exclusive Raw Input registration failed; mouse capture "
        "was not activated.");
    return;
  }

  raw_mouse_previous_cursor_visibility_ = window()->GetCursorVisibility();
  DiscardPendingRawMouseMotion();
  window()->CaptureMouse();
  window()->SetCursorVisibility(ui::Window::CursorVisibility::kHidden);
  if (!UpdateRawMouseClipRectangle()) {
    window()->SetCursorVisibility(raw_mouse_previous_cursor_visibility_);
    window()->ReleaseMouse();
    RegisterRawMouse(false);
    XELOGW("kbm: Raw Input mouse capture was not activated.");
    return;
  }

  raw_mouse_last_center_time_ = std::chrono::steady_clock::now();
  raw_mouse_capture_active_ = true;
  XELOGI("kbm: Raw Input mouse captured.");
  NotifyCaptureState(true);
}

void KbmInputDriver::ReleaseRawMouseCapture() {
  const bool was_active = raw_mouse_capture_active_.exchange(false);
  // Input polling may stop while focus or a host dialog suspends guest input.
  // Never let motion queued before the release become the first sample after
  // capture is restored.
  DiscardPendingRawMouseMotion();
  if (!was_active) {
    return;
  }

  ClipCursor(nullptr);
  window()->SetCursorVisibility(raw_mouse_previous_cursor_visibility_);
  window()->ReleaseMouse();
  RegisterRawMouse(false);
  XELOGI("kbm: Raw Input mouse released.");
  NotifyCaptureState(false);
}

void KbmInputDriver::DiscardPendingRawMouseMotion() {
  auto motion_lock = raw_mouse_motion_critical_region_.Acquire();
  RawInputSample sample;
  sample.time = std::chrono::steady_clock::now();
  raw_mouse_processor_.Reset(sample.time);
  sample.kind = 1;
  sample.sequence = raw_mouse_processor_.event_sequence();
  sample.reset_pending = true;
  sample.capture_active = raw_mouse_capture_active_;
  sample.suspended = host_input_suspended_;
  RecordRawInputSample(sample);
}

void KbmInputDriver::RefreshRawMouseCapture() {
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
      XELOGW("kbm: failed to restore Win32 mouse capture, error {}.",
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
    XELOGI("kbm: restored Win32 mouse capture after a host window change.");
  }
}

void KbmInputDriver::NotifyCaptureState(bool active) {
  if (capture_state_callback_) {
    capture_state_callback_(active, GetSettings().raw_mouse_capture_toggle_key);
  }
}

bool KbmInputDriver::UpdateRawMouseClipRectangle() {
  auto* win32_window = static_cast<ui::Win32Window*>(window());
  HWND hwnd = win32_window->hwnd();
  RECT client_rect;
  if (!hwnd || !GetClientRect(hwnd, &client_rect)) {
    XELOGW(
        "kbm: failed to get the window rectangle for mouse capture, "
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
      XELOGW("kbm: failed to map the mouse capture rectangle, error {}.",
             error);
      return false;
    }
  }

  RECT clip_rect = {corners[0].x, corners[0].y, corners[1].x, corners[1].y};
  if (!ClipCursor(&clip_rect)) {
    XELOGW("kbm: failed to clip the mouse cursor, error {}.", GetLastError());
    return false;
  }
  return CenterRawMouseCursor();
}

bool KbmInputDriver::CenterRawMouseCursor() {
  auto* win32_window = static_cast<ui::Win32Window*>(window());
  HWND hwnd = win32_window->hwnd();
  RECT client_rect;
  if (!hwnd || !GetClientRect(hwnd, &client_rect)) {
    return false;
  }

  POINT center = {(client_rect.left + client_rect.right) / 2,
                  (client_rect.top + client_rect.bottom) / 2};
  if (!ClientToScreen(hwnd, &center) || !SetCursorPos(center.x, center.y)) {
    XELOGW("kbm: failed to center the captured mouse cursor, error {}.",
           GetLastError());
    return false;
  }
  return true;
}

}  // namespace kbm
}  // namespace hid
}  // namespace xe
