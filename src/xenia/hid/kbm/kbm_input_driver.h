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
#include <queue>

#include "xenia/base/mutex.h"
#include "xenia/hid/input_driver.h"
#include "xenia/hid/kbm/kbm_config.h"
#include "xenia/ui/virtual_key.h"
#include "xenia/ui/window_listener.h"

namespace xe {
namespace hid {
namespace kbm {

class KbmInputDriver final : public InputDriver {
 public:
  enum class BindingCaptureStatus { kNone, kCaptured, kCleared, kCancelled };

  struct BindingCaptureResult {
    BindingCaptureStatus status = BindingCaptureStatus::kNone;
    std::string value;
  };

  struct Diagnostics {
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

  X_RESULT GetCapabilities(uint32_t user_index, uint32_t flags,
                           X_INPUT_CAPABILITIES* out_caps) override;
  X_RESULT GetState(uint32_t user_index, X_INPUT_STATE* out_state) override;
  X_RESULT SetState(uint32_t user_index, X_INPUT_VIBRATION* vibration) override;
  X_RESULT GetKeystroke(uint32_t user_index, uint32_t flags,
                        X_INPUT_KEYSTROKE* out_keystroke) override;
  virtual InputType GetInputType() const override;
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
  struct KeyEvent {
    ui::VirtualKey virtual_key = ui::VirtualKey::kNone;
    int repeat_count = 0;
    bool transition = false;  // going up(false) or going down(true)
    bool prev_state = false;  // down(true) or up(false)
    bool shift = false;
    bool ctrl = false;
    bool alt = false;
    bool super = false;
  };

  struct KeyBinding {
    ui::VirtualKey input_key = ui::VirtualKey::kNone;
    ui::VirtualKey output_key = ui::VirtualKey::kNone;
    bool uppercase = false;
    bool lowercase = false;
    bool shift = false;
    bool ctrl = false;
    bool alt = false;
    bool super = false;
  };

  class KbmWindowInputListener final : public ui::WindowInputListener {
   public:
    explicit KbmWindowInputListener(KbmInputDriver& driver)
        : driver_(driver) {}

    void OnKeyDown(ui::KeyEvent& e) override;
    void OnKeyUp(ui::KeyEvent& e) override;
    void OnMouseDown(ui::MouseEvent& e) override;
    void OnRawMouseMove(ui::RawMouseMoveEvent& e) override;

   private:
    KbmInputDriver& driver_;
  };

  class KbmWindowListener final : public ui::WindowListener {
   public:
    explicit KbmWindowListener(KbmInputDriver& driver)
        : driver_(driver) {}

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

  void OnKey(ui::KeyEvent& e, bool is_down);
  void OnMouseDown(ui::MouseEvent& e);
  void OnRawMouseMove(ui::RawMouseMoveEvent& e);
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

  KbmWindowInputListener window_input_listener_;
  KbmWindowListener window_listener_;

  xe::global_critical_region global_critical_region_;
  mutable xe::global_critical_region settings_critical_region_;
  std::queue<KeyEvent> key_events_;
  bool binding_capture_active_ = false;
  BindingCaptureResult binding_capture_result_;
  std::vector<KeyBinding> key_bindings_;
  KbmSettings settings_;
  uint8_t key_map_[256];
  uint32_t packet_number_ = 1;

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
