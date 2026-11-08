/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2026 Xenia authors. All rights reserved.                         *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#include "xenia/app/kbm_config_dialog_win.h"

#include <algorithm>
#include <cfloat>
#include <cmath>
#include <sstream>
#include <vector>

#include "third_party/fmt/include/fmt/format.h"
#include "third_party/imgui/imgui.h"
#include "xenia/app/emulator_window.h"
#include "xenia/app/localization.h"
#include "xenia/hid/input_system.h"
#include "xenia/hid/kbm/kbm_input_driver.h"

namespace xe::app {

namespace {
using localization::StringId;
const char* Tr(StringId id) { return localization::Get(id); }

std::vector<std::string> BindingTokens(const std::string& binding) {
  std::istringstream stream(binding);
  std::vector<std::string> tokens;
  for (std::string token; stream >> token;) {
    tokens.push_back(std::move(token));
  }
  return tokens;
}

bool BeginCard(const char* id) {
  return ImGui::BeginChild(
      id, ImVec2(0, 0), ImGuiChildFlags_Borders | ImGuiChildFlags_AutoResizeY);
}

bool CollapsingSection(const char* id, StringId title,
                       ImGuiTreeNodeFlags flags = 0) {
  const auto label = fmt::format("{}###{}", Tr(title), id);
  return ImGui::CollapsingHeader(label.c_str(), flags);
}

void Error(StringId text) {
  ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(1, 0.55f, 0.4f, 1));
  ImGui::TextWrapped("%s", Tr(text));
  ImGui::PopStyleColor();
}

void NextButton(float width) {
  const float right =
      ImGui::GetCursorScreenPos().x + ImGui::GetContentRegionAvail().x;
  if (ImGui::GetItemRectMax().x + ImGui::GetStyle().ItemSpacing.x + width <=
      right) {
    ImGui::SameLine();
  }
}

float ButtonWidth(StringId label) {
  return ImGui::CalcTextSize(Tr(label)).x +
         ImGui::GetStyle().FramePadding.x * 2;
}
}  // namespace

KbmConfigDialog::KbmConfigDialog(ui::ImGuiDrawer* imgui_drawer,
                                 EmulatorWindow& emulator_window)
    : ui::ImGuiDialog(imgui_drawer), emulator_window_(emulator_window) {
  settings_ = hid::kbm::GetSettingsFromCvars();
  original_settings_ = settings_;
  saved_ = hid::kbm::GetConfigState() == hid::kbm::KbmConfigState::kCompatible;
}

KbmConfigDialog::~KbmConfigDialog() {
  if (auto* driver = GetDriver()) {
    driver->StopMousePreview();
  }
  CancelBindingCapture();
  if (!committed_or_restored_) {
    RestoreOriginal();
  }
}

hid::kbm::KbmInputDriver* KbmConfigDialog::GetDriver() const {
  hid::InputSystem* input_system = emulator_window_.emulator()->input_system();
  return input_system ? input_system->GetDriver<hid::kbm::KbmInputDriver>()
                      : nullptr;
}

void KbmConfigDialog::ApplyDraft() {
  committed_or_restored_ = false;
  dirty_ = true;
  saved_ = false;
  settings_ = hid::kbm::NormalizeSettings(settings_);
  if (auto* driver = GetDriver()) {
    driver->ResetMousePreview();
  }
}

void KbmConfigDialog::RestoreOriginal() {
  settings_ = original_settings_;
  committed_or_restored_ = true;
}

bool KbmConfigDialog::CanSaveDraft() const {
  return hid::kbm::CanSaveKbmConfig(hid::kbm::GetConfigState(),
                                    reset_incompatible_);
}

bool KbmConfigDialog::SaveDraft() {
  if (!CanSaveDraft()) {
    return false;
  }
  CancelBindingCapture();
  if (auto* driver = GetDriver()) {
    driver->StopMousePreview();
  }
  const bool success = hid::kbm::CommitKbmSettings(
      settings_,
      [this](const auto& settings) {
        return hid::kbm::SaveConfig(settings, reset_incompatible_);
      },
      [this](const auto& settings) {
        if (auto* driver = GetDriver()) {
          driver->ApplySettings(settings);
        } else {
          hid::kbm::ApplySettingsToCvars(settings);
        }
      });
  save_failed_ = !success;
  if (success) {
    reset_incompatible_ = false;
    original_settings_ = settings_;
    committed_or_restored_ = true;
    dirty_ = false;
    saved_ = true;
  }
  return success;
}

