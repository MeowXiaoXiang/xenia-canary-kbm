/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2026 Xenia authors. All rights reserved.                         *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#include "xenia/hid/kbm/kbm_input_sampling.h"

#include <atomic>
#include <fstream>
#include <future>
#include <stdexcept>

#include "third_party/catch/single_include/catch2/catch.hpp"
#include "xenia/base/platform.h"

#if XE_PLATFORM_WIN32
#include "xenia/base/mutex.h"
#include "xenia/base/platform_win.h"
#include "xenia/hid/kbm/kbm_input_report_writer_win.h"
#endif

namespace xe::hid::kbm {
namespace {

using Clock = std::chrono::steady_clock;
using namespace std::chrono_literals;

std::unique_ptr<KbmInputSamplingReport> Prepare(KbmInputSampling& sampling,
                                                Clock::time_point started) {
  auto report = sampling.Prepare();
  REQUIRE(report);
  report->started = started;
  KbmRawInputSample initial;
  initial.kind = 2;
  initial.time = started;
  initial.sequence = 42;
  report->events.push_back(initial);
  return report;
}

bool WaitForWrite(KbmInputSampling& sampling) {
  const auto deadline = Clock::now() + 5s;
  while (sampling.GetStatus().writing && Clock::now() < deadline) {
    std::this_thread::sleep_for(1ms);
  }
  return !sampling.GetStatus().writing;
}

struct WriterGate {
  std::mutex mutex;
  std::condition_variable changed;
  bool entered = false;
  bool released = false;
  bool timed_out = false;

