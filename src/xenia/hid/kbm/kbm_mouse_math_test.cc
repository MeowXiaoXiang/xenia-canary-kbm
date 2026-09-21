/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2026 Xenia authors. All rights reserved.                         *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#include "xenia/hid/kbm/kbm_mouse_math.h"
#include "xenia/hid/kbm/kbm_binding.h"
#include "xenia/hid/kbm/kbm_controller_state.h"
#include "xenia/hid/kbm/kbm_input_report.h"
#include "xenia/hid/kbm/kbm_input_state.h"
#include "xenia/hid/kbm/kbm_mouse_processor.h"

#include <deque>

#include "third_party/catch/single_include/catch2/catch.hpp"

namespace xe::hid::kbm {

// Offline reference candidate only. Window membership is evaluated from event
// time without consuming history when the consumer polls.
class FixedWindowReference {
 public:
  void Add(double time, double delta) { events_.push_back({time, delta}); }
  double Snapshot(double now, double window) const {
    double sum = 0.0;
    for (const auto& event : events_) {
      if (event.first > now - window && event.first <= now) {
        sum += event.second;
      }
    }
    return sum / window;
  }

 private:
  std::deque<std::pair<double, double>> events_;
};

TEST_CASE("Fixed window reference preserves gain and has finite support",
          "[kbm]") {
  for (double interval : {0.001, 0.002, 0.008}) {
    FixedWindowReference reference;
    for (int event = 1; event <= 100; ++event) {
      reference.Add(event * interval, 1000.0 * interval);
    }
    // Half an event interval avoids boundary ambiguity from floating point.
    const double time = 99.5 * interval;
    REQUIRE(reference.Snapshot(time, 0.016) == Approx(1000.0));
    const double before = reference.Snapshot(time, 0.016);
    reference.Snapshot(time + 0.0001, 0.016);
    REQUIRE(reference.Snapshot(time, 0.016) == before);
    REQUIRE(reference.Snapshot(100 * interval + 0.017, 0.016) == 0.0);
  }
}

TEST_CASE("Same-time event batches preserve displacement in impulse estimator",
          "[kbm]") {
  MouseEventExponential batched(0.008), combined(0.008);
  for (int i = 0; i < 8; ++i) {
    batched.Add(0.010, {1.0, -1.0});
  }
  combined.Add(0.010, {8.0, -8.0});
  REQUIRE(batched.Snapshot(0.011).x == combined.Snapshot(0.011).x);
  REQUIRE(batched.Snapshot(0.011).y == combined.Snapshot(0.011).y);
  combined.Add(0.012, {-8.0, 8.0});
  REQUIRE(combined.Snapshot(0.012).x < 0.0);
  REQUIRE(combined.Snapshot(0.012).y > 0.0);
}

TEST_CASE("Anti-deadzone cannot sustain an old filter tail", "[kbm]") {
  REQUIRE(MouseCompensationActive(0.001, 0.1, 3000.0));
  REQUIRE_FALSE(MouseCompensationActive(0.013, 0.1, 3000.0));
  REQUIRE(MouseCompensationActive(0.020, 100.0, 3000.0));
  REQUIRE_FALSE(MouseCompensationActive(0.050, 100000.0, 3000.0));
  REQUIRE_FALSE(MouseCompensationActive(-0.001, 100.0, 3000.0));
}

TEST_CASE("Radial response preserves direction and bounds strength", "[kbm]") {
  for (double exponent : {0.8, 0.9, 1.0, 1.2}) {
    for (double speed : {0.0001, 100.0, 3000.0, 9000.0}) {
      const MouseVector velocity{speed * 0.8, speed * 0.6};
      const auto mapped = MapMouseRadial(velocity, 3000.0, exponent);
      REQUIRE(mapped.y / mapped.x == Approx(0.75));
      REQUIRE(std::hypot(mapped.x, mapped.y) <= 1.0 + 1e-12);
      const auto reversed =
          MapMouseRadial({-velocity.x, -velocity.y}, 3000.0, exponent);
      REQUIRE(reversed.x == Approx(-mapped.x));
      REQUIRE(reversed.y == Approx(-mapped.y));
    }
  }
  REQUIRE(MapMouseRadial({}, 3000.0, 0.8).x == 0.0);
  REQUIRE(MapMouseRadial({1500.0, 0.0}, 3000.0, 1.0).x == Approx(0.5));
}

TEST_CASE("Radial minimum response preserves direction", "[kbm]") {
  const auto mapped = MapMouseRadial({3.0, 4.0}, 1000.0, 1.0, 0.30);
  REQUIRE(mapped.y / mapped.x == Approx(4.0 / 3.0));
  REQUIRE(std::hypot(mapped.x, mapped.y) >= 0.30);
  REQUIRE(std::hypot(mapped.x, mapped.y) <= 1.0);
  REQUIRE(MapMouseRadial({}, 1000.0, 1.0, 0.30).x == 0.0);
}

TEST_CASE("Logical KBM input codes use stable v3 tokens", "[kbm]") {
  KbmInputCode code;
  REQUIRE(ParseKbmInputCode("Key.W", code));
  REQUIRE(FormatKbmInputCode(code) == "Key.W");
  REQUIRE(FormatKbmInputCode(code, true) == "W");
  REQUIRE(ParseKbmInputCode("Mouse.X1", code));
  REQUIRE(FormatKbmInputCode(code) == "Mouse.X1");
  REQUIRE(ParseKbmInputCode("Mouse.X2", code));
  REQUIRE(FormatKbmInputCode(code, true) == "Mouse X2");
  REQUIRE(ParseKbmInputCode("Key.Escape", code));
  REQUIRE(FormatKbmInputCode(code) == "Key.Escape");
  REQUIRE(ParseKbmInputCode("Key.Numpad0", code));
  REQUIRE(FormatKbmInputCode(code) == "Key.Numpad0");
  REQUIRE(ParseKbmInputCode("Key.Numpad9", code));
  REQUIRE(FormatKbmInputCode(code) == "Key.Numpad9");
  REQUIRE_FALSE(ParseKbmInputCode("W", code));
  REQUIRE_FALSE(ParseKbmInputCode("0x57", code));
  REQUIRE_FALSE(ParseKbmInputCode("Key.Numpad00", code));
}

TEST_CASE("Logical KBM chords preserve modifiers and display names", "[kbm]") {
  KbmChord chord;
  REQUIRE(ParseKbmChord("Ctrl+Shift+Key.F8", chord));
  REQUIRE(chord.ctrl);
  REQUIRE(chord.shift);
  REQUIRE(FormatKbmChord(chord) == "Ctrl+Shift+Key.F8");
  REQUIRE(FormatKbmChord(chord, true) == "Ctrl+Shift+F8");
  REQUIRE(ParseKbmChord("Key.LeftShift", chord));
  REQUIRE(IsModifierInputCode(chord.input));
  REQUIRE_FALSE(ParseKbmChord("Ctrl+F8", chord));
}

TEST_CASE("Event estimator snapshots do not consume state", "[kbm]") {
  MouseEventExponential frequent(0.008), sparse(0.008);
  for (int event = 1; event <= 100; ++event) {
    const double now = event * 0.001;
    frequent.Add(now, {1.0, -2.0});
    sparse.Add(now, {1.0, -2.0});
    for (int poll = 0; poll < 10; ++poll) {
      frequent.Snapshot(now + poll * 0.0001);
    }
  }
  REQUIRE(frequent.Snapshot(0.101).x == sparse.Snapshot(0.101).x);
  REQUIRE(frequent.Snapshot(0.101).y == sparse.Snapshot(0.101).y);
  frequent.Reset(0.102);
  REQUIRE(frequent.Snapshot(0.103).x == 0.0);
}

TEST_CASE("Event impulse ripple depends on device interval", "[kbm]") {
  for (double interval : {0.001, 0.002, 0.008}) {
    MouseEventExponential estimator(0.008);
    for (int i = 1; i <= 1000; ++i) {
      estimator.Add(i * interval, {1000.0 * interval, 0.0});
    }
    const double peak = estimator.Snapshot(1000 * interval).x;
    const double trough = estimator.Snapshot(1001 * interval).x;
    const double ratio = interval / 0.008;
    REQUIRE(peak / 1000 == Approx(ratio / -std::expm1(-ratio)));
    REQUIRE(trough / 1000 == Approx(ratio / std::expm1(ratio)));
  }
}

TEST_CASE("Mouse filter uses elapsed time rather than query count", "[kbm]") {
  double repeated = 1000.0;
  for (int i = 0; i < 20; ++i) {
    repeated = FilterMouseDisplacement(0.0, 0.00005, 8.0, repeated);
  }
  REQUIRE(repeated == Approx(FilterMouseDisplacement(0.0, 0.001, 8.0, 1000.0)));
  REQUIRE(repeated == Approx(1000.0 * std::exp(-0.001 / 0.008)));
}

TEST_CASE("Mouse filter handles zero time and unsmoothed input", "[kbm]") {
  REQUIRE(FilterMouseDisplacement(3.0, 0.0, 8.0, 42.0) == 42.0);
  REQUIRE(FilterMouseDisplacement(3.0, -1.0, 8.0, 42.0) == 42.0);
  REQUIRE(FilterMouseDisplacement(3.0, 0.00005, 0.0, 42.0) == Approx(60000.0));
  REQUIRE(FilterMouseDisplacement(0.0, 0.001, 0.0, 42.0) == 0.0);
}

TEST_CASE("Mouse filter preserves constant velocity and tiny intervals",
          "[kbm]") {
  for (double dt : {1e-12, 0.00005, 0.001, 0.008, 0.1}) {
    REQUIRE(FilterMouseDisplacement(250.0 * dt, dt, 8.0, 250.0) ==
            Approx(250.0));
    REQUIRE(FilterMouseDisplacement(-250.0 * dt, dt, 8.0, -250.0) ==
            Approx(-250.0));
  }
  REQUIRE(FilterMouseDisplacement(1.0, 1e-12, 8.0, 0.0) == Approx(125.0));
}

TEST_CASE("Raw mouse processor resets stale motion before mapping", "[kbm]") {
  KbmMouseProcessor processor;
  KbmMouseSettings settings;
  settings.smoothing_time_ms = 0.0;
  const auto start = std::chrono::steady_clock::now();
  processor.Reset(start);
  const auto reset =
      processor.Consume(settings, true, start + std::chrono::milliseconds(1));
  REQUIRE(reset.reset_sample);
  processor.AddDelta(240, -120, start + std::chrono::milliseconds(2));
  const auto sample =
      processor.Consume(settings, true, start + std::chrono::milliseconds(12));
  REQUIRE(sample.raw_delta_x == 240);
  REQUIRE(sample.raw_delta_y == -120);
  REQUIRE(sample.thumb_x > 0);
  REQUIRE(sample.thumb_y > 0);

  const auto stale =
      processor.Consume(settings, true, start + std::chrono::milliseconds(120));
  REQUIRE(stale.stale_sample);
  REQUIRE(stale.thumb_x == 0);
  REQUIRE(stale.thumb_y == 0);
}

TEST_CASE("Input reports retain the radial schema two contract", "[kbm]") {
  const auto started = std::chrono::steady_clock::now();
  KbmInputSample sample;
  sample.time = started + std::chrono::milliseconds(8);
  sample.elapsed_seconds = 0.008;
  sample.raw_delta_x = 10;
  sample.thumb_x = 1234;
  KbmRawInputSample event;
  event.time = started;
  event.kind = 2;
  const auto report =
      SerializeKbmInputReport({sample}, {event}, 0, 0, {}, started);
  REQUIRE(report.poll_csv.find("mapper=radial") != std::string::npos);
  REQUIRE(report.poll_csv.find("# schema=2") != std::string::npos);
  REQUIRE(report.events_csv.find("kind: 0=motion,1=reset,2=initial_state") !=
          std::string::npos);
}

TEST_CASE("Logical input state clears modifiers on focus reset", "[kbm]") {
  KbmInputState state;
  state.SetPressed(KbmInputCode::kMouseX1, true);
  state.SetPressed(KbmInputCode::kMouseX2, true);
  state.SetPressed(KbmInputCode::kLeftShift, true);
  state.SetPressed(KbmInputCode::kRightCtrl, true);
  state.SetCapsLock(true);
  REQUIRE(state.modifiers().shift);
  REQUIRE(state.modifiers().ctrl);
  REQUIRE(state.modifiers().caps_lock);
  REQUIRE(state.IsPressed(KbmInputCode::kShift));
  state.ResetMouseButtons();
  REQUIRE_FALSE(state.IsPressed(KbmInputCode::kMouseX1));
  REQUIRE_FALSE(state.IsPressed(KbmInputCode::kMouseX2));
  REQUIRE(state.IsPressed(KbmInputCode::kShift));
  state.Reset();
  REQUIRE_FALSE(state.modifiers().shift);
  REQUIRE_FALSE(state.modifiers().ctrl);
  REQUIRE_FALSE(state.modifiers().caps_lock);
}

TEST_CASE("Portable controller state composes KBM controls", "[kbm]") {
  KbmControllerState state;
  ApplyKbmControl(state, KbmControl::kA);
  ApplyKbmControl(state, KbmControl::kDpadLeft);
  ApplyKbmControl(state, KbmControl::kRTrigger);
  ApplyKbmControl(state, KbmControl::kLThumbLeft);
  ApplyKbmControl(state, KbmControl::kLThumbRight);
  REQUIRE(state.buttons == uint16_t(0x1004));
  REQUIRE(state.right_trigger == 0xFF);
  REQUIRE(state.thumb_lx == -1);
  AddKbmThumb(state.thumb_rx, 30000);
  AddKbmThumb(state.thumb_rx, 30000);
  REQUIRE(state.thumb_rx == 32767);
}

}  // namespace xe::hid::kbm