void KbmConfigDialog::StartBindingCapture(std::string* target, bool append,
                                          int index) {
  if (auto* driver = GetDriver()) {
    driver->StopMousePreview();
  }
  binding_capture_target_ = target;
  binding_capture_append_ = append;
  binding_capture_index_ = index;
  if (auto* driver = GetDriver()) {
    driver->BeginBindingCapture();
  }
}

void KbmConfigDialog::CancelBindingCapture() {
  if (auto* driver = GetDriver()) {
    driver->CancelBindingCapture();
  }
  binding_capture_target_ = nullptr;
  binding_capture_append_ = false;
  binding_capture_index_ = -1;
}

bool KbmConfigDialog::HandleBindingCaptureResult() {
  auto* driver = GetDriver();
  if (!driver || !binding_capture_target_) {
    return false;
  }
  const auto result = driver->ConsumeBindingCaptureResult();
  using Status = hid::kbm::KbmInputDriver::BindingCaptureStatus;
  switch (result.status) {
    case Status::kCaptured:
      if (binding_capture_index_ >= 0) {
        auto tokens = BindingTokens(*binding_capture_target_);
        if (size_t(binding_capture_index_) < tokens.size()) {
          tokens[binding_capture_index_] = result.value;
          binding_capture_target_->clear();
          for (const auto& token : tokens) {
            if (!binding_capture_target_->empty()) {
              *binding_capture_target_ += " ";
            }
            *binding_capture_target_ += token;
          }
        }
      } else if (binding_capture_append_ && !binding_capture_target_->empty()) {
        *binding_capture_target_ += " " + result.value;
      } else {
        *binding_capture_target_ = result.value;
      }
      binding_capture_target_ = nullptr;
      binding_capture_append_ = false;
      binding_capture_index_ = -1;
      return true;
    case Status::kCleared:
      binding_capture_target_->clear();
      binding_capture_target_ = nullptr;
      binding_capture_append_ = false;
      binding_capture_index_ = -1;
      return true;
    case Status::kCancelled:
      binding_capture_target_ = nullptr;
      binding_capture_append_ = false;
      binding_capture_index_ = -1;
      return false;
    default:
      return false;
  }
}

void KbmConfigDialog::StopCapture() {
  if (auto* driver = GetDriver()) {
    driver->StopMousePreview();
  }
  CancelBindingCapture();
}

void KbmConfigDialog::RequestClose() {
  StopCapture();
  close_requested_ = true;
}

bool KbmConfigDialog::DrawBinding(const char* id, std::string& binding,
                                  bool allow_alternatives) {
  bool changed = false;
  ImGui::PushID(id);
  const bool capturing = binding_capture_target_ == &binding;
  if (ImGui::BeginTable("Binding", 2, ImGuiTableFlags_SizingStretchProp)) {
    ImGui::TableSetupColumn("Value", ImGuiTableColumnFlags_WidthStretch);
    const float actions_width =
        ImGui::CalcTextSize("...").x + ImGui::GetStyle().FramePadding.x * 2;
    ImGui::TableSetupColumn("Actions", ImGuiTableColumnFlags_WidthFixed,
                            actions_width);
    ImGui::TableNextRow();
    ImGui::TableSetColumnIndex(0);
    auto tokens = BindingTokens(binding);
    ImGui::BeginDisabled(!GetDriver());
    if (tokens.empty() || (capturing && binding_capture_append_)) {
      const char* label = capturing ? Tr(StringId::kKbmPressBinding)
                                    : Tr(StringId::kKbmUnbound);
      const float width = std::min(
          ImGui::GetContentRegionAvail().x,
          ImGui::CalcTextSize(label).x + ImGui::GetStyle().FramePadding.x * 2);
      if (ImGui::Button(label, ImVec2(width, 0))) {
        StartBindingCapture(&binding, false);
      }
    } else {
      const float available = ImGui::GetContentRegionAvail().x;
      for (size_t i = 0; i < tokens.size(); ++i) {
        ImGui::PushID(int(i));
        const bool capturing_token =
            capturing &&
            (binding_capture_index_ == int(i) || binding_capture_index_ < 0);
        const std::string label = capturing_token
                                      ? Tr(StringId::kKbmPressBinding)
                                      : hid::kbm::FormatKbmBinding(tokens[i]);
        const float width =
            std::min(available, ImGui::CalcTextSize(label.c_str()).x +
                                    ImGui::GetStyle().FramePadding.x * 2);
        if (i) {
          NextButton(width);
        }
        if (ImGui::Button((label + "##Value").c_str(), ImVec2(width, 0))) {
          StartBindingCapture(&binding, false, int(i));
        }
        if (ImGui::IsItemHovered()) {
          ImGui::SetTooltip("%s\n%s", label.c_str(),
                            Tr(StringId::kKbmBindingTooltip));
        }
        ImGui::PopID();
      }
    }
    ImGui::EndDisabled();
    ImGui::TableSetColumnIndex(1);
    if (ImGui::Button("...##Actions", ImVec2(actions_width, 0))) {
      ImGui::OpenPopup("Actions");
    }
    if (ImGui::IsItemHovered()) {
      ImGui::SetTooltip("%s", Tr(StringId::kKbmBindingActions));
    }
    if (ImGui::BeginPopup("Actions")) {
      if (allow_alternatives &&
          ImGui::MenuItem(Tr(StringId::kKbmAddAlternativeBinding), nullptr,
                          false, GetDriver() != nullptr)) {
        StartBindingCapture(&binding, true);
      }
      if (capturing && ImGui::MenuItem(Tr(StringId::kKbmCancelCapture))) {
        CancelBindingCapture();
      }
      if (ImGui::MenuItem(Tr(StringId::kKbmBindEscape))) {
        CancelBindingCapture();
        binding = "Key.Escape";
        changed = true;
      }
      if (ImGui::MenuItem(Tr(StringId::kKbmClearBinding))) {
        CancelBindingCapture();
        binding.clear();
        changed = true;
      }
      ImGui::EndPopup();
    }
    ImGui::EndTable();
  }
  ImGui::PopID();
  return changed;
}

