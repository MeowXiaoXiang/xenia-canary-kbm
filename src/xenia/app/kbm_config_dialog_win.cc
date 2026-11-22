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
#include <array>
#include <cfloat>
#include <chrono>
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
using hid::kbm::KbmInputDriver;
using hid::kbm::KbmMouseMode;
using localization::StringId;
const char* Tr(StringId id) { return localization::Get(id); }

constexpr StringId kMouseModes[] = {StringId::kKbmModeEma,
                                    StringId::kKbmModeFixedWindow,
                                    StringId::kKbmModeDirect};

KbmInputDriver* FindKbmDriver(EmulatorWindow& window) {
  hid::InputSystem* input_system = window.emulator()->input_system();
  return input_system ? input_system->GetDriver<KbmInputDriver>() : nullptr;
}

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

void Tooltip(const char* text, const char* heading = nullptr) {
  if (ImGui::BeginTooltip()) {
    const float width = std::max(
        1.0f,
        std::min(ImGui::GetFontSize() * 26, ImGui::GetIO().DisplaySize.x - 32));
    ImGui::PushTextWrapPos(ImGui::GetCursorPosX() + width);
    if (heading) {
      ImGui::TextUnformatted(heading);
    }
    ImGui::TextUnformatted(text);
    ImGui::PopTextWrapPos();
    ImGui::EndTooltip();
  }
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

bool DrawMinimumOutput(const char* id, StringId label, double& minimum,
                       float slider_width, float input_width) {
  ImGui::PushID(id);
  ImGui::TextUnformatted(Tr(label));
  float percent = float(minimum * 100);
  ImGui::SetNextItemWidth(slider_width);
  bool changed = ImGui::SliderFloat("##Slider", &percent, 0, 50, "%.1f%%",
                                    ImGuiSliderFlags_AlwaysClamp);
  if (changed) {
    minimum = percent / 100.0;
  }
  double precise = minimum * 100;
  ImGui::SameLine();
  ImGui::SetNextItemWidth(input_width);
  if (ImGui::InputDouble("##Input", &precise, 0.0, 0.0, "%.1f%%")) {
    minimum = precise / 100;
    changed = true;
  }
  ImGui::PopID();
  return changed;
}

bool BeginPollHud(int corner, float content_width, float opacity = 0.58f) {
  const bool right = corner % 2 != 0;
  const bool bottom = corner >= 2;
  constexpr float margin = 8;
  const auto* viewport = ImGui::GetMainViewport();
  ImGui::SetNextWindowPos(
      ImVec2(viewport->WorkPos.x +
                 (right ? viewport->WorkSize.x - margin : margin),
             viewport->WorkPos.y +
                 (bottom ? viewport->WorkSize.y - margin : margin)),
      ImGuiCond_Always, ImVec2(right ? 1 : 0, bottom ? 1 : 0));
  const ImVec2 padding(6, 4);
  const float width =
      std::clamp(content_width + padding.x * 2 + 1, 1.0f,
                 std::max(1.0f, viewport->WorkSize.x - margin * 2));
  ImGui::SetNextWindowSize(ImVec2(width, 0));
  ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, padding);
  ImGui::PushStyleVar(ImGuiStyleVar_PopupBorderSize, 0);
  ImGui::PushStyleColor(ImGuiCol_PopupBg, ImVec4(0, 0, 0, opacity));
  // Draw above the settings window without capturing input.
  const bool visible = ImGui::Begin(
      "###KbmPollHud", nullptr,
      ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_AlwaysAutoResize |
          ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoInputs |
          ImGuiWindowFlags_NoFocusOnAppearing | ImGuiWindowFlags_NoDocking |
          ImGuiWindowFlags_Tooltip);
  ImGui::PopStyleColor();
  ImGui::PopStyleVar(2);
  return visible;
}

void DrawPollHudHeading(const std::string& mode, const std::string& detail) {
  const float row_start = ImGui::GetCursorPosX();
  const float row_width = ImGui::GetContentRegionAvail().x;
  ImGui::TextWrapped("%s", mode.c_str());
  if (detail.empty()) {
    return;
  }
  const float detail_width = ImGui::CalcTextSize(detail.c_str()).x;
  const float detail_x = row_start + row_width - detail_width;
  if (ImGui::CalcTextSize(mode.c_str()).x + 12 + detail_width <= row_width) {
    ImGui::SameLine(detail_x);
  } else if (detail_width <= row_width) {
    ImGui::SetCursorPosX(detail_x);
  }
  if (detail_width <= row_width) {
    ImGui::TextUnformatted(detail.c_str());
  } else {
    ImGui::TextWrapped("%s", detail.c_str());
  }
}

struct EmaHud {
  using Clock = KbmInputDriver::EmaHudHistory::Clock;
  static constexpr float kMinimumScaleCmS = 2.0f;

  const KbmInputDriver::EmaHudSample* LatestSample() const {
    if (samples.empty() ||
        sampled_at - samples.front().time > std::chrono::milliseconds(500)) {
      return nullptr;
    }
    return &samples.front();
  }

