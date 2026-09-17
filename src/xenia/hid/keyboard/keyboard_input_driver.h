/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2026 Xenia Canary. All rights reserved.                          *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#ifndef XENIA_HID_KEYBOARD_KEYBOARD_INPUT_DRIVER_H_
#define XENIA_HID_KEYBOARD_KEYBOARD_INPUT_DRIVER_H_

#include <queue>

#include "xenia/base/mutex.h"
#include "xenia/hid/input_driver.h"
#include "xenia/ui/virtual_key.h"
#include "xenia/ui/window_listener.h"

namespace xe {
namespace hid {
namespace keyboard {

enum class KeyboardMode { Disabled, Enabled, Passthrough };

// Optional platform-specific input behavior that shares the keyboard driver's
// listener. Extensions must not install a competing WindowInputListener.
class KeyboardInputExtension {
 public:
  virtual ~KeyboardInputExtension() = default;

  virtual void OnKey(ui::KeyEvent& e, bool is_down) {}
  virtual void OnMouseDown(ui::MouseEvent& e) {}
  virtual void OnRawMouseMove(ui::RawMouseMoveEvent& e) {}
  virtual void OnHostUIVisibilityChanged(bool visible) {}

  virtual bool IsControllerForUserEnabled(uint32_t user_index) const {
    return false;
  }
  virtual void ApplyGamepadState(uint32_t user_index,
                                 X_INPUT_STATE* out_state) {}
  virtual bool UsesGenericKeyboardMode() const { return true; }
};

class KeyboardInputDriver : public InputDriver {
 public:
  explicit KeyboardInputDriver(xe::ui::Window* window, size_t window_z_order,
                               KeyboardInputExtension* extension = nullptr);
  ~KeyboardInputDriver() override;

  uint8_t VirtualKeyToHIDUsage(uint16_t vk) const;

  bool IsPassthroughEnabled();

  bool IsKeyboardForUserEnabled(uint32_t user_index);

  X_STATUS Setup() override;

  X_RESULT GetCapabilities(uint32_t user_index, uint32_t flags,
                           X_INPUT_CAPABILITIES* out_caps) override;
  X_RESULT GetState(uint32_t user_index, X_INPUT_STATE* out_state) override;
  X_RESULT SetState(uint32_t user_index, X_INPUT_VIBRATION* vibration) override;

  X_RESULT GetKeystroke(uint32_t user_index, uint32_t flags,
                        X_INPUT_KEYSTROKE* out_keystroke) override;

  virtual InputType GetInputType() const override;
  void OnHostUIVisibilityChanged(bool visible) override;

 protected:
  class KeyboardWindowInputListener final : public ui::WindowInputListener {
   public:
    explicit KeyboardWindowInputListener(KeyboardInputDriver& driver)
        : driver_(driver) {}

    void OnKeyDown(ui::KeyEvent& e) override;
    void OnKeyUp(ui::KeyEvent& e) override;
    void OnKeyChar(ui::KeyEvent& e) override;
    void OnMouseDown(ui::MouseEvent& e) override;
    void OnRawMouseMove(ui::RawMouseMoveEvent& e) override;

   private:
    KeyboardInputDriver& driver_;
  };

  struct KeyEvent {
    ui::VirtualKey virtual_key = ui::VirtualKey::kNone;
    int repeat_count = 0;
    bool transition = false;  // going up(false) or going down(true)
    bool prev_state = false;  // down(true) or up(false)
    bool shift_pressed = false;
    bool ctrl_pressed = false;
    bool alt_pressed = false;
    bool capital_pressed = false;
    uint32_t unicode = 0;
  };

  struct KeyBinding {
    ui::VirtualKey input_key = ui::VirtualKey::kNone;
    ui::VirtualKey output_key = ui::VirtualKey::kNone;
    bool uppercase = false;
    bool lowercase = false;
    bool is_pressed = false;
  };

  void ParseKeyBinding(ui::VirtualKey virtual_key,
                       const std::string_view description,
                       const std::string_view binding);

  void OnKey(ui::KeyEvent& e, bool is_down);

  void OnChar(ui::KeyEvent& e);

  xe::global_critical_region global_critical_region_;

  std::deque<KeyEvent> key_events_;
  std::vector<KeyBinding> key_bindings_;

  KeyboardWindowInputListener window_input_listener_;
  KeyboardInputExtension* extension_ = nullptr;

  uint32_t packet_number_ = 1;
};

}  // namespace keyboard
}  // namespace hid
}  // namespace xe

#endif  // XENIA_HID_KEYBOARD_KEYBOARD_INPUT_DRIVER_H_