void KbmConfigDialog::DrawMouseSection() {
  if (!CollapsingSection("KbmMouseSection", StringId::kKbmMouse,
                         ImGuiTreeNodeFlags_DefaultOpen)) {
    if (binding_capture_target_ == &settings_.raw_mouse_capture_toggle_key) {
      CancelBindingCapture();
    }
    DrawPreview(false);
    return;
  }
  if (DrawMouseSettings()) {
    ApplyDraft();
  }
  ImGui::Separator();
  if (DrawCaptureSettings()) {
    ApplyDraft();
  }
  ImGui::Separator();
  DrawPreview();
}

bool KbmConfigDialog::DrawMouseSettings() {
  bool changed = false;
  ImGui::BeginDisabled(!settings_.enabled);
  changed |=
      ImGui::Checkbox(Tr(StringId::kKbmEnableRawMouse), &settings_.raw_mouse);
  ImGui::EndDisabled();
  if (!settings_.enabled) {
    ImGui::TextWrapped("%s", Tr(StringId::kKbmRawMouseRequiresController));
  }
  ImGui::Spacing();
  ImGui::AlignTextToFramePadding();
  ImGui::TextUnformatted(Tr(StringId::kKbmDpi));
  ImGui::SameLine();
  ImGui::SetNextItemWidth(-1);
  changed |= ImGui::InputDouble("##MouseDpi", &settings_.raw_mouse_dpi, 0.0,
                                0.0, "%.0f DPI");
  ImGui::TextWrapped("%s", Tr(StringId::kKbmDpiHelp));

  ImGui::Spacing();
  ImGui::TextUnformatted(Tr(StringId::kKbmPhysicalSpeed));
  float speed = float(settings_.raw_mouse_full_stick_speed_cm_s);
  const float precise_width = ImGui::GetFontSize() * 7.5f;
  const float slider_width =
      std::max(ImGui::GetFontSize() * 4, ImGui::GetContentRegionAvail().x -
                                             precise_width -
                                             ImGui::GetStyle().ItemSpacing.x);
  ImGui::SetNextItemWidth(slider_width);
  if (ImGui::SliderFloat(
          "##PhysicalSpeedSlider", &speed, 1.0f, 100.0f, "%.2f cm/s",
          ImGuiSliderFlags_Logarithmic | ImGuiSliderFlags_AlwaysClamp |
              ImGuiSliderFlags_NoRoundToFormat)) {
    settings_.raw_mouse_full_stick_speed_cm_s = speed;
    changed = true;
  }
  ImGui::SameLine();
  ImGui::SetNextItemWidth(precise_width);
  changed |= ImGui::InputDouble("##PhysicalSpeed",
                                &settings_.raw_mouse_full_stick_speed_cm_s, 0.0,
                                0.0, "%.2f cm/s");
  ImGui::TextWrapped("%s", Tr(StringId::kKbmSpeedDirection));

  ImGui::Spacing();
  changed |= ImGui::Checkbox(Tr(StringId::kKbmCompensateGameDeadzone),
                             &settings_.raw_mouse_deadzone_compensation);
  ImGui::BeginDisabled(!settings_.raw_mouse_deadzone_compensation);
  ImGui::TextUnformatted(Tr(StringId::kKbmMinimumResponse));
  float minimum_percent = float(settings_.raw_mouse_minimum_response * 100);
  ImGui::SetNextItemWidth(slider_width);
  if (ImGui::SliderFloat("##MinimumResponse", &minimum_percent, 0, 50, "%.1f%%",
                         ImGuiSliderFlags_AlwaysClamp)) {
    settings_.raw_mouse_minimum_response = minimum_percent / 100.0;
    changed = true;
  }
  double precise_percent = settings_.raw_mouse_minimum_response * 100;
  ImGui::SameLine();
  ImGui::SetNextItemWidth(precise_width);
  if (ImGui::InputDouble("##MinimumResponseInput", &precise_percent, 0.0, 0.0,
                         "%.1f%%")) {
    settings_.raw_mouse_minimum_response = precise_percent / 100;
    changed = true;
  }
  ImGui::EndDisabled();
  changed |= ImGui::Checkbox(Tr(StringId::kKbmInvertVertical),
                             &settings_.raw_mouse_invert_y);
  if (ImGui::TreeNodeEx("KbmTuningHelp", 0, "%s",
                        Tr(StringId::kKbmTuningHelp))) {
    ImGui::TextWrapped("%s", Tr(StringId::kKbmPhysicalGuide));
    ImGui::Spacing();
    ImGui::TextWrapped("%s", Tr(StringId::kKbmMinimumResponseHelp));
    ImGui::TextDisabled("%s", Tr(StringId::kKbmSettingsFile));
    ImGui::TreePop();
  }
  return changed;
}

