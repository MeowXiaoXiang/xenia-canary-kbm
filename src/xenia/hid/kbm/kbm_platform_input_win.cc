/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2026 Xenia authors. All rights reserved.                         *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#include "xenia/hid/kbm/kbm_platform_input.h"

#include <utility>

#include "xenia/base/logging.h"
#include "xenia/base/platform_win.h"
#include "xenia/ui/window_win.h"

namespace xe::hid::kbm {

KbmPlatformInput::KbmPlatformInput(ui::Window& window, Callbacks callbacks)
    : window_(window),
      callbacks_(std::move(callbacks)),
      window_listener_(*this),
      last_center_time_(std::chrono::steady_clock::now()) {
  window_.AddListener(&window_listener_);
}

KbmPlatformInput::~KbmPlatformInput() {
  // KbmInputDriver performs its explicit shutdown while its callback targets
  // still exist. Do not invoke callbacks again during member destruction.
  callbacks_ = {};
  capture_requested_ = false;
  ReleaseCapture();
  UnregisterRawMouse();
  window_.RemoveListener(&window_listener_);
}

bool KbmPlatformInput::RegisterRawMouse(bool exclusive_capture) {
  const DWORD registration_flags =
      exclusive_capture ? RIDEV_NOLEGACY | RIDEV_CAPTUREMOUSE : 0;
  if (raw_mouse_registered_ &&
      raw_mouse_registration_flags_ == registration_flags) {
    return true;
  }

  auto* win32_window = static_cast<ui::Win32Window*>(&window_);
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
    ResetRawMouseMotion();
  }
  XELOGI("kbm: Raw Input mouse registered for right thumbstick ({}).",
         exclusive_capture ? "exclusive capture" : "foreground");
  return true;
}

void KbmPlatformInput::UnregisterRawMouse() {
  if (!raw_mouse_registered_) {
    return;
  }
  capture_requested_ = false;
  ReleaseCapture();

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
  ResetRawMouseMotion();
  XELOGI("kbm: Raw Input mouse unregistered.");
}

void KbmPlatformInput::SetInputSuspended(bool suspended) {
  input_suspended_ = suspended;
  if (suspended) {
    ReleaseCapture();
  } else {
    ApplyCapture();
  }
}

void KbmPlatformInput::ApplyCapture() {
  if (!capture_requested_ || capture_active_ || !raw_mouse_registered_ ||
      input_suspended_ || !window_.HasFocus()) {
    return;
  }

  auto* win32_window = static_cast<ui::Win32Window*>(&window_);
  if (!win32_window->hwnd()) {
    return;
  }

  // Suppress legacy mouse messages and prevent mouse buttons from activating
  // another window while captured. This is restored on release so host UI
  // controls continue to receive normal mouse messages.
  if (!RegisterRawMouse(true)) {
    XELOGW(
        "kbm: exclusive Raw Input registration failed; mouse capture was "
        "not activated.");
    return;
  }

  previous_cursor_visibility_ = window_.GetCursorVisibility();
  ResetRawMouseMotion();
  window_.CaptureMouse();
  window_.SetCursorVisibility(ui::Window::CursorVisibility::kHidden);
  if (!UpdateClipRectangle()) {
    window_.SetCursorVisibility(previous_cursor_visibility_);
    window_.ReleaseMouse();
    RegisterRawMouse(false);
    XELOGW("kbm: Raw Input mouse capture was not activated.");
    return;
  }

  last_center_time_ = std::chrono::steady_clock::now();
  capture_active_ = true;
  XELOGI("kbm: Raw Input mouse captured.");
  NotifyCaptureState(true);
}

void KbmPlatformInput::ReleaseCapture() {
  const bool was_active = capture_active_.exchange(false);
  // Do this after native state changes, so motion queued before release can
  // never become the first sample after a later capture restore.
  ResetRawMouseMotion();
  if (!was_active) {
    return;
  }

  ClipCursor(nullptr);
  window_.SetCursorVisibility(previous_cursor_visibility_);
  window_.ReleaseMouse();
  RegisterRawMouse(false);
  if (callbacks_.reset_mouse_buttons) {
    callbacks_.reset_mouse_buttons();
  }
  XELOGI("kbm: Raw Input mouse released.");
  NotifyCaptureState(false);
}

void KbmPlatformInput::RefreshCapture() {
  if (!capture_requested_) {
    return;
  }
  if (!capture_active_) {
    ApplyCapture();
    return;
  }
  if (!raw_mouse_registered_ || input_suspended_ || !window_.HasFocus()) {
    return;
  }

  auto* win32_window = static_cast<ui::Win32Window*>(&window_);
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
  window_.SetCursorVisibility(ui::Window::CursorVisibility::kHidden);
  if (!UpdateClipRectangle()) {
    return;
  }
  if (repaired_native_capture) {
    XELOGI("kbm: restored Win32 mouse capture after a host window change.");
  }
}

void KbmPlatformInput::RefreshCaptureIfDue(
    std::chrono::steady_clock::time_point now) {
  if (capture_active_ &&
      now - last_center_time_ >= std::chrono::milliseconds(50)) {
    RefreshCapture();
    last_center_time_ = now;
  }
}

void KbmPlatformInput::WindowListener::OnClosing(ui::UIEvent&) {
  platform_.SetCaptureRequested(false);
  platform_.ReleaseCapture();
}

void KbmPlatformInput::WindowListener::OnResize(ui::UISetupEvent&) {
  platform_.RefreshCapture();
}

void KbmPlatformInput::WindowListener::OnGotFocus(ui::UISetupEvent&) {
  platform_.ApplyCapture();
}

void KbmPlatformInput::WindowListener::OnLostFocus(ui::UISetupEvent&) {
  platform_.ReleaseCapture();
  if (platform_.callbacks_.reset_input_state) {
    platform_.callbacks_.reset_input_state();
  }
}

bool KbmPlatformInput::UpdateClipRectangle() {
  auto* win32_window = static_cast<ui::Win32Window*>(&window_);
  HWND hwnd = win32_window->hwnd();
  RECT client_rect;
  if (!hwnd || !GetClientRect(hwnd, &client_rect)) {
    XELOGW(
        "kbm: failed to get the window rectangle for mouse capture, error "
        "{}.",
        GetLastError());
    return false;
  }

  POINT corners[] = {{client_rect.left, client_rect.top},
                     {client_rect.right, client_rect.bottom}};
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
  return CenterCursor();
}

bool KbmPlatformInput::CenterCursor() {
  auto* win32_window = static_cast<ui::Win32Window*>(&window_);
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

void KbmPlatformInput::NotifyCaptureState(bool active) {
  if (callbacks_.capture_state_changed) {
    callbacks_.capture_state_changed(active);
  }
}

void KbmPlatformInput::ResetRawMouseMotion() {
  if (callbacks_.reset_raw_mouse_motion) {
    callbacks_.reset_raw_mouse_motion();
  }
}

}  // namespace xe::hid::kbm
