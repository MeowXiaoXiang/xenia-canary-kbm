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

class KbmPollHistory {
 public:
  using Clock = std::chrono::steady_clock;
  static constexpr size_t kCapacity = 1024;

  void Record(Clock::time_point time, double interval_ms) {
    entries_[next_] = {time, interval_ms};
    next_ = (next_ + 1) % entries_.size();
    if (size_ < entries_.size()) {
      ++size_;
    }
  }

  // The caller can reserve the result before taking its input lock.
  void CopyRecent(Clock::time_point now, std::vector<double>& intervals) const {
    const auto cutoff = now - std::chrono::seconds(5);
    for (size_t offset = 1; offset <= size_; ++offset) {
      const auto& entry =
          entries_[(next_ + entries_.size() - offset) % entries_.size()];
      if (entry.time < cutoff) {
        break;
      }
      intervals.push_back(entry.interval_ms);
    }
  }

 private:
  struct Entry {
    Clock::time_point time;
    double interval_ms;
  };

  std::array<Entry, kCapacity> entries_{};
  size_t next_ = 0;
  size_t size_ = 0;
};

}  // namespace xe::hid::kbm

#endif  // XENIA_HID_KBM_KBM_POLL_HISTORY_H_