  KbmInputReportWriteResult Write(const KbmInputSamplingReport&) {
    std::unique_lock<std::mutex> lock(mutex);
    entered = true;
    changed.notify_all();
    timed_out = !changed.wait_for(lock, 5s, [this]() { return released; });
    return {!timed_out, "report.csv"};
  }
  bool Wait() {
    std::unique_lock<std::mutex> lock(mutex);
    return changed.wait_for(lock, 2s, [this]() { return entered; });
  }
  void Release() {
    std::lock_guard<std::mutex> lock(mutex);
    released = true;
    changed.notify_all();
  }
};

struct ReleaseWriter {
  WriterGate& gate;
  ~ReleaseWriter() { gate.Release(); }
};

}  // namespace

TEST_CASE("Sampling snapshots retain initial state, settings and drop counts",
          "[kbm][sampling]") {
  KbmInputSamplingReport written;
  KbmInputSampling sampling(
      [&](const auto& report) {
        written = report;
        return KbmInputReportWriteResult{true, "report.csv"};
      },
      {2, 3, 10ms});
  const auto now = Clock::now();
  auto report = Prepare(sampling, now);
  report->settings.sensitivity = 13.25;
  report->events.front().delta_x = 123;
  REQUIRE(sampling.Start(report));
  REQUIRE_FALSE(report);
  KbmInputSample poll;
  poll.time = now - 1ms;
  sampling.Record(poll);
  KbmRawInputSample event;
  event.time = now - 1ms;
  sampling.Record(event);
  for (int i = 1; i <= 4; ++i) {
    poll.time = now + std::chrono::milliseconds(i);
    poll.raw_delta_x = i;
    sampling.Record(poll);
    event.time = poll.time;
    event.sequence = 42 + i;
    sampling.Record(event);
  }
  REQUIRE(sampling.GetStatus(now + 4ms).seconds_remaining == Approx(0.006));
  REQUIRE(sampling.Finish());
  REQUIRE(WaitForWrite(sampling));
  REQUIRE(written.samples.size() == 2);
  REQUIRE(written.samples[0].raw_delta_x == 1);
  REQUIRE(written.dropped_samples == 2);
  REQUIRE(written.events.size() == 3);
  REQUIRE(written.dropped_events == 2);
  REQUIRE(written.events[0].kind == 2);
  REQUIRE(written.events[0].sequence == 42);
  REQUIRE(written.events[0].delta_x == 123);
  REQUIRE(written.settings.sensitivity == 13.25);
  REQUIRE(sampling.GetStatus().sample_count == 2);
  REQUIRE(sampling.GetStatus().dropped_sample_count == 2);
}

TEST_CASE("Deadline handoff neither copies buffers nor waits for report I/O",
          "[kbm][sampling]") {
  WriterGate gate;
  const KbmInputSample* sample_data = nullptr;
  std::atomic<bool> same_buffer{false};
  KbmInputSampling sampling(
      [&](const auto& report) {
        same_buffer = report.samples.data() == sample_data;
        return gate.Write(report);
      },
      {2, 3, 5ms});
  ReleaseWriter release{gate};
  auto report = Prepare(sampling, Clock::now());
  const auto now = report->started;
  sample_data = report->samples.data();
  auto standby = Prepare(sampling, now + 10ms);
  REQUIRE(sampling.Start(report));
  // The writer must run even while an input producer holds the global region.
#if XE_PLATFORM_WIN32
  auto global_lock = xe::global_critical_region::AcquireDirect();
#endif
  KbmInputSample poll;
  poll.time = now + 5ms;
  sampling.Record(poll);
  REQUIRE(gate.Wait());
  REQUIRE(same_buffer.load());
  REQUIRE_FALSE(sampling.GetStatus().active);
  REQUIRE(sampling.GetStatus().writing);
  REQUIRE_FALSE(sampling.Prepare());
  REQUIRE_FALSE(sampling.Start(standby));
  REQUIRE_FALSE(sampling.Finish());
  REQUIRE_FALSE(sampling.Cancel());
  sampling.Record(poll);
  REQUIRE(sampling.GetStatus().writing);
  REQUIRE(sampling.GetStatus().sample_count == 1);
  gate.Release();
  REQUIRE(WaitForWrite(sampling));
  REQUIRE_FALSE(gate.timed_out);
  REQUIRE(sampling.GetStatus().report_path == "report.csv");
  auto next = Prepare(sampling, now + 10ms);
  REQUIRE(sampling.Start(next));
  REQUIRE(sampling.GetStatus().report_path.empty());
}

TEST_CASE("Cancel and active restart do not publish discarded sessions",
          "[kbm][sampling]") {
  std::atomic<int> writes{0};
  double sensitivity = 0;
  KbmInputSampling sampling(
      [&](const auto& report) {
        ++writes;
        sensitivity = report.settings.sensitivity;
        return KbmInputReportWriteResult{true, "latest.csv"};
      },
      {2, 3, 10ms});
  auto first = Prepare(sampling, Clock::now());
  REQUIRE(sampling.Start(first));
  REQUIRE(sampling.Cancel());
  REQUIRE_FALSE(sampling.Finish());
  REQUIRE_FALSE(sampling.GetStatus().active);
  REQUIRE(writes.load() == 0);
  auto second = Prepare(sampling, Clock::now());
  REQUIRE(sampling.Start(second));
  auto third = Prepare(sampling, Clock::now());
  third->settings.sensitivity = 17.5;
  REQUIRE(sampling.Start(third));
  REQUIRE(third);  // The UI caller disposes of the replaced session.
  REQUIRE(sampling.Finish());
  REQUIRE(WaitForWrite(sampling));
  REQUIRE(writes.load() == 1);
  REQUIRE(sensitivity == 17.5);
}

TEST_CASE("Full-capacity automatic completion transfers the original buffers",
          "[kbm][sampling]") {
  WriterGate gate;
  const KbmInputSample* polls = nullptr;
  const KbmRawInputSample* events = nullptr;
  std::atomic<bool> original_buffers{false};
  KbmInputSampling sampling([&](const auto& report) {
    original_buffers =
        report.samples.data() == polls && report.events.data() == events &&
        report.samples.size() == 120000 && report.events.size() == 600000;
    return gate.Write(report);
  });
  ReleaseWriter release{gate};
  auto report = Prepare(sampling, Clock::now());
  report->samples.resize(120000);
  report->events.resize(600000);
  polls = report->samples.data();
  events = report->events.data();
  const auto deadline = report->started + 60s;
  REQUIRE(sampling.Start(report));
  KbmInputSample poll;
  poll.time = deadline;
  const auto before = Clock::now();
  sampling.Record(poll);
  const auto handoff_us =
      std::chrono::duration<double, std::micro>(Clock::now() - before).count();
  INFO("Full-capacity completion handoff: " << handoff_us << " us");
  REQUIRE(gate.Wait());
  REQUIRE(original_buffers.load());
  REQUIRE(sampling.GetStatus().writing);
  REQUIRE(sampling.GetStatus().dropped_sample_count == 1);
  gate.Release();
  REQUIRE(WaitForWrite(sampling));
  REQUIRE_FALSE(gate.timed_out);
}

TEST_CASE(
    "A failed writer reports failure and permits a later successful session",
    "[kbm][sampling]") {
  const bool raises_exception = GENERATE(false, true);
  std::atomic<int> calls{0};
  KbmInputSampling sampling(
      [&](const auto&) {
        if (++calls == 1) {
          if (raises_exception) {
            throw std::runtime_error("injected report failure");
          }
          return KbmInputReportWriteResult{};
        }
        return KbmInputReportWriteResult{true, "recovered.csv"};
      },
      {2, 3, 10ms});
  for (int attempt = 0; attempt < 2; ++attempt) {
    auto report = Prepare(sampling, Clock::now());
    REQUIRE(sampling.Start(report));
    REQUIRE_FALSE(sampling.GetStatus().report_write_failed);
    REQUIRE(sampling.Finish());
    REQUIRE(WaitForWrite(sampling));
    REQUIRE(sampling.GetStatus().report_write_failed == (attempt == 0));
  }
  REQUIRE(sampling.GetStatus().report_path == "recovered.csv");
}

TEST_CASE("Shutdown drains completed reports and discards unfinished sessions",
          "[kbm][sampling]") {
  std::atomic<int> writes{0};
  auto writer = [&](const auto&) {
    ++writes;
    return KbmInputReportWriteResult{true, "report.csv"};
  };
  {
    KbmInputSampling sampling(writer, {2, 3, 10ms});
    auto report = Prepare(sampling, Clock::now());
    REQUIRE(sampling.Start(report));
    REQUIRE(sampling.Finish());
  }
  REQUIRE(writes.load() == 1);
  {
    KbmInputSampling sampling(writer, {2, 3, 10ms});
    auto report = Prepare(sampling, Clock::now());
    REQUIRE(sampling.Start(report));
  }
  REQUIRE(writes.load() == 1);
}

TEST_CASE("Explicit shutdown waits for an accepted writer before quick exit",
          "[kbm][sampling]") {
  WriterGate gate;
  KbmInputSampling sampling(
      [&](const auto& report) { return gate.Write(report); }, {2, 3, 10ms});
  ReleaseWriter initial_release{gate};
  auto report = Prepare(sampling, Clock::now());
  auto standby = Prepare(sampling, Clock::now());
  REQUIRE(sampling.Start(report));
  REQUIRE(sampling.Finish());
  REQUIRE(gate.Wait());
  auto shutdown =
      std::async(std::launch::async, [&]() { sampling.Shutdown(); });
  ReleaseWriter release_before_future_destructor{gate};
  REQUIRE(shutdown.wait_for(20ms) == std::future_status::timeout);
  gate.Release();
  shutdown.get();
  REQUIRE_FALSE(gate.timed_out);
  REQUIRE_FALSE(sampling.GetStatus().writing);
  REQUIRE(sampling.GetStatus().report_path == "report.csv");
  REQUIRE_FALSE(sampling.Prepare());
  REQUIRE_FALSE(sampling.Start(standby));
  REQUIRE_FALSE(sampling.Finish());
  sampling.Shutdown();
}

TEST_CASE(
    "Explicit shutdown discards active samples and rejects later producers",
    "[kbm][sampling]") {
  std::atomic<int> writes{0};
  KbmInputSampling sampling(
      [&](const auto&) {
        ++writes;
        return KbmInputReportWriteResult{true, "unexpected.csv"};
      },
      {2, 3, 10ms});
  auto report = Prepare(sampling, Clock::now());
  REQUIRE(sampling.Start(report));
  sampling.Shutdown();
  KbmInputSample late;
  late.time = Clock::now() + 1s;
  sampling.Record(late);
  sampling.Shutdown();
  REQUIRE_FALSE(sampling.GetStatus().active);
  REQUIRE_FALSE(sampling.GetStatus().writing);
  REQUIRE_FALSE(sampling.Finish());
  REQUIRE_FALSE(sampling.Prepare());
  REQUIRE(writes.load() == 0);
}

#if XE_PLATFORM_WIN32
namespace {

struct ReportDirectory {
  std::filesystem::path path =
      std::filesystem::temp_directory_path() /
      ("xenia-kbm-report-test-" + std::to_string(GetCurrentProcessId()) + "-" +
       std::to_string(Clock::now().time_since_epoch().count()));
  ReportDirectory() { REQUIRE(std::filesystem::create_directory(path)); }
  ~ReportDirectory() {
    std::error_code ignored;
    std::filesystem::remove_all(path, ignored);
  }
};

std::string Read(const std::filesystem::path& path) {
  std::ifstream file(path, std::ios::binary);
  return {std::istreambuf_iterator<char>(file), {}};
}

}  // namespace

TEST_CASE(
    "Published report pairs match the existing CSV serializer byte for byte",
    "[kbm][sampling]") {
  ReportDirectory directory;
  KbmInputSamplingReport report;
  report.directory = directory.path;
  report.started = Clock::now();
  KbmInputSample sample;
  sample.time = report.started + 8ms;
  sample.raw_delta_x = 24;
  sample.input_active = true;
  report.samples.push_back(sample);
  KbmRawInputSample event;
  event.time = report.started;
  event.kind = 2;
  event.reset_pending = true;
  report.events.push_back(event);
  report.dropped_samples = 2;
  report.dropped_events = 3;
  auto expected = SerializeKbmInputReport(report.samples, report.events, 2, 3,
                                          report.settings, report.started);
  auto path = directory.path / "report.csv";
  auto result = WriteKbmInputSamplingReportFiles(report, path);
  REQUIRE(result.success);
  REQUIRE(Read(path) == expected.poll_csv);
  auto events_path = directory.path / "report.events.csv";
  REQUIRE(Read(events_path) == expected.events_csv);
  REQUIRE_FALSE(std::filesystem::exists(directory.path / "report.csv.tmp"));
  REQUIRE_FALSE(
      std::filesystem::exists(directory.path / "report.events.csv.tmp"));
  const auto first = WriteKbmInputSamplingReport(report);
  const auto second = WriteKbmInputSamplingReport(report);
  REQUIRE(first.success);
  REQUIRE(second.success);
  REQUIRE(first.report_path != second.report_path);
}

TEST_CASE(
    "Report publication failure preserves existing files and removes owned "
    "files",
    "[kbm][sampling]") {
  const auto conflict =
      GENERATE("report.csv", "report.events.csv", "report.csv.tmp");
  ReportDirectory directory;
  const auto protected_path = directory.path / conflict;
  {
    std::ofstream file(protected_path, std::ios::binary);
    file << "previous data";
  }
  KbmInputSamplingReport report;
  report.started = Clock::now();
  REQUIRE_FALSE(
      WriteKbmInputSamplingReportFiles(report, directory.path / "report.csv")
          .success);
  REQUIRE(Read(protected_path) == "previous data");
  REQUIRE(std::distance(std::filesystem::directory_iterator(directory.path),
                        std::filesystem::directory_iterator()) == 1);
}

TEST_CASE(
    "Invalid report directories fail without publishing a completion marker",
    "[kbm][sampling]") {
  ReportDirectory directory;
  const auto blocker = directory.path / "not-a-directory";
  {
    std::ofstream file(blocker);
    file << "keep";
  }
  KbmInputSamplingReport report;
  report.started = Clock::now();
  REQUIRE_FALSE(
      WriteKbmInputSamplingReportFiles(report, blocker / "report.csv").success);
  REQUIRE(Read(blocker) == "keep");
}
TEST_CASE("Report output supports Unicode directories", "[kbm][sampling]") {
  ReportDirectory directory;
  KbmInputSamplingReport report;
  report.directory = directory.path / L"測試報告";
  report.started = Clock::now();
  const auto result = WriteKbmInputSamplingReport(report);
  REQUIRE(result.success);
  REQUIRE(std::filesystem::exists(std::filesystem::u8path(result.report_path)));
}

TEST_CASE(
    "The production report worker runs outside the emulator global region",
    "[kbm][sampling]") {
  ReportDirectory directory;
  KbmInputSampling sampling(WriteKbmInputSamplingReport, {2, 3, 10ms});
  auto report = Prepare(sampling, Clock::now());
  report->directory = directory.path;
  REQUIRE(sampling.Start(report));
  auto global_lock = xe::global_critical_region::AcquireDirect();
  REQUIRE(sampling.Finish());
  REQUIRE(WaitForWrite(sampling));
  const auto status = sampling.GetStatus();
  REQUIRE_FALSE(status.report_write_failed);
  REQUIRE(std::filesystem::exists(std::filesystem::u8path(status.report_path)));
}
#endif  // XE_PLATFORM_WIN32

}  // namespace xe::hid::kbm
