/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2026 Xenia authors. All rights reserved.                         *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#include "xenia/hid/kbm/kbm_input_driver.h"

#include <utility>

#include "third_party/catch/single_include/catch2/catch.hpp"
#include "xenia/hid/input_system.h"
#include "xenia/kernel/xam/ui_input_filter.h"
#include "xenia/ui/imgui_dialog.h"
#include "xenia/ui/imgui_drawer.h"
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

  void MouseDown(ui::MouseEvent::Button button) {
    ui::MouseEvent event(this, button, 0, 0);
    WindowDestructionReceiver receiver(this);
    OnMouseDown(event, receiver);
  }

  bool Character() {
    ui::KeyEvent event(this, ui::VirtualKey::kA, 1, false, false, false, false,
                       false);
    event.set_unicode('a');
    WindowDestructionReceiver receiver(this);
    OnKeyChar(event, receiver);
    return event.is_handled();
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

class TestDialog final : public ui::ImGuiDialog {
 public:
  explicit TestDialog(ui::ImGuiDrawer& drawer) : ImGuiDialog(&drawer) {}
  void OnDraw(ImGuiIO&) override {}
};

}  // namespace

TEST_CASE("Overlays remain passive after the last host dialog closes",
          "[kbm][lifecycle][overlay]") {
  DriverFixture fixture;
  InputSystem input_system(&fixture.window);
  auto driver = std::make_unique<KbmInputDriver>(&fixture.window, 1);
  driver->ApplySettings(fixture.driver.GetSettings());
  auto* kbm = driver.get();
  input_system.AddDriver(std::move(driver));
  ui::ImGuiDrawer drawer(&fixture.window, 100);
  drawer.LoadInputSystem(&input_system);
  drawer.SetOverlay([] {});
  drawer.SetPassiveOverlay([] {});

  fixture.window.Key(ui::VirtualKey::kSpace, true);
  X_INPUT_STATE state = {};
  REQUIRE(kbm->GetState(0, &state) == X_ERROR_SUCCESS);
  REQUIRE(state.gamepad.buttons & X_INPUT_GAMEPAD_A);
  REQUIRE_FALSE(fixture.window.Character());

  for (int i = 0; i < 2; ++i) {
    auto first = std::make_unique<TestDialog>(drawer);
    auto second = std::make_unique<TestDialog>(drawer);
    REQUIRE(kbm->GetState(0, &state) == X_ERROR_SUCCESS);
    REQUIRE(state.gamepad.buttons == 0);
    REQUIRE(fixture.window.Character());
    first.reset();
    REQUIRE(drawer.IsAnyDialogOpen());
    REQUIRE(fixture.window.Character());
    second.reset();
    REQUIRE_FALSE(drawer.IsAnyDialogOpen());
    REQUIRE(drawer.HasPassiveOverlay());
    REQUIRE_FALSE(fixture.window.Character());

    fixture.window.Key(ui::VirtualKey::kSpace, false);
    fixture.window.Key(ui::VirtualKey::kSpace, true);
    REQUIRE(kbm->GetState(0, &state) == X_ERROR_SUCCESS);
    REQUIRE(state.gamepad.buttons & X_INPUT_GAMEPAD_A);
  }

  drawer.SetOverlay(nullptr);
  REQUIRE(drawer.HasPassiveOverlay());
  REQUIRE_FALSE(fixture.window.Character());
  drawer.SetOverlay([] {});
  drawer.SetPassiveOverlay(nullptr);
  REQUIRE_FALSE(drawer.HasPassiveOverlay());
  REQUIRE_FALSE(fixture.window.Character());
}

TEST_CASE("Host hotkey queries read buttons without recording a poll",
          "[kbm][lifecycle]") {
  DriverFixture fixture;
  auto settings = fixture.driver.GetSettings();
  settings.keybind_a = "Key.G";
  fixture.driver.ApplySettings(settings);
  const auto baseline_packet = fixture.State().packet_number;
  fixture.driver.SetHostInputSuspended(true);
  REQUIRE(fixture.driver.StartInputSampling());
  fixture.driver.SetHostInputSuspended(false);
  fixture.window.Key(ui::VirtualKey::kG, true);

  X_INPUT_STATE host_state = {};
  REQUIRE(fixture.driver.GetStateForHostHotkeys(1, &host_state) ==
          X_ERROR_DEVICE_NOT_CONNECTED);
  REQUIRE(fixture.driver.GetStateForHostHotkeys(0, &host_state) ==
          X_ERROR_SUCCESS);
  REQUIRE(host_state.gamepad.buttons & X_INPUT_GAMEPAD_A);
  REQUIRE(fixture.driver.GetInputSamplingStatus().sample_count == 0);

  const auto guest_state = fixture.State();
  REQUIRE(guest_state.gamepad.buttons & X_INPUT_GAMEPAD_A);
  REQUIRE(guest_state.packet_number == baseline_packet + 1);
  REQUIRE(fixture.driver.GetInputSamplingStatus().sample_count == 1);
  fixture.driver.CancelInputSampling();
}