  void Refresh(KbmInputDriver* driver) {
    sampled_at = Clock::now();
    if (driver) {
      driver->CopyEmaHudSamples(sampled_at, samples);
    } else {
      samples.clear();
    }
    mode_line = fmt::format("{} {:.1f} ms", Tr(StringId::kKbmModeEma), tau_ms);
    scale_max = kMinimumScaleCmS;
    for (const auto& sample : samples) {
      scale_max = std::max(
          {scale_max, sample.raw_speed_cm_s, sample.smoothed_speed_cm_s});
    }
    scale_max = std::ceil(scale_max);
    scale_label = fmt::format("{} {:.0f} cm/s",
                              Tr(StringId::kKbmPollHudEmaScaleMax), scale_max);
    const auto* latest = LatestSample();
    weight = fmt::format(
        "{} {}", Tr(StringId::kKbmPollHudEmaWeight),
        latest ? fmt::format("{:.1f}%", latest->alpha * 100) : "--");
    raw_value = latest ? fmt::format("{:.2f}", latest->raw_speed_cm_s) : "--";
    smoothed_value =
        latest ? fmt::format("{:.2f}", latest->smoothed_speed_cm_s) : "--";
    const char* unit = latest ? " cm/s" : "";
    raw_label = fmt::format("{} {}{}", Tr(StringId::kKbmPollHudEmaRawSpeed),
                            raw_value, unit);
    smoothed_label = fmt::format("{} {}{}", Tr(StringId::kKbmPollHudEmaSpeed),
                                 smoothed_value, unit);
  }

  void Draw(int corner, StringId no_data, float background_opacity) const {
    constexpr ImU32 raw_color = IM_COL32(120, 199, 255, 255);
    constexpr ImU32 smoothed_color = IM_COL32(255, 188, 105, 255);
    const float font = ImGui::GetFontSize();
    if (BeginPollHud(corner, font * 26, background_opacity)) {
      DrawPollHudHeading(mode_line, weight);
      const ImVec2 top = ImGui::GetCursorScreenPos();
      const ImVec2 size(std::max(1.0f, ImGui::GetContentRegionAvail().x),
                        font * 5.5f);
      const ImVec2 end(top.x + size.x, top.y + size.y);
      ImGui::Dummy(size);

      auto* draw = ImGui::GetWindowDrawList();
      draw->PushClipRect(top, end, true);
      const float label_width =
          std::max({ImGui::CalcTextSize("0.00").x,
                    ImGui::CalcTextSize(raw_value.c_str()).x,
                    ImGui::CalcTextSize(smoothed_value.c_str()).x}) +
          8;
      const float plot_left = top.x + std::min(label_width, size.x * 0.4f);
      const float plot_width = end.x - plot_left;
      for (int second = 1; second < 5; ++second) {
        const float x = plot_left + plot_width * second / 5;
        draw->AddLine(ImVec2(x, top.y), ImVec2(x, end.y),
                      IM_COL32(255, 255, 255, 28));
      }
      const float duration =
          std::chrono::duration<float>(KbmInputDriver::EmaHudHistory::kDuration)
              .count();
      const auto speed_y = [&](float speed) {
        return end.y - 1 - speed / scale_max * (size.y - 2);
      };
      float guide_y[2]{};
      float label_y[2]{};
      const auto* latest = LatestSample();
      if (latest) {
        guide_y[0] = speed_y(latest->raw_speed_cm_s);
        guide_y[1] = speed_y(latest->smoothed_speed_cm_s);
        const float label_top = top.y + font + 4;
        const float label_bottom = end.y - font;
        const float label_spacing = font + 2;
        const int upper = guide_y[0] <= guide_y[1] ? 0 : 1;
        const int lower = 1 - upper;
        label_y[upper] = std::clamp(guide_y[upper] - font * 0.5f, label_top,
                                    label_bottom - label_spacing);
        label_y[lower] =
            std::clamp(guide_y[lower] - font * 0.5f,
                       label_y[upper] + label_spacing, label_bottom);

        const ImU32 guide_color[] = {IM_COL32(120, 199, 255, 208),
                                     IM_COL32(255, 188, 105, 208)};
        for (int i = 0; i < 2; ++i) {
          // Alternate dash positions when both speeds share the same height.
          const float y = guide_y[i];
          for (float x = plot_left + i * 7; x < end.x; x += 14) {
            draw->AddLine(ImVec2(x, y), ImVec2(std::min(x + 6, end.x), y),
                          guide_color[i], 1.5f);
          }
        }
      }
      for (const bool smoothed : {false, true}) {
        std::optional<Clock::time_point> previous;
        const ImU32 color = smoothed ? smoothed_color : raw_color;
        for (auto it = samples.rbegin(); it != samples.rend(); ++it) {
          if (previous &&
              it->time - *previous > std::chrono::milliseconds(100)) {
            draw->PathStroke(color, ImDrawFlags_None, 1.25f);
          }
          previous = it->time;
          const float age =
              std::chrono::duration<float>(sampled_at - it->time).count();
          const float speed =
              smoothed ? it->smoothed_speed_cm_s : it->raw_speed_cm_s;
          draw->PathLineTo(
              ImVec2(plot_left + std::clamp(1.0f - age / duration, 0.0f, 1.0f) *
                                     plot_width,
                     speed_y(speed)));
        }
        draw->PathStroke(color, ImDrawFlags_None, 1.25f);
      }
      if (latest) {
        const auto draw_label = [&](int index, const std::string& value,
                                    ImU32 color) {
          const float x = std::max(
              top.x, plot_left - ImGui::CalcTextSize(value.c_str()).x - 7);
          draw->AddLine(ImVec2(plot_left - 5, label_y[index] + font * 0.5f),
                        ImVec2(plot_left, guide_y[index]), color);
          draw->AddText(ImVec2(x, label_y[index]), color, value.c_str());
        };
        draw_label(0, raw_value, raw_color);
        draw_label(1, smoothed_value, smoothed_color);
      }
      draw->AddText(ImVec2(top.x + 4, top.y + 2), IM_COL32(232, 236, 241, 255),
                    scale_label.c_str());
      const char* span = Tr(StringId::kKbmPollHudEmaDuration);
      draw->AddText(ImVec2(end.x - ImGui::CalcTextSize(span).x - 4, top.y + 2),
                    IM_COL32(232, 236, 241, 255), span);
      if (samples.empty()) {
        const char* waiting = Tr(no_data);
        const float left =
            std::max(4.0f, (size.x - ImGui::CalcTextSize(waiting).x) * 0.5f);
        draw->AddText(ImVec2(top.x + left, top.y + (size.y - font) / 2),
                      IM_COL32(232, 236, 241, 255), waiting);
      }
      draw->PopClipRect();
      ImGui::TextColored(ImGui::ColorConvertU32ToFloat4(raw_color), "%s",
                         raw_label.c_str());
      NextButton(ImGui::CalcTextSize(smoothed_label.c_str()).x);
      ImGui::TextColored(ImGui::ColorConvertU32ToFloat4(smoothed_color), "%s",
                         smoothed_label.c_str());
    }
    ImGui::End();
  }

