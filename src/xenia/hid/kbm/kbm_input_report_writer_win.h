/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2026 Xenia authors. All rights reserved.                         *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#ifndef XENIA_HID_KBM_KBM_INPUT_REPORT_WRITER_WIN_H_
#define XENIA_HID_KBM_KBM_INPUT_REPORT_WRITER_WIN_H_

#include "xenia/hid/kbm/kbm_input_sampling.h"

namespace xe::hid::kbm {

KbmInputReportWriteResult WriteKbmInputSamplingReport(
    const KbmInputSamplingReport& report);
// Publish the event file first and the poll file last as the completion marker.
// Existing files are never replaced; failed publication cleans up owned files.
KbmInputReportWriteResult WriteKbmInputSamplingReportFiles(
    const KbmInputSamplingReport& report,
    const std::filesystem::path& report_path);

}  // namespace xe::hid::kbm

#endif  // XENIA_HID_KBM_KBM_INPUT_REPORT_WRITER_WIN_H_