TEST_CASE("XAM closing-input filtering does not advance KBM polling",
          "[kbm][lifecycle][xam]") {
  DriverFixture fixture;
  InputSystem input_system(&fixture.window);
  auto driver = std::make_unique<KbmInputDriver>(&fixture.window, 1);
  driver->ApplySettings(fixture.driver.GetSettings());
  auto* kbm = driver.get();
  input_system.AddDriver(std::move(driver));
  kbm->SetHostInputSuspended(true);
  REQUIRE(kbm->StartInputSampling());
  kbm->SetHostInputSuspended(false);
  fixture.window.Key(ui::VirtualKey::kSpace, true);

  kernel::xam::UIInputFilter filter;
  filter.OnUIClosed(&input_system);
  REQUIRE(kbm->GetInputSamplingStatus().sample_count == 0);
  X_INPUT_KEYSTROKE repeat = {};
  repeat.user_index = 0;
  repeat.flags = X_INPUT_KEYSTROKE_REPEAT;
  REQUIRE(filter.ShouldDropKeystroke(&input_system, repeat));
  REQUIRE(kbm->GetInputSamplingStatus().sample_count == 0);

  X_INPUT_STATE state = {};
  REQUIRE(kbm->GetStateForHostHotkeys(0, &state) == X_ERROR_SUCCESS);
  REQUIRE(state.gamepad.buttons & X_INPUT_GAMEPAD_A);
  filter.FilterState(0, &state);
  REQUIRE_FALSE(state.gamepad.buttons & X_INPUT_GAMEPAD_A);

  fixture.window.Key(ui::VirtualKey::kSpace, false);
  REQUIRE_FALSE(filter.ShouldDropKeystroke(&input_system, repeat));
  fixture.window.Key(ui::VirtualKey::kSpace, true);
  REQUIRE(kbm->GetStateForHostHotkeys(0, &state) == X_ERROR_SUCCESS);
  filter.FilterState(0, &state);
  REQUIRE(state.gamepad.buttons & X_INPUT_GAMEPAD_A);
  REQUIRE(kbm->GetInputSamplingStatus().sample_count == 0);
  REQUIRE(kbm->GetState(0, &state) == X_ERROR_SUCCESS);
  REQUIRE(kbm->GetInputSamplingStatus().sample_count == 1);
  kbm->CancelInputSampling();
}

TEST_CASE("Capture hotkey binding ignores mouse buttons and keeps waiting",
          "[kbm][lifecycle]") {
  DriverFixture fixture;
  fixture.driver.SetHostInputSuspended(true);
  fixture.driver.BeginBindingCapture(false);
  fixture.window.MouseDown(ui::MouseEvent::Button::kLeft);
  REQUIRE(fixture.driver.ConsumeBindingCaptureResult().status ==
          KbmInputDriver::BindingCaptureStatus::kNone);
  fixture.window.Key(ui::VirtualKey::kF8, true);
  const auto result = fixture.driver.ConsumeBindingCaptureResult();
  REQUIRE(result.status == KbmInputDriver::BindingCaptureStatus::kCaptured);
  REQUIRE(result.value == "Key.F8");

  fixture.driver.BeginBindingCapture();
  fixture.window.MouseDown(ui::MouseEvent::Button::kLeft);
  REQUIRE(fixture.driver.ConsumeBindingCaptureResult().value == "Mouse.Left");
  REQUIRE(fixture.State().gamepad.buttons == 0);
}

TEST_CASE("Unsupported keyboard keys do not clear a pending binding",
          "[kbm][lifecycle]") {
  DriverFixture fixture;
  fixture.driver.SetHostInputSuspended(true);
  fixture.driver.BeginBindingCapture();
  fixture.window.Key(ui::VirtualKey::kOem1, true);
  REQUIRE(fixture.driver.ConsumeBindingCaptureResult().status ==
          KbmInputDriver::BindingCaptureStatus::kNone);
  fixture.window.Key(ui::VirtualKey::kG, true);
  const auto result = fixture.driver.ConsumeBindingCaptureResult();
  REQUIRE(result.status == KbmInputDriver::BindingCaptureStatus::kCaptured);
  REQUIRE(result.value == "Key.G");
}