  std::vector<KbmInputDriver::EmaHudSample> samples;
  Clock::time_point sampled_at;
  double tau_ms = 0;
  float scale_max = kMinimumScaleCmS;
  std::string raw_value, smoothed_value;
  std::string mode_line, weight, scale_label, raw_label, smoothed_label;
};
}  // namespace

KbmConfigDialog::KbmConfigDialog(ui::ImGuiDrawer* imgui_drawer,
                                 EmulatorWindow& emulator_window)
    : ui::ImGuiDialog(imgui_drawer), emulator_window_(emulator_window) {
  settings_ = hid::kbm::GetSettingsFromCvars();
  saved_ = hid::kbm::GetConfigState() == hid::kbm::KbmConfigState::kCompatible;
}

KbmConfigDialog::~KbmConfigDialog() { StopCapture(); }

hid::kbm::KbmInputDriver* KbmConfigDialog::GetDriver() const {
  return FindKbmDriver(emulator_window_);
}

void KbmConfigDialog::MarkDraftChanged(bool reset_mouse_preview) {
  dirty_ = true;
  saved_ = false;
  if (!reset_mouse_preview) {
    return;
  }
  settings_ = hid::kbm::NormalizeSettings(settings_);
  if (auto* driver = GetDriver()) {
    driver->ResetMousePreview(settings_);
  }
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
    dirty_ = false;
    saved_ = true;
    UpdatePollHud(emulator_window_);
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
    driver->BeginBindingCapture(target !=
                                &settings_.raw_mouse_capture_toggle_key);
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
  const auto capture_prompt = allow_alternatives
                                  ? StringId::kKbmPressBinding
                                  : StringId::kKbmPressCaptureKey;
  const auto tooltip = allow_alternatives ? StringId::kKbmBindingTooltip
                                          : StringId::kKbmCaptureKeyTooltip;
  const float minimum_width =
      allow_alternatives ? 0
                         : std::max(ImGui::GetFontSize() * 8,
                                    ImGui::CalcTextSize(Tr(capture_prompt)).x +
                                        ImGui::GetStyle().FramePadding.x * 2);
  const float actions_width =
      ImGui::CalcTextSize("...").x + ImGui::GetStyle().FramePadding.x * 2;
  const float table_width =
      allow_alternatives ? 0
                         : std::min(ImGui::GetContentRegionAvail().x,
                                    minimum_width + actions_width +
                                        ImGui::GetStyle().CellPadding.x * 4);
  if (ImGui::BeginTable("Binding", 2, ImGuiTableFlags_SizingStretchProp,
                        ImVec2(table_width, 0))) {
    ImGui::TableSetupColumn("Value", ImGuiTableColumnFlags_WidthStretch);
    ImGui::TableSetupColumn("Actions", ImGuiTableColumnFlags_WidthFixed,
                            actions_width);
    ImGui::TableNextRow();
    ImGui::TableSetColumnIndex(0);
    auto tokens = BindingTokens(binding);
    ImGui::BeginDisabled(!GetDriver());
    if (tokens.empty() || (capturing && binding_capture_append_)) {
      const char* label =
          capturing ? Tr(capture_prompt) : Tr(StringId::kKbmUnbound);
      const float width = std::min(
          ImGui::GetContentRegionAvail().x,
          std::max(minimum_width, ImGui::CalcTextSize(label).x +
                                      ImGui::GetStyle().FramePadding.x * 2));
      if (ImGui::Button(label, ImVec2(width, 0))) {
        StartBindingCapture(&binding, false);
      }
      if (ImGui::IsItemHovered()) {
        Tooltip(Tr(tooltip));
      }
    } else {
      const float available = ImGui::GetContentRegionAvail().x;
      for (size_t i = 0; i < tokens.size(); ++i) {
        ImGui::PushID(int(i));
        const bool capturing_token =
            capturing &&
            (binding_capture_index_ == int(i) || binding_capture_index_ < 0);
        const std::string label = capturing_token
                                      ? Tr(capture_prompt)
                                      : hid::kbm::FormatKbmBinding(tokens[i]);
        const float width = std::min(
            available,
            std::max(minimum_width, ImGui::CalcTextSize(label.c_str()).x +
                                        ImGui::GetStyle().FramePadding.x * 2));
        if (i) {
          NextButton(width);
        }
        if (ImGui::Button((label + "##Value").c_str(), ImVec2(width, 0))) {
          StartBindingCapture(&binding, false, int(i));
        }
        if (ImGui::IsItemHovered()) {
          Tooltip(Tr(tooltip), label.c_str());
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
      Tooltip(Tr(StringId::kKbmBindingActions));
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
    MarkDraftChanged();
  }
  ImGui::Separator();
  DrawPreview();
  ImGui::Separator();
  if (DrawCaptureSettings()) {
    MarkDraftChanged();
  }
  if (ImGui::TreeNodeEx("KbmTuningHelp", 0, "%s",
                        Tr(StringId::kKbmTuningHelp))) {
    ImGui::TextWrapped("%s", Tr(StringId::kKbmPhysicalGuide));
    ImGui::Spacing();
    const auto mode = settings_.mouse_processing.mode;
    if (mode != KbmMouseMode::kEma) {
      ImGui::TextWrapped("%s", Tr(mode == KbmMouseMode::kFixedWindow
                                      ? StringId::kKbmFixedWindowHelp
                                      : StringId::kKbmDirectHelp));
      ImGui::Spacing();
    }
    ImGui::TextWrapped("%s", Tr(settings_.raw_mouse_anti_deadzone_shape ==
                                        hid::kbm::KbmAntiDeadzoneShape::kAxial
                                    ? StringId::kKbmAxialResponseHelp
                                    : StringId::kKbmMinimumResponseHelp));
    ImGui::TextDisabled("%s", Tr(StringId::kKbmSettingsFile));
    ImGui::TreePop();
  }
}

bool KbmConfigDialog::DrawProcessingTime() {
  auto& processing = settings_.mouse_processing;
  using hid::kbm::KbmMouseMode;
  bool changed = false;
  const bool fixed = processing.mode == KbmMouseMode::kFixedWindow;
  ImGui::PushID(static_cast<int>(processing.mode));
  ImGui::TextUnformatted(
      Tr(fixed ? StringId::kKbmWindowLength : StringId::kKbmEmaTau));
  double& time_ms = fixed ? processing.fixed_window_ms : processing.ema_tau_ms;
  const int low =
      int((fixed ? hid::kbm::kMinFixedWindowMs : hid::kbm::kMinEmaTauMs) * 2);
  const int high =
      int((fixed ? hid::kbm::kMaxFixedWindowMs : hid::kbm::kMaxEmaTauMs) * 2);
  int half_ms = int(time_ms * 2);
  const auto label = fmt::format("{:.1f} ms", time_ms);
  const float input_width = ImGui::GetFontSize() * 5.5f;
  ImGui::SetNextItemWidth(std::max(
      ImGui::GetFontSize() * 4, ImGui::GetContentRegionAvail().x - input_width -
                                    ImGui::GetStyle().ItemSpacing.x));
  if (ImGui::SliderInt("##ProcessingTime", &half_ms, low, high, label.c_str(),
                       ImGuiSliderFlags_NoInput)) {
    time_ms = half_ms * 0.5;
    changed = true;
  }
  ImGui::SameLine();
  ImGui::SetNextItemWidth(input_width);
  double entered = time_ms;
  if (ImGui::InputDouble("##ProcessingTimeInput", &entered, 0.0, 0.0,
                         "%.1f ms")) {
    auto updated = processing;
    (fixed ? updated.fixed_window_ms : updated.ema_tau_ms) = entered;
    updated = hid::kbm::NormalizeMouseProcessing(updated);
    changed |= updated != processing;
    processing = updated;
  }
  ImGui::PopID();
  return changed;
}

void KbmConfigDialog::PollStats::Refresh(KbmInputDriver* driver) {
  const double now = ImGui::GetTime();
  if (now < next_refresh) {
    return;
  }
  next_refresh = now + 0.5;
  Read(driver);
}

void KbmConfigDialog::PollStats::Read(KbmInputDriver* driver) {
  if (driver) {
    driver->CopyRecentGamePollIntervals(intervals);
  } else {
    intervals.clear();
  }
  if (!intervals.empty()) {
    latest_ms = intervals.front();
    std::sort(intervals.begin(), intervals.end());
  }
}

double KbmConfigDialog::PollStats::Percentile(double fraction) const {
  const double index = (intervals.size() - 1) * fraction;
  return std::lerp(intervals[size_t(index)],
                   intervals[size_t(std::ceil(index))],
                   index - std::floor(index));
}

double KbmConfigDialog::PollStats::Coverage(double window_ms) const {
  const auto covered =
      std::upper_bound(intervals.begin(), intervals.end(), window_ms) -
      intervals.begin();
  return 100.0 * covered / intervals.size();
}

void KbmConfigDialog::PollStats::Draw(double window_ms) const {
  if (intervals.empty()) {
    ImGui::TextWrapped("%s", Tr(StringId::kKbmNoRecentPollIntervals));
    return;
  }

  ImGui::TextDisabled(Tr(StringId::kKbmRecentPollIntervals),
                      unsigned(intervals.size()));
  const double median_ms = Percentile(0.5);
  const struct {
    const char* label;
    const char* format;
    double value;
  } metrics[] = {
      {Tr(StringId::kKbmCurrentPollInterval), "%.2f ms", latest_ms},
      {Tr(StringId::kKbmMedianPollInterval), "%.2f ms", median_ms},
      {"P95", "%.2f ms", Percentile(0.95)},
      {"P99", "%.2f ms", Percentile(0.99)},
      {Tr(StringId::kKbmPollIntervalCoverage), "%.1f%%", Coverage(window_ms)},
  };
  const int metric_count = int(std::size(metrics));
  const float min_cell_width =
      std::max(ImGui::CalcTextSize(metrics[0].label).x,
               ImGui::CalcTextSize(Tr(StringId::kKbmPollIntervalCoverage)).x) +
      ImGui::GetStyle().FramePadding.x * 2;
  const int fit = int(ImGui::GetContentRegionAvail().x / min_cell_width);
  const int columns =
      fit >= metric_count ? metric_count : std::clamp(fit, 1, 3);
  ImGui::PushStyleVar(ImGuiStyleVar_CellPadding,
                      ImGui::GetStyle().FramePadding);
  if (ImGui::BeginTable(
          "KbmPollMetrics", columns,
          ImGuiTableFlags_Borders | ImGuiTableFlags_SizingStretchSame)) {
    for (int i = 0; i < metric_count; ++i) {
      const auto& [label, format, value] = metrics[i];
      ImGui::TableNextColumn();
      if (i == metric_count - 1) {
        while (ImGui::TableGetColumnIndex() < columns - 1) {
          ImGui::TableNextColumn();
        }
      }
      ImGui::TextDisabled("%s", label);
      ImGui::Text(format, value);
    }
    ImGui::EndTable();
  }
  ImGui::PopStyleVar();
}

void KbmConfigDialog::DrawPollHudControls() {
  auto& overlay = settings_.overlay;
  const auto label =
      fmt::format("{}###KbmPollHudToggle", Tr(StringId::kKbmShowPollHud));
  const bool toggled = ImGui::Checkbox(label.c_str(), &overlay.enabled);
  const char* corners[] = {Tr(StringId::kKbmPollHudTopLeft),
                           Tr(StringId::kKbmPollHudTopRight),
                           Tr(StringId::kKbmPollHudBottomLeft),
                           Tr(StringId::kKbmPollHudBottomRight)};
  float corner_label_width = 0;
  for (const char* corner : corners) {
    corner_label_width =
        std::max(corner_label_width, ImGui::CalcTextSize(corner).x);
  }
  const float combo_width = corner_label_width + ImGui::GetFrameHeight() +
                            ImGui::GetStyle().FramePadding.x * 2;
  NextButton(combo_width);
  ImGui::SetNextItemWidth(combo_width);
  const bool moved = ImGui::Combo("##KbmPollHudCorner", &overlay.corner,
                                  corners, int(std::size(corners)));
  if (toggled || moved) {
    MarkDraftChanged(false);
  }
  if (toggled) {
    UpdatePollHud(emulator_window_);
  }
  if (overlay.enabled &&
      settings_.mouse_processing.mode == KbmMouseMode::kEma) {
    ImGui::AlignTextToFramePadding();
    ImGui::TextUnformatted(Tr(StringId::kKbmPollHudEmaTransparency));
    ImGui::SameLine();
    ImGui::SetNextItemWidth(-1);
    if (ImGui::SliderInt("##KbmEmaHudTransparency",
                         &overlay.ema_background_transparency, 0, 100, "%d%%",
                         ImGuiSliderFlags_AlwaysClamp)) {
      MarkDraftChanged(false);
    }
  }
}

void KbmConfigDialog::UpdatePollHud(EmulatorWindow& window) {
  auto& drawer = *window.imgui_drawer();
  auto* driver = FindKbmDriver(window);
  if (!driver) {
    drawer.SetPassiveOverlay({});
    return;
  }
  const auto settings = driver->GetSettings();
  const auto* config = window.kbm_config_dialog_.get();
  const auto& displayed = config ? config->settings_ : settings;
  const auto mode = displayed.mouse_processing.mode;
  const bool ema = mode == KbmMouseMode::kEma;
  driver->SetEmaHudSamplingEnabled(settings.overlay.enabled &&
                                   settings.mouse_processing.mode ==
                                       KbmMouseMode::kEma);
  if (!displayed.overlay.enabled || mode == KbmMouseMode::kDirect) {
    drawer.SetPassiveOverlay({});
    return;
  }
  drawer.SetPassiveOverlay([window = &window, ema,
                            active_processing = settings.mouse_processing,
                            saved_overlay = settings.overlay,
                            ema_hud = EmaHud{}, stats = PollStats{},
                            window_ms = 0.0, next_refresh = 0.0,
                            previously_open = false, mode_line = std::string{},
                            coverage = std::string{},
                            cells = std::array<std::string, 4>{},
                            cell_widths = std::array<float, 4>{},
                            grid_width = 0.0f]() mutable {
    const auto* config = window->kbm_config_dialog_.get();
    const bool preview = config && !config->IsClosing();
    const bool title_open = window->emulator()->is_title_open();
    if (title_open != previously_open) {
      previously_open = title_open;
      next_refresh = 0;
    }
    if (!preview &&
        (!title_open || window->imgui_drawer()->IsAnyDialogOpen())) {
      return;
    }

    const auto& overlay = preview ? config->settings_.overlay : saved_overlay;
    const auto& processing =
        preview ? config->settings_.mouse_processing : active_processing;
    const bool processing_matches =
        processing.mode == active_processing.mode &&
        (ema ? processing.ema_tau_ms == active_processing.ema_tau_ms
             : processing.fixed_window_ms == active_processing.fixed_window_ms);
    auto* driver =
        title_open && processing_matches ? FindKbmDriver(*window) : nullptr;
    const auto no_data_id = !processing_matches
                                ? StringId::kKbmPollHudSaveForLiveData
                            : !title_open ? StringId::kKbmPollHudWaitingForGame
                            : ema         ? StringId::kKbmPollHudEmaWaiting
                                          : StringId::kKbmNoRecentPollIntervals;
    const int corner = overlay.corner;
    const double now = ImGui::GetTime();
    if (ema) {
      if (now >= next_refresh || ema_hud.tau_ms != processing.ema_tau_ms) {
        next_refresh = now + 1.0 / 30.0;
        ema_hud.tau_ms = processing.ema_tau_ms;
        ema_hud.Refresh(driver);
      }
      const float opacity = 1.0f - overlay.ema_background_transparency * 0.01f;
      ema_hud.Draw(corner, no_data_id, opacity);
      return;
    }

    constexpr float cell_gap = 12;
    if (now >= next_refresh || window_ms != processing.fixed_window_ms) {
      next_refresh = now + 0.5;
      window_ms = processing.fixed_window_ms;
      mode_line = fmt::format("{} {:.1f} ms", Tr(StringId::kKbmModeFixedWindow),
                              window_ms);
      coverage.clear();
      grid_width = 0;
      stats.Read(driver);
      if (!stats.intervals.empty()) {
        coverage =
            fmt::format("{} {:.1f}%", Tr(StringId::kKbmPollIntervalCoverage),
                        stats.Coverage(window_ms));
        const char* labels[] = {Tr(StringId::kKbmCurrentPollInterval),
                                Tr(StringId::kKbmMedianPollInterval), "P95",
                                "P99"};
        const double values[] = {stats.latest_ms, stats.Percentile(0.5),
                                 stats.Percentile(0.95),
                                 stats.Percentile(0.99)};
        const float value_width = ImGui::CalcTextSize(" 888.8 ms").x;
        for (size_t i = 0; i < cells.size(); ++i) {
          cells[i] = fmt::format("{} {:.1f} ms", labels[i], values[i]);
          cell_widths[i] = ImGui::CalcTextSize(labels[i]).x + value_width;
          grid_width += cell_widths[i] + (i ? cell_gap : 0);
        }
      }
    }

    const char* no_data = Tr(no_data_id);
    const float content_width = std::max(
        ImGui::CalcTextSize(mode_line.c_str()).x +
            (coverage.empty()
                 ? 0
                 : cell_gap + ImGui::CalcTextSize(coverage.c_str()).x),
        stats.intervals.empty() ? ImGui::CalcTextSize(no_data).x : grid_width);
    if (BeginPollHud(corner, content_width)) {
      const float row_start = ImGui::GetCursorPosX();
      const float row_width = ImGui::GetContentRegionAvail().x;
      DrawPollHudHeading(mode_line, coverage);
      if (stats.intervals.empty()) {
        ImGui::TextWrapped("%s", no_data);
      } else {
        float offset = 0;
        for (size_t i = 0; i < cells.size(); ++i) {
          if (offset > 0 && offset + cell_widths[i] <= row_width) {
            ImGui::SameLine(row_start + offset);
          } else {
            offset = 0;
          }
          ImGui::TextWrapped("%s", cells[i].c_str());
          offset += cell_widths[i] + cell_gap;
        }
      }
    }
    ImGui::End();
  });
}

bool KbmConfigDialog::DrawMouseSettings() {
  bool changed = false;
  ImGui::BeginDisabled(!settings_.enabled);
  changed |=
      ImGui::Checkbox(Tr(StringId::kKbmEnableRawMouse), &settings_.raw_mouse);
  if (ImGui::IsItemHovered()) {
    Tooltip(Tr(StringId::kKbmEnableRawMouseTooltip));
  }
  ImGui::EndDisabled();
  if (!settings_.enabled) {
    ImGui::TextWrapped("%s", Tr(StringId::kKbmRawMouseRequiresController));
  }
  auto& processing = settings_.mouse_processing;
  const char* modes[] = {Tr(kMouseModes[0]), Tr(kMouseModes[1]),
                         Tr(kMouseModes[2])};
  int mode = static_cast<int>(processing.mode);
  if (ImGui::Combo(Tr(StringId::kKbmMouseMode), &mode, modes, 3)) {
    processing.mode = static_cast<KbmMouseMode>(mode);
    UpdatePollHud(emulator_window_);
    changed = true;
  }
  if (processing.mode != KbmMouseMode::kDirect) {
    if (BeginCard("KbmProcessingTuning")) {
      changed |= DrawProcessingTime();
      ImGui::Separator();
      if (processing.mode == KbmMouseMode::kFixedWindow) {
        poll_stats_.Refresh(GetDriver());
        poll_stats_.Draw(processing.fixed_window_ms);
      } else {
        ImGui::TextWrapped("%s", Tr(StringId::kKbmEmaTimeConstantHelp));
      }
      DrawPollHudControls();
    }
    ImGui::EndChild();
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
  using hid::kbm::KbmAntiDeadzoneShape;
  const float icon_size = ImGui::GetFontSize() * 0.6f;
  const ImVec2 button_padding(ImGui::GetStyle().FramePadding.x + 2,
                              ImGui::GetStyle().FramePadding.y);
  const float buttons_width = ButtonWidth(StringId::kKbmAntiDeadzoneCircle) +
                              ButtonWidth(StringId::kKbmAntiDeadzoneAxial) +
                              ImGui::CalcTextSize("        ").x + 8;
  NextButton(buttons_width);
  ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, button_padding);
  ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing,
                      ImVec2(0, ImGui::GetStyle().ItemSpacing.y));
  const auto shape_button = [&](KbmAntiDeadzoneShape shape, StringId label,
                                StringId help) {
    const bool selected = settings_.raw_mouse_anti_deadzone_shape == shape;
    ImGui::PushStyleColor(ImGuiCol_Button, ImGui::GetStyleColorVec4(
                                               selected ? ImGuiCol_ButtonActive
                                                        : ImGuiCol_FrameBg));
    const auto text = fmt::format("    {}", Tr(label));
    const bool clicked = ImGui::Button(text.c_str());
    ImGui::PopStyleColor();
    const ImVec2 corner = ImGui::GetItemRectMin();
    const ImVec2 center(
        corner.x + ImGui::GetStyle().FramePadding.x + icon_size * 0.5f,
        (corner.y + ImGui::GetItemRectMax().y) * 0.5f);
    auto* draw = ImGui::GetWindowDrawList();
    if (selected) {
      draw->AddRect(corner, ImGui::GetItemRectMax(),
                    ImGui::GetColorU32(ImGuiCol_CheckMark),
                    ImGui::GetStyle().FrameRounding);
    }
    const ImU32 color = ImGui::GetColorU32(ImGuiCol_Text);
    const float radius = icon_size * 0.5f;
    if (shape == KbmAntiDeadzoneShape::kRadial) {
      draw->AddCircle(center, radius, color, 16, 1.0f);
    } else {
      draw->AddRect(ImVec2(center.x - radius, center.y - radius),
                    ImVec2(center.x + radius, center.y + radius), color);
    }
    if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled)) {
      Tooltip(Tr(help));
    }
    if (clicked) {
      settings_.raw_mouse_anti_deadzone_shape = shape;
      changed = true;
    }
  };
  shape_button(KbmAntiDeadzoneShape::kRadial, StringId::kKbmAntiDeadzoneCircle,
               StringId::kKbmMinimumResponseHelp);
  ImGui::SameLine();
  shape_button(KbmAntiDeadzoneShape::kAxial, StringId::kKbmAntiDeadzoneAxial,
               StringId::kKbmAxialResponseHelp);
  ImGui::PopStyleVar(2);
  if (settings_.raw_mouse_anti_deadzone_shape == KbmAntiDeadzoneShape::kAxial) {
    changed |= DrawMinimumOutput(
        "MinimumResponseX", StringId::kKbmMinimumResponseX,
        settings_.raw_mouse_minimum_response_x, slider_width, precise_width);
    changed |= DrawMinimumOutput(
        "MinimumResponseY", StringId::kKbmMinimumResponseY,
        settings_.raw_mouse_minimum_response_y, slider_width, precise_width);
  } else {
    changed |= DrawMinimumOutput(
        "MinimumResponse", StringId::kKbmMinimumResponse,
        settings_.raw_mouse_minimum_response, slider_width, precise_width);
  }
  ImGui::EndDisabled();
  changed |= ImGui::Checkbox(Tr(StringId::kKbmInvertVertical),
                             &settings_.raw_mouse_invert_y);
  return changed;
}

