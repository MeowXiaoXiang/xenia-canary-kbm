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
#include <cmath>

#include "third_party/fmt/include/fmt/format.h"
#include "third_party/imgui/imgui.h"
#include "xenia/app/emulator_window.h"
#include "xenia/app/localization.h"
#include "xenia/hid/input_system.h"
#include "xenia/hid/kbm/kbm_input_driver.h"

namespace xe::app {

KbmConfigDialog::KbmConfigDialog(ui::ImGuiDrawer* imgui_drawer,
                                 EmulatorWindow& emulator_window)
    : ui::ImGuiDialog(imgui_drawer), emulator_window_(emulator_window) {
  settings_ = hid::kbm::GetSettingsFromCvars();
  original_settings_ = settings_;
  saved_ = hid::kbm::GetConfigState() == hid::kbm::KbmConfigState::kCompatible;
}

KbmConfigDialog::~KbmConfigDialog() {
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
  if (auto* driver = GetDriver()) {
    driver->ApplySettings(settings_);
  } else {
    hid::kbm::ApplySettingsToCvars(settings_);
  }
}

void KbmConfigDialog::RestoreOriginal() {
  settings_ = original_settings_;
  if (auto* driver = GetDriver()) {
    driver->ApplySettings(settings_);
  } else {
    hid::kbm::ApplySettingsToCvars(settings_);
  }
  committed_or_restored_ = true;
}

void KbmConfigDialog::StartBindingCapture(std::string* target, bool append) {
  binding_capture_target_ = target;
  binding_capture_append_ = append;
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
      if (binding_capture_append_ && !binding_capture_target_->empty()) {
        *binding_capture_target_ += " " + result.value;
      } else {
        *binding_capture_target_ = result.value;
      }
      binding_capture_target_ = nullptr;
      binding_capture_append_ = false;
      return true;
    case Status::kCleared:
      binding_capture_target_->clear();
      binding_capture_target_ = nullptr;
      binding_capture_append_ = false;
      return true;
    case Status::kCancelled:
      binding_capture_target_ = nullptr;
      binding_capture_append_ = false;
      return false;
    default:
      return false;
  }
}

