/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2026 Xenia authors. All rights reserved.                         *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#include "xenia/hid/kbm/kbm_input_driver.h"

#include "third_party/catch/single_include/catch2/catch.hpp"
#include "xenia/ui/window.h"
#include "xenia/ui/windowed_app_context.h"

namespace xe::hid::kbm {
namespace {

// Exercise the real driver and window listeners without an HWND, Raw Input
// registration, cursor changes, or a desktop message loop. Native capture still
// requires the manual Windows checks documented in docs/kbm_input.md.
class TestAppContext final : public ui::WindowedAppContext {
  void NotifyUILoopOfPendingFunctions() override {}
  void PlatformQuitFromUIThread() override {}
};

class TestWindow final : public ui::Window {
 public:
  explicit TestWindow(TestAppContext& context)
      : Window(context, "KBM lifecycle test", 640, 480) {}
  ~TestWindow() override { EnterDestructor(); }

  void SetFocused(bool focused) {
    WindowDestructionReceiver receiver(this);
    OnFocusUpdate(focused, receiver);
  }

  void Key(ui::VirtualKey key, bool down, bool repeat = false) {
    ui::KeyEvent event(this, key, 1, repeat, false, false, false, false);
    WindowDestructionReceiver receiver(this);
    if (down) {
      OnKeyDown(event, receiver);
    } else {
      OnKeyUp(event, receiver);
    }
  }

 private:
  bool OpenImpl() override { return true; }
  void RequestCloseImpl() override {}
  std::unique_ptr<ui::Surface> CreateSurfaceImpl(
      ui::Surface::TypeFlags) override {
    return nullptr;
  }
  void RequestPaintImpl() override {}
};

class DriverFixture {
 public:
  DriverFixture() : window(context), driver(&window, 0) {
    KbmSettings settings;
    settings.enabled = true;
    settings.raw_mouse = false;
    settings.user_index = 0;
    driver.ApplySettings(settings);
    REQUIRE(window.Open());
    window.SetFocused(true);
  }
  ~DriverFixture() { ApplySettingsToCvars(original_settings); }

  X_INPUT_STATE State() {
    X_INPUT_STATE state = {};
    REQUIRE(driver.GetState(0, &state) == X_ERROR_SUCCESS);
    return state;
  }