TEST_CASE("Named navigation and numpad keys reach their controller bindings",
          "[kbm][lifecycle]") {
  const std::pair<ui::VirtualKey, const char*> cases[] = {
      {ui::VirtualKey::kInsert, "Key.Insert"},
      {ui::VirtualKey::kDelete, "Key.Delete"},
      {ui::VirtualKey::kLWin, "Key.LeftSuper"},
      {ui::VirtualKey::kRWin, "Key.RightSuper"},
      {ui::VirtualKey::kApps, "Key.Menu"},
      {ui::VirtualKey::kNumLock, "Key.NumLock"},
      {ui::VirtualKey::kMultiply, "Key.NumpadMultiply"},
      {ui::VirtualKey::kAdd, "Key.NumpadAdd"},
      {ui::VirtualKey::kSubtract, "Key.NumpadSubtract"},
      {ui::VirtualKey::kDecimal, "Key.NumpadDecimal"},
      {ui::VirtualKey::kDivide, "Key.NumpadDivide"},
  };
  for (const auto& [key, token] : cases) {
    CAPTURE(token);
    DriverFixture fixture;
    auto settings = fixture.driver.GetSettings();
    settings.keybind_a = token;
    fixture.driver.ApplySettings(settings);
    fixture.window.Key(key, true);
    REQUIRE(fixture.State().gamepad.buttons & X_INPUT_GAMEPAD_A);
    fixture.window.Key(key, false);
    REQUIRE_FALSE(fixture.State().gamepad.buttons & X_INPUT_GAMEPAD_A);
  }
}

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

TEST_CASE("Cancelling binding capture discards an unconsumed result",
          "[kbm][lifecycle]") {
  DriverFixture fixture;
  fixture.driver.SetHostInputSuspended(true);
  fixture.driver.BeginBindingCapture();
  fixture.window.Key(ui::VirtualKey::kSpace, true);
  // A UI action can cancel after the native event queued a result, before
  // the dialog has accepted it into its draft.
  fixture.driver.CancelBindingCapture();
  auto result = fixture.driver.ConsumeBindingCaptureResult();
  REQUIRE(result.status == KbmInputDriver::BindingCaptureStatus::kNone);
  REQUIRE(result.value.empty());
  REQUIRE(fixture.State().gamepad.buttons == 0);
  fixture.window.Key(ui::VirtualKey::kSpace, false);
  fixture.driver.BeginBindingCapture();
  fixture.window.Key(ui::VirtualKey::kG, true);
  result = fixture.driver.ConsumeBindingCaptureResult();
  REQUIRE(result.status == KbmInputDriver::BindingCaptureStatus::kCaptured);
  REQUIRE(result.value == "Key.G");
  REQUIRE(fixture.driver.ConsumeBindingCaptureResult().status ==
          KbmInputDriver::BindingCaptureStatus::kNone);
  fixture.driver.SetHostInputSuspended(false);
  REQUIRE(fixture.State().gamepad.buttons == 0);
}

TEST_CASE("Draft bindings and failed publication leave driver output unchanged",
          "[kbm][lifecycle]") {
  DriverFixture fixture;
  auto draft = fixture.driver.GetSettings();
  draft.keybind_a = "Key.G";
  draft.raw_mouse_dpi = 1600;
  draft.mouse_processing = {KbmMouseMode::kFixedWindow, 6.5, 3.0};
  REQUIRE_FALSE(fixture.driver.StartMousePreview());
  REQUIRE_FALSE(CommitKbmSettings(
      draft, [](const auto&) { return false; },
      [&](const auto& value) { fixture.driver.ApplySettings(value); }));
  REQUIRE(fixture.driver.GetSettings().raw_mouse_dpi == 800);
  REQUIRE(fixture.driver.GetSettings().mouse_processing ==
          KbmMouseProcessing{});
  fixture.window.Key(ui::VirtualKey::kSpace, true);
  REQUIRE(fixture.State().gamepad.buttons & X_INPUT_GAMEPAD_A);
  fixture.window.Key(ui::VirtualKey::kSpace, false);
  bool persisted = false;
  REQUIRE(CommitKbmSettings(
      draft,
      [&](const auto&) {
        persisted = true;
        return true;
      },
      [&](const auto& value) {
        REQUIRE(persisted);
        fixture.driver.ApplySettings(value);
      }));
  REQUIRE(fixture.driver.GetSettings().mouse_processing ==
          draft.mouse_processing);
  fixture.window.Key(ui::VirtualKey::kSpace, true);
  REQUIRE_FALSE(fixture.State().gamepad.buttons & X_INPUT_GAMEPAD_A);
  fixture.window.Key(ui::VirtualKey::kG, true);
  REQUIRE(fixture.State().gamepad.buttons & X_INPUT_GAMEPAD_A);
}

}  // namespace xe::hid::kbm