void KbmConfigDialog::OnDraw(ImGuiIO& io) {
  using localization::StringId;
  const auto tr = [](StringId id) { return localization::Get(id); };

  ImGui::SetNextWindowPos(ImVec2(20, 20), ImGuiCond_FirstUseEver);
  ImGui::SetNextWindowSize(ImVec2(560, 660), ImGuiCond_FirstUseEver);
  ImGui::SetNextWindowBgAlpha(0.94f);

  bool dialog_open = true;
  const std::string title =
      fmt::format("{}###KbmSettings", tr(StringId::kKbmTitle));
  if (!ImGui::Begin(title.c_str(), &dialog_open, ImGuiWindowFlags_NoCollapse)) {
    ImGui::End();
    return;
  }

  if (!GetDriver()) {
    ImGui::TextWrapped("%s", tr(StringId::kKbmBackendInactive));
    ImGui::Spacing();
  }
  if (hid::kbm::GetConfigState() == hid::kbm::KbmConfigState::kIncompatible) {
    ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(1.0f, 0.75f, 0.2f, 1.0f));
    ImGui::TextWrapped("%s", tr(StringId::kKbmConfigIncompatible));
    ImGui::PopStyleColor();
    ImGui::Spacing();
  }

  bool changed = HandleBindingCaptureResult();
  bool close_after_start_sampling = false;
  ImGui::TextWrapped("%s", tr(StringId::kKbmInputPaused));
  ImGui::Text("%s %s", tr(StringId::kKbmCaptureToggle),
              hid::kbm::FormatKbmBinding(settings_.raw_mouse_capture_toggle_key)
                  .c_str());
  ImGui::BeginChild("KbmSettingsBody",
                    ImVec2(0, -ImGui::GetFrameHeightWithSpacing() * 3), false);
  auto draw_binding = [&](const char* id, std::string& binding,
                          bool allow_alternatives = true) {
    ImGui::PushID(id);
    const bool capturing = binding_capture_target_ == &binding;
    const std::string label = capturing ? tr(StringId::kKbmPressBinding)
                              : binding.empty()
                                  ? tr(StringId::kKbmUnbound)
                                  : hid::kbm::FormatKbmBinding(binding);
    const std::string binding_button_id = label + "##BindingValue";
    const float action_width = ImGui::CalcTextSize("Esc").x +
                               ImGui::GetStyle().FramePadding.x * 2 +
                               (allow_alternatives ? 64.0f : 40.0f);
    ImGui::BeginDisabled(GetDriver() == nullptr);
    if (ImGui::Button(binding_button_id.c_str(),
                      ImVec2(std::max(80.0f, ImGui::GetContentRegionAvail().x -
                                                 action_width),
                             0.0f))) {
      StartBindingCapture(&binding, false);
    }
    if (ImGui::IsItemHovered()) {
      ImGui::SetTooltip("%s", tr(StringId::kKbmBindingTooltip));
    }
    if (allow_alternatives) {
      ImGui::SameLine();
      if (ImGui::SmallButton("+##AddAlternativeBinding")) {
        StartBindingCapture(&binding, true);
      }
      if (ImGui::IsItemHovered()) {
        ImGui::SetTooltip("%s", tr(StringId::kKbmAddAlternativeBinding));
      }
    }
    ImGui::EndDisabled();
    ImGui::SameLine();
    if (ImGui::SmallButton("Esc##BindEscape")) {
      CancelBindingCapture();
      binding = "Key.Escape";
      changed = true;
    }
    if (ImGui::IsItemHovered()) {
      ImGui::SetTooltip("%s", tr(StringId::kKbmBindEscape));
    }
    ImGui::SameLine();
    if (ImGui::SmallButton("x##ClearBinding")) {
      if (capturing) {
        CancelBindingCapture();
      }
      binding.clear();
      changed = true;
    }
    if (ImGui::IsItemHovered()) {
      ImGui::SetTooltip("%s", tr(StringId::kKbmClearBinding));
    }
    ImGui::PopID();
  };
  if (ImGui::TreeNodeEx(
          tr(StringId::kKbmKeyboard),
          ImGuiTreeNodeFlags_Framed | ImGuiTreeNodeFlags_DefaultOpen)) {
    if (ImGui::Checkbox(tr(StringId::kKbmEnableController),
                        &settings_.enabled)) {
      changed = true;
    }

    if (settings_.enabled) {
      int controller_slot = std::clamp(settings_.user_index, 0, 3);
      const char* controller_slot_names[] = {
          tr(StringId::kKbmPlayer1), tr(StringId::kKbmPlayer2),
          tr(StringId::kKbmPlayer3), tr(StringId::kKbmPlayer4)};
      if (ImGui::BeginCombo(tr(StringId::kKbmControllerSlot),
                            controller_slot_names[controller_slot])) {
        for (int i = 0; i < 4; ++i) {
          if (ImGui::Selectable(controller_slot_names[i],
                                controller_slot == i)) {
            settings_.user_index = i;
            changed = true;
          }
        }
        ImGui::EndCombo();
      }

      ImGui::TextWrapped("%s", tr(StringId::kKbmBindingHelp));
      if (ImGui::BeginTable("KbmBindings", 2,
                            ImGuiTableFlags_BordersInnerV |
                                ImGuiTableFlags_RowBg |
                                ImGuiTableFlags_SizingStretchProp)) {
        ImGui::TableSetupColumn(tr(StringId::kKbmXboxInput),
                                ImGuiTableColumnFlags_WidthFixed, 165.0f);
        ImGui::TableSetupColumn(tr(StringId::kKbmKeyboardMouse));
        ImGui::TableHeadersRow();
#define XE_HID_KBM_BINDING(button, description, cvar_name, cvar_default_value) \
  ImGui::TableNextRow();                                                       \
  ImGui::TableSetColumnIndex(0);                                               \
  ImGui::TextUnformatted(tr(StringId::kKbmBind##button));                      \
  ImGui::TableSetColumnIndex(1);                                               \
  draw_binding(#cvar_name, settings_.cvar_name);
#include "xenia/hid/kbm/kbm_binding_table.inc"
#undef XE_HID_KBM_BINDING
        ImGui::EndTable();
      }
    }
    ImGui::TreePop();
  }

  if (ImGui::TreeNodeEx(
          tr(StringId::kKbmMouse),
          ImGuiTreeNodeFlags_Framed | ImGuiTreeNodeFlags_DefaultOpen)) {
    const bool mouse_controls_available = settings_.enabled;
    ImGui::BeginDisabled(!mouse_controls_available);
    if (ImGui::Checkbox(tr(StringId::kKbmEnableRawMouse),
                        &settings_.raw_mouse)) {
      changed = true;
    }
    ImGui::EndDisabled();
    if (!mouse_controls_available) {
      ImGui::TextWrapped("%s", tr(StringId::kKbmRawMouseRequiresController));
    }

    ImGui::BeginDisabled(!mouse_controls_available);
    ImGui::Spacing();
    ImGui::TextUnformatted(tr(StringId::kKbmSensitivity));
    float sensitivity = float(settings_.raw_mouse_sensitivity);
    ImGui::SetNextItemWidth(
        std::max(80.0f, ImGui::GetContentRegionAvail().x - 180.0f));
    if (ImGui::SliderFloat(
            "##MouseSensitivitySlider", &sensitivity, 0.01f, 256.0f, "%.3f x",
            ImGuiSliderFlags_Logarithmic | ImGuiSliderFlags_AlwaysClamp)) {
      settings_.raw_mouse_sensitivity = sensitivity;
      changed = true;
    }
    ImGui::SameLine();
    ImGui::SetNextItemWidth(90.0f);
    if (ImGui::InputDouble("##MouseSensitivityInput",
                           &settings_.raw_mouse_sensitivity, 0.0, 0.0, "%.3f",
                           ImGuiInputTextFlags_CharsDecimal)) {
      settings_.raw_mouse_sensitivity =
          std::clamp(settings_.raw_mouse_sensitivity, 0.01, 256.0);
      changed = true;
    }
    ImGui::SameLine();
    const std::string reset_sensitivity =
        fmt::format("{}##MouseSensitivity", tr(StringId::kPostReset));
    if (ImGui::Button(reset_sensitivity.c_str())) {
      settings_.raw_mouse_sensitivity = 10.0;
      changed = true;
    }
    ImGui::TextWrapped("%s", tr(StringId::kKbmSensitivityGuide));

    if (ImGui::Checkbox(tr(StringId::kKbmCompensateGameDeadzone),
                        &settings_.raw_mouse_deadzone_compensation)) {
      changed = true;
    }
    if (settings_.raw_mouse_deadzone_compensation) {
      ImGui::TextUnformatted(tr(StringId::kKbmMinimumResponse));
      float minimum_response = float(settings_.raw_mouse_minimum_response);
      ImGui::SetNextItemWidth(
          std::max(80.0f, ImGui::GetContentRegionAvail().x - 180.0f));
      if (ImGui::SliderFloat("##MinimumResponse", &minimum_response, 0.0f, 0.5f,
                             "%.3f", ImGuiSliderFlags_AlwaysClamp)) {
        settings_.raw_mouse_minimum_response = minimum_response;
        changed = true;
      }
      ImGui::SameLine();
      ImGui::SetNextItemWidth(90.0f);
      if (ImGui::InputDouble("##MinimumResponseInput",
                             &settings_.raw_mouse_minimum_response, 0.0, 0.0,
                             "%.3f", ImGuiInputTextFlags_CharsDecimal)) {
        settings_.raw_mouse_minimum_response =
            std::clamp(settings_.raw_mouse_minimum_response, 0.0, 0.5);
        changed = true;
      }
    }
    ImGui::TextWrapped("%s", tr(StringId::kKbmMinimumResponseHelp));

    if (ImGui::Checkbox(tr(StringId::kKbmInvertVertical),
                        &settings_.raw_mouse_invert_y)) {
      changed = true;
    }
    if (ImGui::Checkbox(tr(StringId::kKbmCaptureOnStart),
                        &settings_.raw_mouse_capture_on_start)) {
      changed = true;
    }
    ImGui::TextUnformatted(tr(StringId::kKbmCaptureToggle));
    draw_binding("CaptureToggle", settings_.raw_mouse_capture_toggle_key,
                 false);

    if (ImGui::TreeNodeEx(tr(StringId::kKbmFineTuning),
                          ImGuiTreeNodeFlags_Framed)) {
      ImGui::TextUnformatted(tr(StringId::kKbmAimCurve));
      float curve = float(settings_.raw_mouse_response_curve);
      ImGui::SetNextItemWidth(
          std::max(80.0f, ImGui::GetContentRegionAvail().x - 180.0f));
      if (ImGui::SliderFloat(
              "##MouseCurve", &curve, 0.1f, 4.0f, "%.3f",
              ImGuiSliderFlags_Logarithmic | ImGuiSliderFlags_AlwaysClamp)) {
        settings_.raw_mouse_response_curve = curve;
        changed = true;
      }
      ImGui::SameLine();
      ImGui::SetNextItemWidth(90.0f);
      if (ImGui::InputDouble("##MouseCurveInput",
                             &settings_.raw_mouse_response_curve, 0.0, 0.0,
                             "%.3f", ImGuiInputTextFlags_CharsDecimal)) {
        settings_.raw_mouse_response_curve =
            std::clamp(settings_.raw_mouse_response_curve, 0.1, 4.0);
        changed = true;
      }
      ImGui::TextWrapped("%s", tr(StringId::kKbmAimCurveHelp));

      ImGui::TextUnformatted(tr(StringId::kKbmMouseSmoothing));
      float smoothing_time_ms = float(settings_.raw_mouse_smoothing_time_ms);
      ImGui::SetNextItemWidth(
          std::max(80.0f, ImGui::GetContentRegionAvail().x - 180.0f));
      if (ImGui::SliderFloat("##MouseSmoothing", &smoothing_time_ms, 0.0f,
                             20.0f, "%.1f ms", ImGuiSliderFlags_AlwaysClamp)) {
        settings_.raw_mouse_smoothing_time_ms = smoothing_time_ms;
        changed = true;
      }
      ImGui::TextWrapped("%s", tr(StringId::kKbmMouseSmoothingHelp));

      ImGui::TextUnformatted(tr(StringId::kKbmFullStickThreshold));
      ImGui::SetNextItemWidth(220.0f);
      if (ImGui::InputDouble(
              "##FullScaleVelocity", &settings_.raw_mouse_full_scale_velocity,
              100.0, 1000.0, "%.0f", ImGuiInputTextFlags_CharsDecimal)) {
        settings_.raw_mouse_full_scale_velocity =
            std::clamp(settings_.raw_mouse_full_scale_velocity, 1.0, 1000000.0);
        changed = true;
      }
      ImGui::TextWrapped(tr(StringId::kKbmFullStickHelp),
                         settings_.raw_mouse_sensitivity,
                         settings_.raw_mouse_full_scale_velocity /
                             std::max(settings_.raw_mouse_sensitivity, 0.01));
      ImGui::TreePop();
    }
    ImGui::EndDisabled();

    if (ImGui::TreeNodeEx(tr(StringId::kKbmDiagnostics),
                          ImGuiTreeNodeFlags_Framed)) {
      if (auto* driver = GetDriver()) {
        const auto diagnostics = driver->GetDiagnostics();
        const float stick_output =
            std::max(std::abs(float(diagnostics.thumb_x)) / 32767.0f,
                     std::abs(float(diagnostics.thumb_y)) / 32767.0f);
        const std::string stick_label = fmt::format(
            fmt::runtime(tr(StringId::kKbmLastStickOutput)),
            stick_output * 100.0f,
            stick_output >= 0.999f ? tr(StringId::kKbmMaximum) : "");
        ImGui::ProgressBar(stick_output, ImVec2(-1.0f, 0.0f),
                           stick_label.c_str());
        ImGui::TextWrapped(tr(StringId::kKbmRawSpeed),
                           diagnostics.raw_counts_per_second_x,
                           diagnostics.raw_counts_per_second_y,
                           diagnostics.thumb_x, diagnostics.thumb_y);
        ImGui::Text(tr(StringId::kKbmRawInputStatus),
                    diagnostics.raw_mouse_requested ? tr(StringId::kKbmOn)
                                                    : tr(StringId::kKbmOff),
                    diagnostics.raw_mouse_registered ? tr(StringId::kKbmReady)
                                                     : tr(StringId::kKbmOff),
                    diagnostics.capture_requested ? tr(StringId::kKbmOn)
                                                  : tr(StringId::kKbmOff),
                    diagnostics.capture_active ? tr(StringId::kKbmActive)
                                               : tr(StringId::kKbmReleased));
        ImGui::TextWrapped("%s", tr(StringId::kKbmSnapshot));

        ImGui::Separator();
        ImGui::TextUnformatted(tr(StringId::kKbmInputSampling));
        const auto sampling = driver->GetInputSamplingStatus();
        if (sampling.active) {
          ImGui::Text(tr(StringId::kKbmInputSamplingProgress),
                      sampling.seconds_remaining, sampling.sample_count);
          if (ImGui::Button(tr(StringId::kKbmStopAndSaveInputSampling))) {
            driver->StopInputSampling();
          }
          ImGui::SameLine();
          if (ImGui::Button(tr(StringId::kKbmCancelInputSampling))) {
            driver->CancelInputSampling();
          }
        } else if (sampling.writing) {
          ImGui::TextWrapped("%s", tr(StringId::kKbmInputSamplingWriting));
        } else {
          const bool save_and_start =
              dirty_ || hid::kbm::GetConfigState() ==
                            hid::kbm::KbmConfigState::kIncompatible;
          if (ImGui::Button(tr(save_and_start
                                   ? StringId::kKbmSaveAndStartInputSampling
                                   : StringId::kKbmStartInputSampling))) {
            if (save_and_start) {
              CancelBindingCapture();
              ApplyDraft();
              if (hid::kbm::SaveConfig()) {
                original_settings_ = settings_;
                committed_or_restored_ = true;
                save_failed_ = false;
                dirty_ = false;
                saved_ = true;
              } else {
                save_failed_ = true;
              }
            }
            if (!save_failed_ && driver->StartInputSampling()) {
              committed_or_restored_ = true;
              close_after_start_sampling = true;
            }
          }
          ImGui::TextWrapped("%s", tr(StringId::kKbmInputSamplingHelp));
          if (!sampling.report_path.empty()) {
            ImGui::TextWrapped(tr(StringId::kKbmInputSamplingReportSaved),
                               sampling.report_path.c_str());
          } else if (sampling.report_write_failed) {
            ImGui::TextDisabled("%s",
                                tr(StringId::kKbmInputSamplingReportFailed));
          } else {
            ImGui::TextDisabled("%s", tr(StringId::kKbmInputSamplingNoReport));
          }
        }
      } else {
        ImGui::TextDisabled("%s", tr(StringId::kKbmDiagnosticsUnavailable));
      }
      ImGui::TreePop();
    }
    ImGui::TreePop();
  }

  ImGui::EndChild();
  if (changed) {
    ApplyDraft();
  }

  if (close_after_start_sampling) {
    Close();
    ImGui::End();
    emulator_window_.ToggleKbmConfigDialog();
    return;
  }

  ImGui::Separator();
  if (save_failed_) {
    ImGui::TextColored(ImVec4(1.0f, 0.35f, 0.35f, 1.0f), "%s",
                       tr(StringId::kKbmAppliedSaveFailed));
  } else if (dirty_) {
    ImGui::TextUnformatted(tr(StringId::kKbmAppliedUnsaved));
  } else if (saved_) {
    ImGui::TextUnformatted(tr(StringId::kKbmAppliedSaved));
  }
  const bool save = ImGui::Button(tr(StringId::kKbmSave));
  ImGui::SameLine();
  const bool save_and_close = ImGui::Button(tr(StringId::kKbmSaveClose));
  if (save || save_and_close) {
    CancelBindingCapture();
    ApplyDraft();
    if (hid::kbm::SaveConfig()) {
      original_settings_ = settings_;
      committed_or_restored_ = true;
      save_failed_ = false;
      dirty_ = false;
      saved_ = true;
      if (save_and_close) {
        Close();
        ImGui::End();
        emulator_window_.ToggleKbmConfigDialog();
        return;
      }
    } else {
      save_failed_ = true;
    }
  }
  ImGui::SameLine();
  if (ImGui::Button(tr(StringId::kKbmRestoreDefaults))) {
    CancelBindingCapture();
    settings_ = hid::kbm::KbmSettings();
    ApplyDraft();
  }
  ImGui::TextDisabled("%s", tr(StringId::kKbmSettingsFile));

  ImGui::End();
  if (!dialog_open) {
    CancelBindingCapture();
    RestoreOriginal();
    Close();
    emulator_window_.ToggleKbmConfigDialog();
  }
}
}  // namespace xe::app
