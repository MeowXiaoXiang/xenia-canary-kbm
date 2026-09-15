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
#include <functional>

#include "xenia/base/mutex.h"
#include "xenia/hid/kbm/kbm_config.h"
#include "xenia/hid/keyboard/keyboard_input_driver.h"
#include "xenia/ui/virtual_key.h"
#include "xenia/ui/window_listener.h"

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

 protected:
  struct KeyBinding {
    ui::VirtualKey input_key = ui::VirtualKey::kNone;
    ui::VirtualKey output_key = ui::VirtualKey::kNone;
    bool uppercase = false;
    bool lowercase = false;
    bool shift = false;
    bool ctrl = false;
    bool alt = false;
    bool super = false;
    bool pressed = false;
  };

  class KbmWindowListener final : public ui::WindowListener {
   public:
    explicit KbmWindowListener(KbmInputDriver& driver) : driver_(driver) {}

    void OnClosing(ui::UIEvent& e) override;
    void OnResize(ui::UISetupEvent& e) override;
    void OnGotFocus(ui::UISetupEvent& e) override;
    void OnLostFocus(ui::UISetupEvent& e) override;

   private:
    KbmInputDriver& driver_;
  };

  void ParseKeyBinding(std::vector<KeyBinding>& bindings,
                       ui::VirtualKey virtual_key,
                       const std::string_view description,
                       const std::string_view binding);
  void RebuildKeyBindings(const KbmSettings& settings);
  void UpdateControllerKeystrokes(ui::KeyEvent* event = nullptr,
                                  bool is_down = false);

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
  void ApplyRawMouseCapture();
  void ReleaseRawMouseCapture();
  bool UpdateRawMouseClipRectangle();
  bool CenterRawMouseCursor();
  bool RegisterRawMouse(bool exclusive_capture = false);
  void UnregisterRawMouse();
  void NotifyCaptureState(bool active);

  KbmWindowListener window_listener_;

  xe::global_critical_region global_critical_region_;
  mutable xe::global_critical_region settings_critical_region_;
  bool binding_capture_active_ = false;
  BindingCaptureResult binding_capture_result_;
  std::vector<KeyBinding> key_bindings_;
  std::deque<X_INPUT_KEYSTROKE> controller_keystrokes_;
  KbmSettings settings_;
  std::atomic<int64_t> raw_mouse_delta_x_{0};
  std::atomic<int64_t> raw_mouse_delta_y_{0};
  std::chrono::steady_clock::time_point raw_mouse_last_sample_time_;
  std::chrono::steady_clock::time_point raw_mouse_last_center_time_;
  KbmChord raw_mouse_capture_toggle_;
  ui::Window::CursorVisibility raw_mouse_previous_cursor_visibility_ =
      ui::Window::CursorVisibility::kVisible;
  std::atomic<bool> raw_mouse_registered_{false};
  DWORD raw_mouse_registration_flags_ = 0;
  std::atomic<bool> raw_mouse_capture_requested_{false};
  std::atomic<bool> raw_mouse_capture_active_{false};
  CaptureStateCallback capture_state_callback_;
  std::atomic<bool> host_input_suspended_{false};
  std::atomic<double> raw_mouse_counts_per_second_x_{0.0};
  std::atomic<double> raw_mouse_counts_per_second_y_{0.0};
  std::atomic<int16_t> raw_mouse_thumb_x_{0};
  std::atomic<int16_t> raw_mouse_thumb_y_{0};
};

}  // namespace kbm
}  // namespace hid
}  // namespace xe

#endif  // XENIA_HID_KBM_KBM_INPUT_DRIVER_H_
