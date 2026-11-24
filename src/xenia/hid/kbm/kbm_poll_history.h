/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2026 Xenia authors. All rights reserved.                         *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#ifndef XENIA_HID_KBM_KBM_POLL_HISTORY_H_
#define XENIA_HID_KBM_KBM_POLL_HISTORY_H_

#include <array>
#include <chrono>
#include <cstddef>
#include <vector>

namespace xe::hid::kbm {

template <typename Value, size_t Capacity>
class KbmRecentHistory {
 public:
  using Clock = std::chrono::steady_clock;
  static constexpr size_t kCapacity = Capacity;
  static constexpr auto kDuration = std::chrono::seconds(5);

  void Clear() { next_ = size_ = 0; }

  void Record(Clock::time_point time, Value value) {
    entries_[next_] = {time, value};
    next_ = (next_ + 1) % entries_.size();
    if (size_ < entries_.size()) {
      ++size_;
    }
  }

  // The caller can reserve the result before taking its input lock.
  void CopyRecent(Clock::time_point now, std::vector<Value>& values) const {
    const auto cutoff = now - kDuration;
    for (size_t offset = 1; offset <= size_; ++offset) {
      const auto& entry =
          entries_[(next_ + entries_.size() - offset) % entries_.size()];
      if (entry.time < cutoff) {
        break;
      }
      values.push_back(entry.value);
    }
  }

 private:
  struct Entry {
    Clock::time_point time;
    Value value;
  };

  std::array<Entry, kCapacity> entries_{};
  size_t next_ = 0;
  size_t size_ = 0;
};

using KbmPollHistory = KbmRecentHistory<double, 1024>;

}  // namespace xe::hid::kbm

#endif  // XENIA_HID_KBM_KBM_POLL_HISTORY_H_