void KbmConfigDialog::DrawPreview(bool visible) {
  auto* driver = GetDriver();
  if (!visible ||
      !ImGui::TreeNodeEx("KbmLivePreview", ImGuiTreeNodeFlags_SpanAvailWidth,
                         "%s", Tr(StringId::kKbmLivePreview))) {
    if (driver && driver->MousePreviewActive()) {
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
  if (settings_.raw_mouse_deadzone_compensation &&
      settings_.raw_mouse_anti_deadzone_shape ==
          hid::kbm::KbmAntiDeadzoneShape::kAxial) {
    ImGui::TextWrapped("%s", Tr(StringId::kKbmAxialMagnitudeHelp));
  }
  ImGui::TextWrapped(Tr(StringId::kKbmPreviewSpeed),
                     preview.speed_cm_s(settings_.raw_mouse_dpi),
                     settings_.raw_mouse_full_stick_speed_cm_s);
  ImGui::AlignTextToFramePadding();
  ImGui::Text(Tr(StringId::kKbmPreviewPeak), preview.peak() * 100);
  if (ImGui::IsItemHovered()) {
    Tooltip(Tr(StringId::kKbmPreviewSamplingHelp));
  }
  NextButton(ButtonWidth(StringId::kKbmResetPreviewPeak));
  if (ImGui::Button(Tr(StringId::kKbmResetPreviewPeak))) {
    driver->ResetMousePreview(settings_);
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
    preview_failed_ = !driver->StartMousePreview(settings_);
    focus_preview_stop_ = !preview_failed_;
  }
  if (preview_failed_ || driver->MousePreviewFailed()) {
    Error(StringId::kKbmPreviewFailed);
  }
  ImGui::TreePop();
}

bool KbmConfigDialog::DrawCaptureSettings() {
  bool changed = false;
  ImGui::AlignTextToFramePadding();
  ImGui::TextUnformatted(Tr(StringId::kKbmCaptureToggle));
  NextButton(ImGui::GetFontSize() * 15);
  changed |= DrawBinding("CaptureToggle",
                         settings_.raw_mouse_capture_toggle_key, false);
  if (ImGui::TreeNodeEx("KbmCaptureOptions", 0, "%s",
                        Tr(StringId::kKbmCaptureOptions))) {
    changed |= ImGui::Checkbox(Tr(StringId::kKbmCaptureOnStart),
                               &settings_.raw_mouse_capture_on_start);
    ImGui::TreePop();
  }
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
      const auto* emulator = emulator_window_.emulator();
      sampling_failed_ = emulator->is_title_open()
                             ? !driver->StartInputSampling(
                                   emulator->title_name(), emulator->title_id())
                             : !driver->StartInputSampling();
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
    const float output =
        std::hypot(float(diagnostics.thumb_x), float(diagnostics.thumb_y)) /
        32767.0f;
    const auto label = fmt::format(
        fmt::runtime(Tr(StringId::kKbmLastStickOutput)), output * 100);
    ImGui::TextWrapped("%s", Tr(StringId::kKbmSnapshot));
    ImGui::ProgressBar(std::min(output, 1.0f), ImVec2(-1, 0), label.c_str());
    if (ImGui::IsItemHovered()) {
      Tooltip(Tr(StringId::kKbmAxialMagnitudeHelp));
    }
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
      MarkDraftChanged();
      UpdatePollHud(emulator_window_);
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
    const bool preview_active = driver && driver->MousePreviewActive();
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
      MarkDraftChanged();
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
              MarkDraftChanged();
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
      MarkDraftChanged();
    }
    close |= DrawConfirmations();
    preview_was_active_ = driver && driver->MousePreviewActive();
  } else if (!open) {
    RequestClose();
  }
  ImGui::End();
  if (close) {
    StopCapture();
    Close();
    emulator_window_.ToggleKbmConfigDialog();
  }
}
}  // namespace xe::app
