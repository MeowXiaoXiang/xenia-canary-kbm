/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2026 Xenia authors. All rights reserved.                         *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#ifndef XENIA_HID_KBM_KBM_MOUSE_WINDOW_H_
#define XENIA_HID_KBM_KBM_MOUSE_WINDOW_H_

#include <array>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <optional>

#include "xenia/hid/kbm/kbm_mouse_math.h"

namespace xe::hid::kbm {

// Calls are ordered by the host clock. The processor skips nonadvancing reads.
class KbmMouseWindow {
 public:
  using Time = std::chrono::steady_clock::time_point;
  using Duration = std::chrono::microseconds;
  static constexpr size_t kCapacity = 1024;

  void Reset() {
    head_ = size_ = 0;
    x_ = y_ = 0;
    last_dropped_.reset();
  }

  void Add(int64_t x, int64_t y, Time time, Duration width) {
    Expire(time, width);
    if (!x && !y) {
      return;
    }
    if (size_ == kCapacity) {
      last_dropped_ = events_[head_].time;
      Pop();
    }
    events_[(head_ + size_) % kCapacity] = {time, x, y};
    ++size_;
    x_ += x;
    y_ += y;
  }

  MouseVector Read(Time time, Duration width) {
    Expire(time, width);
    if (last_dropped_) {
      return {};
    }
    const double seconds = std::chrono::duration<double>(width).count();
    return {double(x_) / seconds, double(y_) / seconds};
  }

 private:
  struct Event {
    Time time;
    int64_t x;
    int64_t y;
  };

  void Pop() {
    x_ -= events_[head_].x;
    y_ -= events_[head_].y;
    head_ = (head_ + 1) % kCapacity;
    --size_;
  }

  void Expire(Time now, Duration width) {
    const auto cutoff = now - width;
    while (size_ && events_[head_].time <= cutoff) {
      Pop();
    }
    if (last_dropped_ && *last_dropped_ <= cutoff) {
      last_dropped_.reset();
    }
  }

  std::array<Event, kCapacity> events_;
  size_t head_ = 0;
  size_t size_ = 0;
  int64_t x_ = 0;
  int64_t y_ = 0;
  std::optional<Time> last_dropped_;
};

}  // namespace xe::hid::kbm

#endif  // XENIA_HID_KBM_KBM_MOUSE_WINDOW_H_
