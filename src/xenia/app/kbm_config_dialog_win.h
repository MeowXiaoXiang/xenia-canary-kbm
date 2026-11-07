/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2026 Xenia authors. All rights reserved.                         *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#ifndef XENIA_APP_KBM_CONFIG_DIALOG_WIN_H_
#define XENIA_APP_KBM_CONFIG_DIALOG_WIN_H_

#include <string>

#include "xenia/hid/kbm/kbm_config.h"
#include "xenia/ui/imgui_dialog.h"

namespace xe::hid::kbm {
class KbmInputDriver;
}  // namespace xe::hid::kbm

namespace xe::app {

class EmulatorWindow;

class KbmConfigDialog final : public ui::ImGuiDialog {
 public:
  KbmConfigDialog(ui::ImGuiDrawer* imgui_drawer,
                  EmulatorWindow& emulator_window);
  ~KbmConfigDialog() override;

 protected:
  void OnDraw(ImGuiIO& io) override;

 private:
  hid::kbm::KbmInputDriver* GetDriver() const;
  void ApplyDraft();
  bool SaveDraft();
  bool preview_failed_ = false;
  bool focus_preview_stop_ = false;
  void RestoreOriginal();
  void StartBindingCapture(std::string* target, bool append);
  void CancelBindingCapture();
  bool HandleBindingCaptureResult();

  EmulatorWindow& emulator_window_;
  hid::kbm::KbmSettings original_settings_;
  hid::kbm::KbmSettings settings_;
  bool committed_or_restored_ = false;
  bool save_failed_ = false;
  bool dirty_ = false;
  bool saved_ = false;
  std::string* binding_capture_target_ = nullptr;
  bool binding_capture_append_ = false;
};

}  // namespace xe::app

#endif  // XENIA_APP_KBM_CONFIG_DIALOG_WIN_H_
