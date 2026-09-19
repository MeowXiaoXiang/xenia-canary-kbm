/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2026 Xenia authors. All rights reserved.                         *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#include "xenia/hid/kbm/kbm_mouse_math.h"
#include "xenia/hid/kbm/kbm_config.h"

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

TEST_CASE("KBM configuration accepts only the current schema", "[kbm]") {
  REQUIRE(IsKbmConfigSchemaVersionSupported(kKbmConfigSchemaVersion));
  REQUIRE_FALSE(IsKbmConfigSchemaVersionSupported(std::nullopt));
  REQUIRE_FALSE(IsKbmConfigSchemaVersionSupported(
      kKbmConfigSchemaVersion - 1));
  REQUIRE_FALSE(IsKbmConfigSchemaVersionSupported(
      kKbmConfigSchemaVersion + 1));
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

}  // namespace xe::hid::kbm
