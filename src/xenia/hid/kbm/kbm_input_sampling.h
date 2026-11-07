/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2026 Xenia authors. All rights reserved.                         *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#ifndef XENIA_HID_KBM_KBM_INPUT_SAMPLING_H_
#define XENIA_HID_KBM_KBM_INPUT_SAMPLING_H_

#include <condition_variable>
#include <filesystem>
#include <functional>
#include <memory>
#include <mutex>
#include <thread>

#include "xenia/hid/kbm/kbm_input_report.h"

namespace xe::hid::kbm {

struct KbmInputSamplingReport {
  std::vector<KbmInputSample> samples;
  std::vector<KbmRawInputSample> events;
  KbmInputReportSettings settings;
  std::filesystem::path directory;
  std::chrono::steady_clock::time_point started;
  size_t dropped_samples = 0;
  size_t dropped_events = 0;
};

struct KbmInputSamplingStatus {
  bool active = false;
  bool writing = false;
  bool report_write_failed = false;
  size_t sample_count = 0;
  size_t dropped_sample_count = 0;
  double seconds_remaining = 0.0;
  std::string report_path;
};

struct KbmInputReportWriteResult {
  bool success = false;
  std::string report_path;
};

struct KbmInputSamplingLimits {
  size_t samples = 120000;
  size_t events = 600000;
  std::chrono::milliseconds duration{60000};
};

// Input producers may hold the emulator's global critical region. The private
// mutex protects only session ownership and status; the worker never acquires
// the global region and never holds this mutex during serialization or I/O.
class KbmInputSampling {
 public:
  using Writer =
      std::function<KbmInputReportWriteResult(const KbmInputSamplingReport&)>;

  explicit KbmInputSampling(Writer writer, KbmInputSamplingLimits limits = {});
  ~KbmInputSampling();

  // UI path, before acquiring input locks: allocate buffers and start the lazy
  // worker. Start swaps ownership while the caller snapshots initial motion.
  std::unique_ptr<KbmInputSamplingReport> Prepare();
  bool Start(std::unique_ptr<KbmInputSamplingReport>& report);
  void Record(const KbmInputSample& sample);
  void Record(const KbmRawInputSample& sample);
  // Returns whether a report was handed off, not whether its write succeeded.
  bool Finish();
  bool Cancel();
  // Owner/UI thread, without input locks. Safe to call again before
  // destruction.
  void Shutdown();
  KbmInputSamplingStatus GetStatus(std::chrono::steady_clock::time_point now =
                                       std::chrono::steady_clock::now()) const;

 private:
  void FinishLocked();
  void RunWriter();

  const Writer writer_;
  const KbmInputSamplingLimits limits_;
  mutable std::mutex mutex_;
  std::condition_variable changed_;
  std::unique_ptr<KbmInputSamplingReport> active_;
  std::unique_ptr<KbmInputSamplingReport> pending_;
  KbmInputSamplingStatus status_;
  bool stopping_ = false;
  std::thread worker_;
};

}  // namespace xe::hid::kbm

#endif  // XENIA_HID_KBM_KBM_INPUT_SAMPLING_H_
