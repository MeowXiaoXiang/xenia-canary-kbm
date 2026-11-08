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
  friend class EmulatorWindow;
  hid::kbm::KbmInputDriver* GetDriver() const;
  void RequestClose();
  void StopCapture();
  bool DrawBinding(const char* id, std::string& binding,
                   bool allow_alternatives = true);
  void DrawMouseSection();
  bool DrawMouseSettings();
  void DrawPreview(bool visible = true);
  bool DrawCaptureSettings();
  bool DrawBindings();
  bool DrawDiagnostics();
  bool DrawConfirmations();
  void MarkDraftChanged();
  bool SaveDraft();
  bool CanSaveDraft() const;
  bool preview_failed_ = false;
  bool focus_preview_stop_ = false;
  void StartBindingCapture(std::string* target, bool append, int index = -1);
  void CancelBindingCapture();
  bool HandleBindingCaptureResult();

  EmulatorWindow& emulator_window_;
  hid::kbm::KbmSettings settings_;
  bool save_failed_ = false;
  bool dirty_ = false;
  bool saved_ = false;
  bool reset_incompatible_ = false;
  bool close_requested_ = false;
  bool preview_was_active_ = false;
  bool sampling_failed_ = false;
  std::string* binding_capture_target_ = nullptr;
  bool binding_capture_append_ = false;
  int binding_capture_index_ = -1;
};

}  // namespace xe::app

#endif  // XENIA_APP_KBM_CONFIG_DIALOG_WIN_H_