  KbmSettings original_settings = GetSettingsFromCvars();
  TestAppContext context;
  TestWindow window;
  KbmInputDriver driver;
};

}  // namespace

TEST_CASE("KBM focus loss clears held controls without a key-up event",
          "[kbm][lifecycle]") {
  DriverFixture fixture;
  fixture.window.Key(ui::VirtualKey::kW, true);
  fixture.window.Key(ui::VirtualKey::kSpace, true);
  REQUIRE(fixture.State().gamepad.thumb_ly > 0);
  REQUIRE(fixture.State().gamepad.buttons & X_INPUT_GAMEPAD_A);

  fixture.window.SetFocused(false);
  REQUIRE(fixture.State().gamepad.thumb_ly == 0);
  REQUIRE(fixture.State().gamepad.buttons == 0);
  fixture.window.SetFocused(true);
  REQUIRE(fixture.State().gamepad.thumb_ly == 0);
  REQUIRE(fixture.State().gamepad.buttons == 0);

  fixture.window.Key(ui::VirtualKey::kW, true);
  REQUIRE(fixture.State().gamepad.thumb_ly > 0);
  fixture.window.Key(ui::VirtualKey::kW, false);
  REQUIRE(fixture.State().gamepad.thumb_ly == 0);
}

TEST_CASE("KBM host suspension clears controls before resuming",
          "[kbm][lifecycle]") {
  DriverFixture fixture;
  fixture.window.Key(ui::VirtualKey::kW, true);
  REQUIRE(fixture.State().gamepad.thumb_ly > 0);
  fixture.driver.SetHostInputSuspended(true);
  REQUIRE(fixture.State().gamepad.thumb_ly == 0);
  fixture.driver.SetHostInputSuspended(true);
  fixture.driver.SetHostInputSuspended(false);
  REQUIRE(fixture.State().gamepad.thumb_ly == 0);
  fixture.window.Key(ui::VirtualKey::kW, true);
  REQUIRE(fixture.State().gamepad.thumb_ly > 0);
  fixture.driver.SetHostInputSuspended(false);
  REQUIRE(fixture.State().gamepad.thumb_ly > 0);
}

TEST_CASE("KBM focus reset clears modifiers used by chords",
          "[kbm][lifecycle]") {
  DriverFixture fixture;
  auto settings = fixture.driver.GetSettings();
  settings.keybind_a = "Ctrl+Key.K";
  fixture.driver.ApplySettings(settings);
  fixture.window.Key(ui::VirtualKey::kControl, true);
  fixture.window.Key(ui::VirtualKey::kK, true);
  REQUIRE(fixture.State().gamepad.buttons & X_INPUT_GAMEPAD_A);
  fixture.window.SetFocused(false);
  fixture.window.SetFocused(true);
  fixture.window.Key(ui::VirtualKey::kK, true);
  REQUIRE_FALSE(fixture.State().gamepad.buttons & X_INPUT_GAMEPAD_A);
}

TEST_CASE("KBM host UI keys do not become gameplay input on resume",
          "[kbm][lifecycle]") {
  DriverFixture fixture;
  fixture.driver.SetHostInputSuspended(true);
  fixture.window.Key(ui::VirtualKey::kSpace, true);
  fixture.window.Key(ui::VirtualKey::kW, true);
  REQUIRE(fixture.State().gamepad.buttons == 0);
  REQUIRE(fixture.State().gamepad.thumb_ly == 0);
  SECTION("resume while focused") {}
  SECTION("resume before regaining focus") { fixture.window.SetFocused(false); }
  fixture.driver.SetHostInputSuspended(false);
  fixture.window.SetFocused(true);
  REQUIRE(fixture.State().gamepad.buttons == 0);
  REQUIRE(fixture.State().gamepad.thumb_ly == 0);
  X_INPUT_KEYSTROKE stroke = {};
  REQUIRE(fixture.driver.GetKeystroke(0, 0, &stroke) == X_ERROR_EMPTY);
  fixture.window.Key(ui::VirtualKey::kSpace, false);
  fixture.window.Key(ui::VirtualKey::kSpace, true);
  REQUIRE(fixture.State().gamepad.buttons & X_INPUT_GAMEPAD_A);
}

TEST_CASE("KBM keystrokes report ordinary press and release transitions",
          "[kbm][lifecycle]") {
  DriverFixture fixture;
  X_INPUT_KEYSTROKE stroke = {};
  fixture.window.Key(ui::VirtualKey::kSpace, true);
  REQUIRE(fixture.driver.GetKeystroke(0, 0, &stroke) == X_ERROR_SUCCESS);
  REQUIRE(stroke.flags == X_INPUT_KEYSTROKE_KEYDOWN);
  const auto key = stroke.virtual_key;
  fixture.window.Key(ui::VirtualKey::kSpace, false);
  REQUIRE(fixture.driver.GetKeystroke(0, 0, &stroke) == X_ERROR_SUCCESS);
  REQUIRE(stroke.virtual_key == key);
  REQUIRE(stroke.flags == X_INPUT_KEYSTROKE_KEYUP);
  REQUIRE(fixture.driver.GetKeystroke(0, 0, &stroke) == X_ERROR_EMPTY);
}

TEST_CASE("KBM binding capture works while host input is suspended",
          "[kbm][lifecycle]") {
  DriverFixture fixture;
  fixture.driver.SetHostInputSuspended(true);
  fixture.driver.BeginBindingCapture();
  fixture.window.Key(ui::VirtualKey::kSpace, true);
  const auto captured = fixture.driver.ConsumeBindingCaptureResult();
  REQUIRE(captured.status == KbmInputDriver::BindingCaptureStatus::kCaptured);
  REQUIRE(captured.value == "Key.Space");
  REQUIRE(fixture.State().gamepad.buttons == 0);
  fixture.driver.BeginBindingCapture();
  fixture.window.Key(ui::VirtualKey::kEscape, true);
  REQUIRE(fixture.driver.ConsumeBindingCaptureResult().status ==
          KbmInputDriver::BindingCaptureStatus::kCancelled);
  fixture.driver.CancelBindingCapture();
  fixture.driver.SetHostInputSuspended(false);
  REQUIRE(fixture.State().gamepad.buttons == 0);
}

}  // namespace xe::hid::kbm