void KbmConfigDialog::DrawPreview(bool visible) {
  auto* driver = GetDriver();
  if (!visible ||
      !ImGui::TreeNodeEx("KbmLivePreview", ImGuiTreeNodeFlags_SpanAvailWidth,
                         "%s", Tr(StringId::kKbmLivePreview))) {
    if (driver && driver->SampleMousePreview(settings_).active()) {
      driver->StopMousePreview();
    }
    focus_preview_stop_ = false;
    if (driver && driver->MousePreviewFailed()) {
      Error(StringId::kKbmPreviewFailed);
    }
    return;
  }
  ImGui::TextWrapped("%s", Tr(StringId::kKbmPreviewDraft));
  if (!driver) {
    ImGui::TextWrapped("%s", Tr(StringId::kKbmDiagnosticsUnavailable));
    ImGui::TreePop();
    return;
  }
  const auto& preview = driver->SampleMousePreview(settings_);
  const auto label = fmt::format("{:.1f}%", preview.current() * 100);
  ImGui::ProgressBar(float(preview.current()),
                     ImVec2(-1, ImGui::GetFrameHeight()), label.c_str());
  ImGui::AlignTextToFramePadding();
  ImGui::Text(Tr(StringId::kKbmPreviewPeak), preview.peak() * 100);
  NextButton(ButtonWidth(StringId::kKbmResetPreviewPeak));
  if (ImGui::Button(Tr(StringId::kKbmResetPreviewPeak))) {
    driver->ResetMousePreview();
  }
  if (preview.active()) {
    ImGui::TextWrapped("%s", Tr(StringId::kKbmPreviewExitHelp));
    if (focus_preview_stop_) {
      ImGui::SetKeyboardFocusHere();
    }
    if (ImGui::Button(Tr(StringId::kKbmStopPreview), ImVec2(-1, 0))) {
      driver->StopMousePreview();
    }
    if (focus_preview_stop_) {
      ImGui::SetScrollHereY();
      focus_preview_stop_ = false;
    }
  } else if (ImGui::Button(Tr(StringId::kKbmStartPreview), ImVec2(-1, 0))) {
    CancelBindingCapture();
    preview_failed_ = !driver->StartMousePreview();
    focus_preview_stop_ = !preview_failed_;
  }
  if (preview_failed_ || driver->MousePreviewFailed()) {
    Error(StringId::kKbmPreviewFailed);
  }
  ImGui::TreePop();
}

