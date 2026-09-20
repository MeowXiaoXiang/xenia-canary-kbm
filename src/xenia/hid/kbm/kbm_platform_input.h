/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2026 Xenia authors. All rights reserved.                         *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#ifndef XENIA_HID_KBM_KBM_PLATFORM_INPUT_H_
#define XENIA_HID_KBM_KBM_PLATFORM_INPUT_H_

#include <atomic>
#include <chrono>
#include <cstdint>
#include <functional>

#include "xenia/ui/window.h"
#include "xenia/ui/window_listener.h"

namespace xe::hid::kbm {

// Owns native Raw Input registration, cursor capture and window lifecycle.
// It deliberately only reports state transitions to the KBM driver: mapping,
// input state and report collection remain portable core concerns.
class KbmPlatformInput final {
 public:
  struct Callbacks {
    std::function<void()> reset_raw_mouse_motion;
    std::function<void()> reset_mouse_buttons;
    std::function<void()> reset_input_state;
    std::function<void(bool)> capture_state_changed;
  };

  KbmPlatformInput(ui::Window& window, Callbacks callbacks);
  ~KbmPlatformInput();

  bool RegisterRawMouse(bool exclusive_capture = false);
  void UnregisterRawMouse();

  void SetCaptureRequested(bool requested) { capture_requested_ = requested; }
  bool capture_requested() const { return capture_requested_; }
  bool capture_active() const { return capture_active_; }
  bool raw_mouse_registered() const { return raw_mouse_registered_; }

  void SetInputSuspended(bool suspended);
  void ApplyCapture();
  void ReleaseCapture();
  void RefreshCapture();
  void RefreshCaptureIfDue(std::chrono::steady_clock::time_point now);

 private:
  class WindowListener final : public ui::WindowListener {
   public:
    explicit WindowListener(KbmPlatformInput& platform) : platform_(platform) {}

    void OnClosing(ui::UIEvent& e) override;
    void OnResize(ui::UISetupEvent& e) override;
    void OnGotFocus(ui::UISetupEvent& e) override;
    void OnLostFocus(ui::UISetupEvent& e) override;

   private:
    KbmPlatformInput& platform_;
  };

  bool UpdateClipRectangle();
  bool CenterCursor();
  void NotifyCaptureState(bool active);
  void ResetRawMouseMotion();

  ui::Window& window_;
  Callbacks callbacks_;
  WindowListener window_listener_;
  std::chrono::steady_clock::time_point last_center_time_;
  ui::Window::CursorVisibility previous_cursor_visibility_ =
      ui::Window::CursorVisibility::kVisible;
  std::atomic<bool> raw_mouse_registered_{false};
  uint32_t raw_mouse_registration_flags_ = 0;
  std::atomic<bool> capture_requested_{false};
  std::atomic<bool> capture_active_{false};
  std::atomic<bool> input_suspended_{false};
};

}  // namespace xe::hid::kbm

#endif  // XENIA_HID_KBM_KBM_PLATFORM_INPUT_H_
