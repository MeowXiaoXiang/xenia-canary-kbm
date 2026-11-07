/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2026 Xenia authors. All rights reserved.                         *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#include "xenia/hid/kbm/kbm_input_report_writer_win.h"

#include <algorithm>
#include <atomic>
#include <utility>

#include "third_party/fmt/include/fmt/format.h"
#include "xenia/base/filesystem.h"
#include "xenia/base/logging.h"
#include "xenia/base/platform_win.h"

namespace xe::hid::kbm {
namespace {

class OwnedFile {
 public:
  explicit OwnedFile(std::filesystem::path path) : path_(std::move(path)) {}
  OwnedFile(const OwnedFile&) = delete;
  OwnedFile& operator=(const OwnedFile&) = delete;
  ~OwnedFile() {
    Close();
    if (owned_) {
      DeleteFileW(path_.c_str());
    }
  }
  bool Open() {
    handle_ = CreateFileW(path_.c_str(), GENERIC_WRITE, FILE_SHARE_READ,
                          nullptr, CREATE_NEW, FILE_ATTRIBUTE_NORMAL, nullptr);
    owned_ = handle_ != INVALID_HANDLE_VALUE;
    return owned_;
  }
  bool Write(const std::string& text) {
    size_t offset = 0;
    while (offset < text.size()) {
      DWORD size =
          static_cast<DWORD>(std::min<size_t>(text.size() - offset, MAXDWORD));
      DWORD written = 0;
      if (!WriteFile(handle_, text.data() + offset, size, &written, nullptr) ||
          written == 0) {
        return false;
      }
      offset += written;
    }
    return true;
  }
  bool Close() {
    if (handle_ == INVALID_HANDLE_VALUE) {
      return true;
    }
    HANDLE handle = handle_;
    handle_ = INVALID_HANDLE_VALUE;
    return CloseHandle(handle) != FALSE;
  }
  bool Publish(const std::filesystem::path& destination) {
    auto published_path = destination;
    if (!MoveFileExW(path_.c_str(), destination.c_str(),
                     MOVEFILE_WRITE_THROUGH)) {
      return false;
    }
    path_.swap(published_path);
    return true;
  }
  void Release() { owned_ = false; }

 private:
  std::filesystem::path path_;
  HANDLE handle_ = INVALID_HANDLE_VALUE;
  bool owned_ = false;
};

}  // namespace

KbmInputReportWriteResult WriteKbmInputSamplingReportFiles(
    const KbmInputSamplingReport& report,
    const std::filesystem::path& report_path) {
  if (report_path.empty()) {
    return {};
  }
  auto result_path = xe::path_to_utf8(report_path);
  std::error_code error;
  if (!report_path.parent_path().empty()) {
    std::filesystem::create_directories(report_path.parent_path(), error);
    if (error) {
      return {};
    }
  }
  const auto contents = SerializeKbmInputReport(
      report.samples, report.events, report.dropped_samples,
      report.dropped_events, report.settings, report.started);
  auto events_path = report_path;
  events_path.replace_extension(".events.csv");
  auto event_temporary = events_path;
  event_temporary += ".tmp";
  auto poll_temporary = report_path;
  poll_temporary += ".tmp";
  OwnedFile events(event_temporary);
  OwnedFile polls(poll_temporary);
  if (!events.Open() || !events.Write(contents.events_csv) || !events.Close() ||
      !polls.Open() || !polls.Write(contents.poll_csv) || !polls.Close() ||
      !events.Publish(events_path) || !polls.Publish(report_path)) {
    return {};
  }
  events.Release();
  polls.Release();
  return {true, std::move(result_path)};
}

KbmInputReportWriteResult WriteKbmInputSamplingReport(
    const KbmInputSamplingReport& report) {
  if (report.directory.empty()) {
    XELOGE("kbm: no directory available for the input sampling report.");
    return {};
  }
  static std::atomic<int64_t> last_timestamp{0};
  int64_t previous = last_timestamp.load();
  int64_t timestamp;
  do {
    const auto now = std::chrono::duration_cast<std::chrono::microseconds>(
                         std::chrono::system_clock::now().time_since_epoch())
                         .count();
    timestamp = std::max<int64_t>(now, previous + 1);
  } while (!last_timestamp.compare_exchange_weak(previous, timestamp));
  const auto path =
      report.directory / fmt::format("kbm-input-report-{}.csv", timestamp);
  auto result = WriteKbmInputSamplingReportFiles(report, path);
  if (result.success) {
    XELOGI("kbm: saved input sampling report '{}'.", path);
  } else {
    XELOGE("kbm: failed to write input sampling report '{}'.", path);
  }
  return result;
}

}  // namespace xe::hid::kbm