bool KbmConfigDialog::DrawCaptureSettings() {
  ImGui::TextDisabled("%s", Tr(StringId::kKbmCaptureOptions));
  bool changed = ImGui::Checkbox(Tr(StringId::kKbmCaptureOnStart),
                                 &settings_.raw_mouse_capture_on_start);
  ImGui::TextUnformatted(Tr(StringId::kKbmCaptureToggle));
  changed |= DrawBinding("CaptureToggle",
                         settings_.raw_mouse_capture_toggle_key, false);
  return changed;
}

bool KbmConfigDialog::DrawBindings() {
  bool changed = false;
  if (!CollapsingSection("KbmBindingsSection",
                         StringId::kKbmKeyboardBindings)) {
    if (binding_capture_target_ &&
        binding_capture_target_ != &settings_.raw_mouse_capture_toggle_key) {
      CancelBindingCapture();
    }
    return false;
  }
  ImGui::TextWrapped("%s", Tr(StringId::kKbmBindingHelp));
  if (ImGui::BeginTable("KbmBindings", 2, ImGuiTableFlags_SizingStretchProp)) {
    ImGui::TableSetupColumn(Tr(StringId::kKbmXboxInput),
                            ImGuiTableColumnFlags_WidthStretch, 0.42f);
    ImGui::TableSetupColumn(Tr(StringId::kKbmKeyboardMouse),
                            ImGuiTableColumnFlags_WidthStretch, 0.58f);
#define XE_HID_KBM_BINDING(button, description, cvar_name, cvar_default_value) \
  ImGui::TableNextRow();                                                       \
  ImGui::TableSetColumnIndex(0);                                               \
  ImGui::AlignTextToFramePadding();                                            \
  ImGui::TextWrapped("%s", Tr(StringId::kKbmBind##button));                    \
  ImGui::TableSetColumnIndex(1);                                               \
  changed |= DrawBinding(#cvar_name, settings_.cvar_name);
#include "xenia/hid/kbm/kbm_binding_table.inc"
#undef XE_HID_KBM_BINDING
    ImGui::EndTable();
  }
  return changed;
}

bool KbmConfigDialog::DrawDiagnostics() {
  auto* driver = GetDriver();
  const auto sampling = driver ? driver->GetInputSamplingStatus()
                               : hid::kbm::KbmInputSamplingStatus{};
  if (ImGui::IsWindowAppearing() && (sampling.active || sampling.writing)) {
    ImGui::SetNextItemOpen(true);
  }
  if (!CollapsingSection("KbmDiagnosticsSection", StringId::kKbmDiagnostics)) {
    return false;
  }
  if (!driver) {
    ImGui::TextWrapped("%s", Tr(StringId::kKbmDiagnosticsUnavailable));
    return false;
  }
  bool close = false;
  ImGui::TextDisabled("%s", Tr(StringId::kKbmInputSampling));
  if (sampling.active) {
    ImGui::TextWrapped(Tr(StringId::kKbmInputSamplingProgress),
                       sampling.seconds_remaining, sampling.sample_count);
    if (ImGui::Button(Tr(StringId::kKbmStopAndSaveInputSampling))) {
      driver->StopInputSampling();
    }
    NextButton(ButtonWidth(StringId::kKbmCancelInputSampling));
    if (ImGui::Button(Tr(StringId::kKbmCancelInputSampling))) {
      driver->CancelInputSampling();
    }
  } else if (sampling.writing) {
    ImGui::TextWrapped("%s", Tr(StringId::kKbmInputSamplingWriting));
  } else {
    const bool save_first = dirty_ || hid::kbm::GetConfigState() !=
                                          hid::kbm::KbmConfigState::kCompatible;
    ImGui::BeginDisabled(!CanSaveDraft());
    const bool start =
        ImGui::Button(Tr(save_first ? StringId::kKbmSaveAndStartInputSampling
                                    : StringId::kKbmStartInputSampling),
                      ImVec2(-1, 0));
    ImGui::EndDisabled();
    if (start && (!save_first || SaveDraft())) {
      StopCapture();
      sampling_failed_ = !driver->StartInputSampling();
      close = !sampling_failed_;
    }
    ImGui::TextWrapped("%s", Tr(StringId::kKbmInputSamplingHelp));
    if (sampling_failed_) {
      Error(StringId::kKbmInputSamplingStartFailed);
    }
    if (sampling.report_write_failed) {
      Error(StringId::kKbmInputSamplingReportFailed);
    } else if (!sampling.report_path.empty()) {
      ImGui::TextWrapped(Tr(StringId::kKbmInputSamplingReportSaved),
                         sampling.report_path.c_str());
    } else {
      ImGui::TextWrapped("%s", Tr(StringId::kKbmInputSamplingNoReport));
    }
  }
  if (ImGui::TreeNodeEx("KbmTechnicalDetails", 0, "%s",
                        Tr(StringId::kKbmTechnicalDetails))) {
    const auto diagnostics = driver->GetDiagnostics();
    const float output = std::min(1.0f, std::hypot(float(diagnostics.thumb_x),
                                                   float(diagnostics.thumb_y)) /
                                            32767.0f);
    const auto label = fmt::format(
        fmt::runtime(Tr(StringId::kKbmLastStickOutput)), output * 100,
        output >= 0.999f ? Tr(StringId::kKbmMaximum) : "");
    ImGui::TextWrapped("%s", Tr(StringId::kKbmSnapshot));
    ImGui::ProgressBar(output, ImVec2(-1, 0), label.c_str());
    ImGui::TextWrapped(Tr(StringId::kKbmRawSpeed),
                       diagnostics.raw_counts_per_second_x,
                       diagnostics.raw_counts_per_second_y, diagnostics.thumb_x,
                       diagnostics.thumb_y);
    ImGui::TextWrapped(Tr(StringId::kKbmRawInputStatus),
                       Tr(diagnostics.raw_mouse_requested ? StringId::kKbmOn
                                                          : StringId::kKbmOff),
                       Tr(diagnostics.raw_mouse_registered ? StringId::kKbmReady
                                                           : StringId::kKbmOff),
                       Tr(diagnostics.capture_requested ? StringId::kKbmOn
                                                        : StringId::kKbmOff),
                       Tr(diagnostics.capture_active ? StringId::kKbmActive
                                                     : StringId::kKbmReleased));
    ImGui::TreePop();
  }
  return close;
}

bool KbmConfigDialog::DrawConfirmations() {
  bool close = false;
  const auto close_title =
      fmt::format("{}###KbmUnsaved", Tr(StringId::kKbmUnsavedTitle));
  const bool requested = close_requested_;
  if (close_requested_) {
    close_requested_ = false;
    if (dirty_) {
      ImGui::OpenPopup(close_title.c_str());
    } else {
      close = true;
    }
  }
  const float width = std::min(440.0f, ImGui::GetIO().DisplaySize.x - 40);
  ImGui::SetNextWindowSize(ImVec2(width, 0));
  if (ImGui::BeginPopupModal(close_title.c_str(), nullptr,
                             ImGuiWindowFlags_AlwaysAutoResize)) {
    ImGui::TextWrapped("%s", Tr(StringId::kKbmUnsavedHelp));
    if (save_failed_) {
      Error(StringId::kKbmAppliedSaveFailed);
    }
    ImGui::BeginDisabled(!CanSaveDraft());
    const bool save = ImGui::Button(Tr(StringId::kKbmSaveClose), ImVec2(-1, 0));
    ImGui::EndDisabled();
    if (save && SaveDraft()) {
      close = true;
      ImGui::CloseCurrentPopup();
    }
    if (ImGui::Button(Tr(StringId::kKbmDiscardClose), ImVec2(-1, 0))) {
      close = true;
      ImGui::CloseCurrentPopup();
    }
    if (ImGui::Button(Tr(StringId::kKbmContinueEditing), ImVec2(-1, 0)) ||
        (!requested && ImGui::IsKeyPressed(ImGuiKey_Escape, false))) {
      ImGui::CloseCurrentPopup();
    }
    ImGui::EndPopup();
  }
  const auto reset_title =
      fmt::format("{}###KbmReset", Tr(StringId::kKbmResetTitle));
  ImGui::SetNextWindowSize(ImVec2(width, 0));
  if (ImGui::BeginPopupModal(reset_title.c_str(), nullptr,
                             ImGuiWindowFlags_AlwaysAutoResize)) {
    ImGui::TextWrapped("%s", Tr(StringId::kKbmResetHelp));
    if (ImGui::Button(Tr(StringId::kKbmResetConfirm), ImVec2(-1, 0))) {
      StopCapture();
      settings_ = hid::kbm::KbmSettings();
      reset_incompatible_ = true;
      ApplyDraft();
      ImGui::CloseCurrentPopup();
    }
    if (ImGui::Button(Tr(StringId::kKbmContinueEditing), ImVec2(-1, 0)) ||
        ImGui::IsKeyPressed(ImGuiKey_Escape, false)) {
      ImGui::CloseCurrentPopup();
    }
    ImGui::EndPopup();
  }
  return close;
}

void KbmConfigDialog::OnDraw(ImGuiIO& io) {
  const float minimum_width = std::min(520.0f, io.DisplaySize.x);
  const float minimum_height = std::min(480.0f, io.DisplaySize.y);
  ImGui::SetNextWindowSizeConstraints(ImVec2(minimum_width, minimum_height),
                                      ImVec2(FLT_MAX, FLT_MAX));
  ImGui::SetNextWindowPos(ImVec2(20, 20), ImGuiCond_FirstUseEver);
  ImGui::SetNextWindowSize(
      ImVec2(minimum_width,
             std::max(minimum_height, std::min(680.0f, io.DisplaySize.y - 40))),
      ImGuiCond_FirstUseEver);
  ImGui::SetNextWindowBgAlpha(0.90f);
  bool open = true;
  const auto title = fmt::format("{}###KbmSettings", Tr(StringId::kKbmTitle));
  bool close = false;
  if (ImGui::Begin(title.c_str(), &open,
                   ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoScrollbar |
                       ImGuiWindowFlags_NoScrollWithMouse)) {
    // The driver handles capture Escape before this frame is drawn. Remember
    // the previous preview and binding state so that key cannot also close UI.
    const bool was_binding = binding_capture_target_ != nullptr;
    const bool had_popup =
        ImGui::IsPopupOpen(nullptr, ImGuiPopupFlags_AnyPopupId);
    const bool was_editing = ImGui::IsAnyItemActive();
    auto* driver = GetDriver();
    const bool preview_active =
        driver && driver->SampleMousePreview(settings_).active();
    const bool escape = ImGui::IsKeyPressed(ImGuiKey_Escape, false);
    if (preview_active && (ImGui::IsKeyPressed(ImGuiKey_Enter, false) ||
                           ImGui::IsKeyPressed(ImGuiKey_Space, false))) {
      driver->StopMousePreview();
    }
    if (escape && !was_binding && !preview_was_active_ && !preview_active &&
        !had_popup && !was_editing &&
        ImGui::IsWindowFocused(ImGuiFocusedFlags_RootAndChildWindows)) {
      RequestClose();
    }
    if (escape && was_binding) {
      CancelBindingCapture();
    }
    bool header_changed =
        ImGui::Checkbox(Tr(StringId::kKbmEnableController), &settings_.enabled);
    const char* players[] = {
        Tr(StringId::kKbmPlayer1), Tr(StringId::kKbmPlayer2),
        Tr(StringId::kKbmPlayer3), Tr(StringId::kKbmPlayer4)};
    if (ImGui::GetContentRegionAvail().x > ImGui::GetFontSize() * 42) {
      ImGui::SameLine();
    }
    ImGui::SetNextItemWidth(ImGui::GetFontSize() * 10);
    header_changed |= ImGui::Combo(Tr(StringId::kKbmControllerSlot),
                                   &settings_.user_index, players, 4);
    if (header_changed) {
      ApplyDraft();
    }
    ImGui::TextWrapped("%s", Tr(StringId::kKbmInputPaused));
    ImGui::Separator();

    const float available_width = ImGui::GetContentRegionAvail().x;
    const auto status_id = [&] {
      return save_failed_ ? StringId::kKbmAppliedSaveFailed
             : dirty_     ? StringId::kKbmAppliedUnsaved
             : saved_     ? StringId::kKbmAppliedSaved
                          : StringId::kKbmSharedDraft;
    };
    const float reset_width = ButtonWidth(StringId::kKbmRestoreDefaults);
    const float status_width =
        std::max(1.0f, available_width - reset_width -
                           ImGui::GetStyle().CellPadding.x * 4);
    // Reserve the measured footer, including translated wrapping and button
    // rows, so a narrow window still scrolls only the settings body.
    float status_height = 0;
    for (StringId id :
         {StringId::kKbmAppliedSaveFailed, StringId::kKbmAppliedUnsaved,
          StringId::kKbmAppliedSaved, StringId::kKbmSharedDraft}) {
      status_height =
          std::max(status_height,
                   ImGui::CalcTextSize(Tr(id), nullptr, false, status_width).y +
                       ImGui::GetStyle().FramePadding.y);
    }
    int button_rows = 1;
    float row_width = 0;
    for (StringId id :
         {StringId::kKbmCancel, StringId::kKbmSave, StringId::kKbmSaveClose}) {
      const float width = ButtonWidth(id);
      if (row_width && row_width + width > available_width) {
        ++button_rows;
        row_width = 0;
      }
      row_width += width + ImGui::GetStyle().ItemSpacing.x;
    }
    const float footer_height =
        std::max(status_height, ImGui::GetFrameHeight()) +
        ImGui::GetFrameHeightWithSpacing() * button_rows +
        ImGui::GetStyle().ItemSpacing.y * 3 +
        ImGui::GetStyle().CellPadding.y * 2 + 1;
    const bool body =
        ImGui::BeginChild("KbmSettingsBody", ImVec2(0, -footer_height));
    if (body) {
      if (!driver) {
        ImGui::TextWrapped("%s", Tr(StringId::kKbmBackendInactive));
      }
      if (hid::kbm::GetConfigState() ==
          hid::kbm::KbmConfigState::kIncompatible) {
        Error(StringId::kKbmConfigIncompatible);
      }
      const bool two_columns =
          ImGui::GetContentRegionAvail().x >= ImGui::GetFontSize() * 50;
      if (ImGui::BeginTable("KbmMainLayout", two_columns ? 2 : 1,
                            ImGuiTableFlags_SizingStretchSame)) {
        auto mouse = [&] {
          if (BeginCard("Mouse")) {
            DrawMouseSection();
          }
          ImGui::EndChild();
        };
        auto bindings = [&] {
          if (BeginCard("Bindings")) {
            if (DrawBindings()) {
              ApplyDraft();
            }
          }
          ImGui::EndChild();
        };
        ImGui::TableNextRow();
        ImGui::TableSetColumnIndex(0);
        bindings();
        ImGui::TableNextColumn();
        mouse();
        ImGui::EndTable();
      }
      ImGui::Spacing();
      if (BeginCard("Diagnostics")) {
        close = DrawDiagnostics();
      }
      ImGui::EndChild();
    }
    ImGui::EndChild();

    ImGui::Separator();
    bool reset_requested = false;
    if (ImGui::BeginTable("KbmFooterStatus", 2,
                          ImGuiTableFlags_SizingStretchProp)) {
      ImGui::TableSetupColumn("Status", ImGuiTableColumnFlags_WidthStretch);
      ImGui::TableSetupColumn("Defaults", ImGuiTableColumnFlags_WidthFixed,
                              reset_width);
      ImGui::TableNextRow();
      ImGui::TableSetColumnIndex(0);
      ImGui::AlignTextToFramePadding();
      if (save_failed_) {
        Error(StringId::kKbmAppliedSaveFailed);
      } else {
        ImGui::TextWrapped("%s", Tr(status_id()));
      }
      ImGui::TableSetColumnIndex(1);
      const bool reset = ImGui::Button(Tr(StringId::kKbmRestoreDefaults));
      if (reset) {
        StopCapture();
        // Open the popup outside the table's ID scope below.
        reset_requested = true;
      }
      ImGui::EndTable();
    }
    if (reset_requested) {
      const auto reset_title =
          fmt::format("{}###KbmReset", Tr(StringId::kKbmResetTitle));
      ImGui::OpenPopup(reset_title.c_str());
    }
    if (ImGui::Button(Tr(StringId::kKbmCancel))) {
      StopCapture();
      close = true;
    }
    NextButton(ButtonWidth(StringId::kKbmSave));
    ImGui::BeginDisabled(!CanSaveDraft());
    const bool save = ImGui::Button(Tr(StringId::kKbmSave));
    NextButton(ButtonWidth(StringId::kKbmSaveClose));
    const bool save_close = ImGui::Button(Tr(StringId::kKbmSaveClose));
    ImGui::EndDisabled();
    if ((save || save_close) && SaveDraft() && save_close) {
      close = true;
    }
    if (!open) {
      RequestClose();
    }
    // UI clicks may also have completed a mouse binding. Let collapse,
    // cancel, save and close discard it before consuming a pending result;
    // wait through mouse-down until the active widget handles its release.
    if (!ImGui::IsAnyItemActive() && HandleBindingCaptureResult()) {
      ApplyDraft();
    }
    close |= DrawConfirmations();
    preview_was_active_ =
        driver && driver->SampleMousePreview(settings_).active();
  } else if (!open) {
    RequestClose();
  }
  ImGui::End();
  if (close) {
    StopCapture();
    RestoreOriginal();
    Close();
    emulator_window_.ToggleKbmConfigDialog();
  }
}
}  // namespace xe::app
