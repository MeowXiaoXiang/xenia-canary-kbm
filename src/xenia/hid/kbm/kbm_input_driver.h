/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2022 Ben Vanik. All rights reserved.                             *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#ifndef XENIA_HID_KBM_KBM_INPUT_DRIVER_H_
#define XENIA_HID_KBM_KBM_INPUT_DRIVER_H_

#include <atomic>
#include <chrono>
#include <filesystem>
#include <functional>
#include <vector>

#include "xenia/base/mutex.h"
#include "xenia/hid/kbm/kbm_config.h"
#include "xenia/hid/kbm/kbm_controller_state.h"
#include "xenia/hid/kbm/kbm_input_report.h"
#include "xenia/hid/kbm/kbm_input_state.h"
#include "xenia/hid/kbm/kbm_mouse_processor.h"
#include "xenia/hid/kbm/kbm_platform_input.h"
#include "xenia/hid/keyboard/keyboard_input_driver.h"
#include "xenia/ui/virtual_key.h"

namespace xe {
namespace hid {
namespace kbm {

class KbmInputDriver final : public keyboard::KeyboardInputDriver,
                             public keyboard::KeyboardInputExtension {
 public:
  enum class BindingCaptureStatus { kNone, kCaptured, kCleared, kCancelled };

  struct BindingCaptureResult {
    BindingCaptureStatus status = BindingCaptureStatus::kNone;
    std::string value;
  };

  struct Diagnostics {
    bool raw_mouse_requested = false;
    bool raw_mouse_registered = false;
    bool capture_requested = false;
    bool capture_active = false;
    double raw_counts_per_second_x = 0.0;
    double raw_counts_per_second_y = 0.0;
    int16_t thumb_x = 0;
    int16_t thumb_y = 0;
  };

  struct InputSamplingStatus {
    bool active = false;
    bool report_write_failed = false;
    double seconds_remaining = 0.0;
    size_t sample_count = 0;
    size_t dropped_sample_count = 0;
    std::string report_path;
  };

  using CaptureStateCallback =
      std::function<void(bool active, const std::string& toggle_binding)>;

  explicit KbmInputDriver(xe::ui::Window* window, size_t window_z_order);
  ~KbmInputDriver() override;

  X_STATUS Setup() override;
  X_RESULT GetKeystroke(uint32_t user_index, uint32_t flags,
                        X_INPUT_KEYSTROKE* out_keystroke) override;

  void OnHostUIVisibilityChanged(bool visible) override {
    SetHostInputSuspended(visible);
  }

  KbmSettings GetSettings() const;
  void ApplySettings(const KbmSettings& settings);
  Diagnostics GetDiagnostics() const;
  void SetHostInputSuspended(bool suspended);
  void BeginBindingCapture();
  BindingCaptureResult ConsumeBindingCaptureResult();
  void CancelBindingCapture();
  void SetCaptureStateCallback(CaptureStateCallback callback);
  void RefreshRawMouseCapture();
  void StartInputSampling();
  bool StopInputSampling();
  void CancelInputSampling();
  InputSamplingStatus GetInputSamplingStatus() const;

 protected:
  struct KeyBinding {
    KbmInputCode input_code = KbmInputCode::kNone;
    KbmControl output_control = KbmControl::kA;
    bool uppercase = false;
    bool lowercase = false;
    bool shift = false;
    bool ctrl = false;
    bool alt = false;
    bool super = false;
    bool pressed = false;
  };

  void ParseKeyBinding(std::vector<KeyBinding>& bindings,
                       KbmControl output_control,
                       const std::string_view description,
                       const std::string_view binding);
  void RebuildKeyBindings(const KbmSettings& settings);
  void UpdateControllerKeystrokes(
      KbmInputCode changed_code = KbmInputCode::kNone, bool is_down = false,
      bool repeated = false);

  void OnKey(ui::KeyEvent& e, bool is_down) override;
  void OnMouseDown(ui::MouseEvent& e) override;
  void OnRawMouseMove(ui::RawMouseMoveEvent& e) override;
  bool IsControllerForUserEnabled(uint32_t user_index) const override;
  void ApplyGamepadState(uint32_t user_index,
                         X_INPUT_STATE* out_state) override;
  bool UsesGenericKeyboardMode() const override { return false; }
  bool CompleteBindingCapture(BindingCaptureStatus status,
                              std::string value = {});
  void ToggleRawMouseCapture();
  void NotifyCaptureState(bool active);
  void ResetRawMouseMotion();
  void ResetMouseInputState();
  void ResetInputState();
  KbmInputState GetInputStateSnapshot() const;
  void SetInputPressed(KbmInputCode code, bool pressed);
  void SetInputCapsLock(bool enabled);

  using InputSample = KbmInputSample;
  using RawInputSample = KbmRawInputSample;

  // Called under the motion lock so event order matches bucket consumption.
  void RecordRawInputSample(const RawInputSample& sample);

  void RecordInputSample(const InputSample& sample);
  bool FinishInputSampling();
  bool WriteInputSamplingReport(const std::vector<InputSample>& samples,
                                const std::vector<RawInputSample>& events,
                                size_t dropped_event_count,
                                const KbmSettings& settings,
                                std::chrono::steady_clock::time_point started,
                                size_t dropped_sample_count,
                                std::filesystem::path* report_path) const;

  xe::global_critical_region global_critical_region_;
  mutable xe::global_critical_region settings_critical_region_;
  mutable xe::global_critical_region input_state_critical_region_;
  bool binding_capture_active_ = false;
  BindingCaptureResult binding_capture_result_;
  std::vector<KeyBinding> key_bindings_;
  KbmInputState input_state_;
  std::deque<X_INPUT_KEYSTROKE> controller_keystrokes_;
  KbmSettings settings_;
  KbmPlatformInput platform_input_;
  // Couples motion arrival, bucket consumption and reset. Never acquire the
  // settings lock while holding this lock.
  xe::global_critical_region raw_mouse_motion_critical_region_;
  KbmMouseProcessor raw_mouse_processor_;
  KbmChord raw_mouse_capture_toggle_;
  CaptureStateCallback capture_state_callback_;
  std::atomic<bool> host_input_suspended_{false};
  std::atomic<double> raw_mouse_counts_per_second_x_{0.0};
  std::atomic<double> raw_mouse_counts_per_second_y_{0.0};
  std::atomic<int16_t> raw_mouse_thumb_x_{0};
  std::atomic<int16_t> raw_mouse_thumb_y_{0};

  mutable xe::global_critical_region input_sampling_critical_region_;
  bool input_sampling_active_ = false;
  bool input_sampling_report_write_failed_ = false;
  std::chrono::steady_clock::time_point input_sampling_started_;
  std::chrono::steady_clock::time_point input_sampling_deadline_;
  KbmSettings input_sampling_settings_;
  std::vector<InputSample> input_samples_;
  std::vector<RawInputSample> input_events_;
  size_t input_events_dropped_count_ = 0;
  size_t input_sampling_dropped_sample_count_ = 0;
  std::string input_sampling_report_path_;
};

}  // namespace kbm
}  // namespace hid
}  // namespace xe

#endif  // XENIA_HID_KBM_KBM_INPUT_DRIVER_H_
