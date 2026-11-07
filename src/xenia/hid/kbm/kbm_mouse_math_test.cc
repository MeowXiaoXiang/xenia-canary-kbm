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

#include <limits>
#include "xenia/hid/kbm/kbm_mouse_preview.h"

#include "third_party/catch/single_include/catch2/catch.hpp"

namespace xe::hid::kbm {

TEST_CASE("Physical mapping preserves radial direction and speed units",
          "[kbm]") {
  const double full = 800.0 * 10.0 / 2.54;
  REQUIRE(full == Approx(3149.606299));
  for (const double fraction : {0.0, 0.01, 0.5, 1.0, 2.0}) {
    auto value = MapMousePhysical(
        {full * fraction * 0.6, -full * fraction * 0.8}, 800, 10);
    REQUIRE(std::hypot(value.x, value.y) == Approx(std::min(fraction, 1.0)));
    REQUIRE(value.x == Approx(std::min(fraction, 1.0) * 0.6));
    REQUIRE(value.y == Approx(-std::min(fraction, 1.0) * 0.8));
  }
  REQUIRE(MapMousePhysical({}, 800, 10, 0.5).x == 0.0);
  REQUIRE(MapMousePhysical({full * 0.5, 0}, 800, 10, 0.3).x == Approx(0.65));
  REQUIRE(MapMousePhysical({full, 0}, 1600, 10).x == Approx(0.5));
  REQUIRE(MapMousePhysical({full, 0}, 800, 20).x == Approx(0.5));
  REQUIRE(
      MapMousePhysical({std::numeric_limits<double>::infinity(), 0}, 800, 10)
          .x == 0.0);
  REQUIRE(
      MapMousePhysical({1, 1}, std::numeric_limits<double>::quiet_NaN(), 0).x >
      0.0);
}

TEST_CASE("Physical poll buckets use actual duration and stop without a tail",
          "[kbm]") {
  for (const auto micros : {1, 50, 1000, 8000, 16000, 33000, 100000, 100001}) {
    for (bool compensation : {false, true}) {
      KbmMouseProcessor processor;
      KbmMouseSettings settings;
      settings.deadzone_compensation = compensation;
      const auto start = std::chrono::steady_clock::now();
      processor.Reset(start);
      const auto initial = start + std::chrono::microseconds(1);
      processor.Consume(settings, true, initial);
      processor.AddDelta(1, -1, initial);
      const auto sample = processor.Consume(
          settings, true, initial + std::chrono::microseconds(micros));
      REQUIRE(sample.raw_delta_x == 1);
      REQUIRE(sample.stale_sample == (micros > 100000));
      if (micros <= 100000) {
        REQUIRE(sample.velocity_x == Approx(1000000.0 / micros));
        REQUIRE(sample.thumb_x > 0);
        REQUIRE(sample.thumb_y > 0);
      } else {
        REQUIRE(sample.thumb_x == 0);
      }
      const auto stopped = processor.Consume(
          settings, true, initial + std::chrono::microseconds(micros + 1000));
      REQUIRE(stopped.thumb_x == 0);
      REQUIRE(stopped.thumb_y == 0);
    }
  }
}

TEST_CASE("Preview runs independently and retains its peak until reset",
          "[kbm]") {
  KbmMousePreview preview;
  KbmMouseProcessor gameplay;
  KbmMouseSettings settings;
  const auto start = std::chrono::steady_clock::now();
  gameplay.AddDelta(37, -19, start);
  const auto sequence = gameplay.event_sequence();
  preview.Start(start);
  preview.AddDelta(40, -10, start + std::chrono::milliseconds(1));
  preview.Sample(settings, start + std::chrono::milliseconds(15));
  REQUIRE(preview.current() == 0.0);
  preview.Sample(settings, start + std::chrono::milliseconds(16));
  REQUIRE(preview.current() > 0.0);
  const double peak = preview.peak();
  preview.Sample(settings, start + std::chrono::milliseconds(32));
  REQUIRE(preview.current() == 0.0);
  REQUIRE(preview.peak() == peak);
  preview.Stop();
  REQUIRE(preview.peak() == peak);
  preview.Reset(start + std::chrono::milliseconds(33));
  REQUIRE(preview.peak() == 0.0);
  REQUIRE(gameplay.pending_delta_x() == 37);
  REQUIRE(gameplay.pending_delta_y() == -19);
  REQUIRE(gameplay.event_sequence() == sequence);
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

TEST_CASE("Raw mouse processor resets stale motion before mapping", "[kbm]") {
  KbmMouseProcessor processor;
  KbmMouseSettings settings;
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

TEST_CASE("Short capture resets discard queued motion and filter history",
          "[kbm]") {
  KbmMouseProcessor processor;
  KbmMouseSettings settings;
  const auto start = std::chrono::steady_clock::now();
  processor.Reset(start);
  processor.Consume(settings, true, start + std::chrono::milliseconds(1));
  processor.AddDelta(24, -12, start + std::chrono::milliseconds(2));
  const auto moving =
      processor.Consume(settings, true, start + std::chrono::milliseconds(3));
  REQUIRE(moving.thumb_x > 0);
  processor.AddDelta(240, -120, start + std::chrono::milliseconds(4));

  // Release and restore within the stale-sample timeout must still reset.
  processor.Reset(start + std::chrono::milliseconds(5));
  const auto restored =
      processor.Consume(settings, true, start + std::chrono::milliseconds(6));
  REQUIRE(restored.reset_sample);
  REQUIRE_FALSE(restored.stale_sample);
  REQUIRE(restored.raw_delta_x == 0);
  REQUIRE(restored.raw_delta_y == 0);
  REQUIRE(restored.thumb_x == 0);
  REQUIRE(restored.thumb_y == 0);
  REQUIRE(restored.velocity_x == 0.0);
  REQUIRE(restored.velocity_y == 0.0);

  processor.AddDelta(12, -6, start + std::chrono::milliseconds(7));
  const auto resumed =
      processor.Consume(settings, true, start + std::chrono::milliseconds(8));
  REQUIRE_FALSE(resumed.reset_sample);
  REQUIRE(resumed.raw_delta_x == 12);
  REQUIRE(resumed.thumb_x > 0);
}

TEST_CASE("Suspended polls discard motion without carrying a filter tail",
          "[kbm]") {
  KbmMouseProcessor processor;
  const KbmMouseSettings settings;
  const auto start = std::chrono::steady_clock::now();
  processor.Reset(start);
  processor.Consume(settings, true, start + std::chrono::milliseconds(1));
  processor.AddDelta(24, -12, start + std::chrono::milliseconds(2));
  REQUIRE(
      processor.Consume(settings, true, start + std::chrono::milliseconds(3))
          .thumb_x > 0);
  processor.AddDelta(240, -120, start + std::chrono::milliseconds(4));
  const auto suspended =
      processor.Consume(settings, false, start + std::chrono::milliseconds(5));
  REQUIRE(suspended.raw_delta_x == 240);
  REQUIRE(suspended.thumb_x == 0);
  REQUIRE(suspended.thumb_y == 0);
  const auto resumed =
      processor.Consume(settings, true, start + std::chrono::milliseconds(6));
  REQUIRE(resumed.raw_delta_x == 0);
  REQUIRE(resumed.thumb_x == 0);
  REQUIRE(resumed.thumb_y == 0);
}

TEST_CASE("Same-time polls retain pending motion for the next advancing poll",
          "[kbm]") {
  KbmMouseProcessor processor;
  KbmMouseSettings settings;
  const auto start = std::chrono::steady_clock::now();
  processor.Reset(start);
  processor.Consume(settings, true, start + std::chrono::milliseconds(1));
  processor.AddDelta(12, -6, start + std::chrono::milliseconds(1));
  const auto same_time =
      processor.Consume(settings, true, start + std::chrono::milliseconds(1));
  REQUIRE(same_time.raw_delta_x == 0);
  const auto advancing =
      processor.Consume(settings, true, start + std::chrono::milliseconds(2));
  REQUIRE(advancing.raw_delta_x == 12);
  REQUIRE(advancing.raw_delta_y == -6);
  REQUIRE(advancing.thumb_x > 0);
  const auto empty =
      processor.Consume(settings, true, start + std::chrono::milliseconds(3));
  REQUIRE(empty.raw_delta_x == 0);
  REQUIRE(empty.thumb_x == 0);
}

TEST_CASE("Input reports retain the radial schema three contract", "[kbm]") {
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
  REQUIRE(report.poll_csv.find("mapper=physical_linear_radial") !=
          std::string::npos);
  REQUIRE(report.poll_csv.find("# schema=3") != std::string::npos);
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

TEST_CASE("Nonadvancing polls retain buckets and Y inversion is applied once",
          "[kbm]") {
  for (bool invert : {false, true}) {
    KbmMouseProcessor processor;
    KbmMouseSettings settings;
    settings.invert_y = invert;
    const auto start = std::chrono::steady_clock::now();
    processor.Reset(start);
    const auto initial = start + std::chrono::microseconds(1);
    processor.Consume(settings, true, initial);
    processor.AddDelta(10, 10, initial);
    const auto backwards = processor.Consume(settings, true, start);
    REQUIRE(backwards.thumb_x == 0);
    REQUIRE(backwards.raw_delta_x == 0);
    REQUIRE(processor.pending_delta_x() == 10);
    REQUIRE(processor.last_sample_time() == initial);
    const auto sample = processor.Consume(
        settings, true, initial + std::chrono::milliseconds(8));
    REQUIRE(sample.raw_delta_y == 10);
    REQUIRE((sample.thumb_y > 0) == invert);
  }
}

TEST_CASE(
    "Low-speed integer traces conserve counts across device and guest poll "
    "rates",
    "[kbm]") {
  using Clock = std::chrono::steady_clock;
  for (const int device_hz : {125, 500, 1000}) {
    for (const int poll_hz : {30, 60, 120, 1000}) {
      CAPTURE(device_hz, poll_hz);
      KbmMouseProcessor processor;
      KbmMouseSettings settings;
      settings.deadzone_compensation = false;
      const auto reset = Clock::now();
      processor.Reset(reset);
      const auto start = reset + std::chrono::microseconds(1);
      processor.Consume(settings, true, start);
      int event = 1, delivered = 0;
      int64_t consumed = 0;
      double integrated_output = 0.0;
      for (int poll = 1; poll <= poll_hz; ++poll) {
        const int64_t elapsed_ns = int64_t(poll) * 1000000000 / poll_hz;
        while (event <= device_hz &&
               int64_t(event) * 1000000000 / device_hz <= elapsed_ns) {
          const int cumulative = event * 125 / device_hz;
          if (cumulative != delivered) {
            processor.AddDelta(
                cumulative - delivered, 0,
                start + std::chrono::nanoseconds(int64_t(event) * 1000000000 /
                                                 device_hz));
          }
          delivered = cumulative;
          ++event;
        }
        const auto value = processor.Consume(
            settings, true, start + std::chrono::nanoseconds(elapsed_ns));
        consumed += value.raw_delta_x;
        integrated_output +=
            double(value.thumb_x) / 32767.0 * value.elapsed_seconds;
        if (!value.raw_delta_x) {
          REQUIRE(value.thumb_x == 0);
        }
      }
      REQUIRE(consumed == 125);
      REQUIRE(std::abs(integrated_output - 125.0 / (800.0 * 10.0 / 2.54)) <
              1.0 / 32767.0);
    }
  }
}

}  // namespace xe::hid::kbm
