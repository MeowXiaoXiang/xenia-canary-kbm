/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2026 Xenia authors. All rights reserved.                         *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#include "xenia/hid/kbm/kbm_input_sampling.h"

#include <algorithm>
#include <exception>
#include <system_error>
#include <utility>

#include "xenia/base/logging.h"

namespace xe::hid::kbm {

KbmInputSampling::KbmInputSampling(Writer writer, KbmInputSamplingLimits limits)
    : writer_(std::move(writer)), limits_(limits) {}

KbmInputSampling::~KbmInputSampling() { Shutdown(); }

void KbmInputSampling::Shutdown() {
  std::unique_ptr<KbmInputSamplingReport> discarded;
  {
    std::lock_guard<std::mutex> lock(mutex_);
    stopping_ = true;
    if (active_) {
      discarded.swap(active_);
      status_.active = false;
      status_.sample_count = 0;
      status_.dropped_sample_count = 0;
    }
  }
  changed_.notify_one();
  // A completed report is drained. An unfinished session is discarded, as on
  // driver shutdown previously. The worker owns no driver/UI references.
  if (worker_.joinable()) {
    worker_.join();
  }
}

std::unique_ptr<KbmInputSamplingReport> KbmInputSampling::Prepare() {
  {
    std::lock_guard<std::mutex> lock(mutex_);
    if (stopping_ || status_.writing) {
      return nullptr;
    }
  }
  auto report = std::make_unique<KbmInputSamplingReport>();
  report->samples.reserve(limits_.samples);
  report->events.reserve(limits_.events);
  std::unique_lock<std::mutex> lock(mutex_);
  if (stopping_ || status_.writing) {
    return nullptr;
  }
  if (!worker_.joinable()) {
    try {
      worker_ = std::thread([this]() { RunWriter(); });
    } catch (const std::system_error& error) {
      status_.report_write_failed = true;
      lock.unlock();
      XELOGE("kbm: could not start the input report worker: {}.", error.what());
      return nullptr;
    }
  }
  return report;
}

bool KbmInputSampling::Start(std::unique_ptr<KbmInputSamplingReport>& report) {
  std::lock_guard<std::mutex> lock(mutex_);
  if (!report || stopping_ || status_.writing || !worker_.joinable() ||
      report->samples.capacity() < limits_.samples ||
      report->events.capacity() < limits_.events ||
      report->samples.size() > limits_.samples ||
      report->events.size() > limits_.events) {
    return false;
  }
  // An old active buffer, if any, goes back to the UI caller for destruction
  // after it releases the input locks.
  active_.swap(report);
  status_ = {};
  status_.active = true;
  return true;
}

void KbmInputSampling::Record(const KbmInputSample& sample) {
  bool finished = false;
  {
    std::lock_guard<std::mutex> lock(mutex_);
    if (!active_ || sample.time < active_->started) {
      return;
    }
    if (active_->samples.size() < limits_.samples) {
      active_->samples.push_back(sample);
    } else {
      ++active_->dropped_samples;
    }
    if (sample.time >= active_->started + limits_.duration) {
      FinishLocked();
      finished = true;
    }
  }
  if (finished) {
    changed_.notify_one();
  }
}

void KbmInputSampling::Record(const KbmRawInputSample& sample) {
  std::lock_guard<std::mutex> lock(mutex_);
  if (!active_ || sample.time < active_->started) {
    return;
  }
  if (active_->events.size() < limits_.events) {
    active_->events.push_back(sample);
  } else {
    ++active_->dropped_events;
  }
}

void KbmInputSampling::FinishLocked() {
  status_.sample_count = active_->samples.size();
  status_.dropped_sample_count = active_->dropped_samples;
  status_.active = false;
  status_.writing = true;
  pending_.swap(active_);
}

bool KbmInputSampling::Finish() {
  {
    std::lock_guard<std::mutex> lock(mutex_);
    if (!active_) {
      return false;
    }
    FinishLocked();
  }
  changed_.notify_one();
  return true;
}

bool KbmInputSampling::Cancel() {
  std::unique_ptr<KbmInputSamplingReport> discarded;
  {
    std::lock_guard<std::mutex> lock(mutex_);
    if (!active_) {
      return false;
    }
    discarded.swap(active_);
    status_.active = false;
    status_.sample_count = 0;
    status_.dropped_sample_count = 0;
  }
  return true;
}

KbmInputSamplingStatus KbmInputSampling::GetStatus(
    std::chrono::steady_clock::time_point now) const {
  std::lock_guard<std::mutex> lock(mutex_);
  auto status = status_;
  if (active_) {
    status.sample_count = active_->samples.size();
    status.dropped_sample_count = active_->dropped_samples;
    status.seconds_remaining = std::max(
        0.0,
        std::chrono::duration<double>(active_->started + limits_.duration - now)
            .count());
  }
  return status;
}

void KbmInputSampling::RunWriter() {
  for (;;) {
    std::unique_ptr<KbmInputSamplingReport> report;
    {
      std::unique_lock<std::mutex> lock(mutex_);
      changed_.wait(lock, [this]() { return stopping_ || pending_; });
      if (!pending_) {
        return;
      }
      report.swap(pending_);
    }
    KbmInputReportWriteResult result;
    try {
      result = writer_(*report);
    } catch (const std::exception& error) {
      XELOGE("kbm: input sampling report writer failed: {}.", error.what());
      result = {};
    } catch (...) {
      // Keep the worker available for later sessions after a write failure.
      XELOGE("kbm: input sampling report writer raised an exception.");
      result = {};
    }
    report.reset();
    {
      std::lock_guard<std::mutex> lock(mutex_);
      status_.writing = false;
      status_.report_write_failed = !result.success;
      status_.report_path = result.success ? std::move(result.report_path) : "";
    }
  }
}

}  // namespace xe::hid::kbm
