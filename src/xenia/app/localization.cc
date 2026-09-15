/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2026 Xenia Canary. All rights reserved.                          *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#include "xenia/app/localization.h"

#include <array>
#include <cstddef>

#include "xenia/base/cvar.h"
#include "xenia/config.h"

DEFINE_string(ui_language, "en",
              "Host interface language. Use: [en, zh-TW]. This does not "
              "change the emulated Xbox 360 language.",
              "General");

namespace xe::app::localization {
namespace {

struct Translation {
  const char* english;
  const char* traditional_chinese;
};

constexpr std::array<Translation, static_cast<size_t>(StringId::kCount)>
    kTranslations = {{
        {"&File", "檔案(&F)"},
        {"&Open Recent", "最近開啟(&R)"},
        {"&Zar Package", "Zar 封裝(&Z)"},
        {"&Open...", "開啟(&O)..."},
        {"Install Content...", "安裝內容..."},
        {"Create", "建立"},
        {"Extract", "解壓縮"},
        {"Close", "關閉"},
        {"Show content directory...", "顯示內容目錄..."},
        {"E&xit", "結束(&X)"},
        {"&Profile", "玩家設定檔(&P)"},
        {"&Show Profile Menu", "開啟玩家設定檔(&S)"},
        {"&Content", "內容(&N)"},
        {"Install Content", "安裝內容"},
        {"Extract Content", "解壓內容"},
        {"Show Installed Content", "顯示已安裝內容"},
        {"&CPU", "&CPU"},
        {"&Reset Time Scalar", "重設 Time Scalar(&R)"},
        {"Time Scalar /= 2", "Time Scalar /= 2"},
        {"Time Scalar *= 2", "Time Scalar *= 2"},
        {"Toggle Profiler &Display", "切換 Profiler 顯示(&D)"},
        {"&Pause/Resume Profiler", "暫停／繼續 Profiler(&P)"},
        {"&Break and Show Guest Debugger", "中斷並顯示 Guest Debugger(&B)"},
        {"&Break into Host Debugger", "中斷至 Host Debugger(&B)"},
        {"&GPU", "&GPU"},
        {"&Trace Frame", "Trace Frame(&T)"},
        {"&Clear Runtime Caches", "清除 Runtime Cache(&C)"},
        {"&Display", "顯示(&D)"},
        {"&Post-processing settings", "後處理設定(&P)"},
        {"&Fullscreen", "全螢幕(&F)"},
        {"&Take Screenshot", "擷取螢幕畫面(&T)"},
        {"&KBM Controller", "&KBM 控制器"},
        {"&KBM Controller settings...", "KBM 控制器設定(&K)..."},
        {"&HID", "&HID"},
        {"&Toggle controller vibration", "切換控制器震動(&T)"},
        {"&Display controller hotkeys", "顯示控制器 Hotkey(&D)"},
        {"&XMP", "&XMP"},
        {"&Show XMP Menu", "顯示 XMP 選單(&S)"},
        {"&Console", "主機(&C)"},
        {"&Open console settings", "開啟主機設定(&O)"},
        {"&UI", "&UI"},
        {"Interface &Language", "介面語言(&L)"},
        {"English", "English"},
        {"English (Active)", "English（目前使用）"},
        {"繁體中文", "繁體中文"},
        {"Traditional Chinese (Active)", "繁體中文（目前使用）"},
        {"&Help", "說明(&H)"},
        {"FA&Q...", "常見問題(&Q)..."},
        {"Game &compatibility...", "遊戲相容性(&C)..."},
        {"Fork build commit on GitHub...",
         "在 GitHub 顯示 fork 版本 commit..."},
        {"Upstream base commit on GitHub...",
         "在 GitHub 顯示上游基底 commit..."},
        {"Recent changes on GitHub...", "在 GitHub 顯示最近變更..."},
        {"&About...", "關於(&A)..."},

        {"Audio Player Menu", "音訊播放器選單"},
        {"Audio player status:", "音訊播放器狀態："},
        {"Idle", "閒置"},
        {"Paused", "已暫停"},
        {"Playing", "播放中"},
        {"Pause", "暫停"},
        {"Resume", "繼續"},
        {"Audio player volume", "音訊播放器音量"},

        {"Profiles Menu", "玩家設定檔"},
        {"No Profiles Found", "找不到玩家設定檔"},
        {"There is no profile available! You will not be able to save without "
         "one.\n\nWould you like to create one?",
         "目前沒有玩家設定檔，遊戲將無法儲存進度。\n\n要建立設定檔嗎？"},
        {"Create Profile", "建立設定檔"},
        {"Create profile & migrate data", "建立設定檔並移轉資料"},
        {"Open profile menu", "開啟設定檔選單"},
        {"No profiles found!", "找不到玩家設定檔！"},
        {"Login", "登入"},
        {"Login to slot:", "登入至玩家欄位："},
        {"slot {}", "玩家欄位 {}"},
        {"Logout", "登出"},
        {"Modify", "編輯"},
        {"Show Played Titles", "查看玩過的遊戲"},
        {"Show Content Directory", "開啟內容目錄"},
        {"Delete Profile", "刪除設定檔"},
        {"You're about to delete profile: {} (XUID: {:016X}). This will remove "
         "all data assigned to this profile including savefiles. Are you sure?",
         "即將刪除設定檔：{}（XUID：{:016X}"
         "）。此操作會刪除該設定檔的所有資料，包括存檔。確定要繼續嗎？"},
        {"Yes, delete it!", "確定刪除"},
        {"User: {}\n", "玩家：{}\n"},
        {"Assigned to slot: {}\n", "已指派至玩家欄位：{}\n"},
        {"Profile is not signed in", "此設定檔尚未登入"},

        {"Post-processing", "後製處理設定"},
        {"All effects can be used on GPUs of any brand.",
         "所有效果皆可用於任何品牌的 GPU。"},
        {"Anti-aliasing", "反鋸齒"},
        {"None", "無"},
        {"NVIDIA Fast Approximate Anti-Aliasing (FXAA) [Normal Quality]",
         "NVIDIA Fast Approximate Anti-Aliasing (FXAA)［一般品質］"},
        {"NVIDIA Fast Approximate Anti-Aliasing (FXAA) [Extreme Quality]",
         "NVIDIA Fast Approximate Anti-Aliasing (FXAA)［極致品質］"},
        {"Resampling and sharpening", "重新取樣與銳利化"},
        {"None / Bilinear", "無／雙線性"},
        {"AMD FidelityFX Contrast Adaptive Sharpening (CAS)",
         "AMD FidelityFX Contrast Adaptive Sharpening (CAS)"},
        {"AMD FidelityFX Super Resolution 1.0 (FSR)",
         "AMD FidelityFX Super Resolution 1.0 (FSR)"},
        {"Simple bilinear filtering is done if resampling is "
         "needed.\nOtherwise, only anti-aliasing is done if enabled, or "
         "displaying as is.",
         "需要重新取樣時，會使用簡單的雙線性過濾。\n否則僅套用已啟用的反鋸齒，"
         "或直接顯示原始畫面。"},
        {"Sharpening and resampling to up to 2x2 to improve the fidelity of "
         "details.\nFor scaling by more than 2x2, bilinear stretching is done "
         "afterwards.",
         "執行銳利化及最高 2x2 的重新取樣，以改善細節清晰度。\n縮放超過 2x2 "
         "時，之後會使用雙線性拉伸。"},
        {"High-quality edge-preserving upscaling to arbitrary target "
         "resolutions.\nFor scaling by more than 2x2, multiple upsampling "
         "passes are done.\nIf not upscaling, Contrast Adaptive Sharpening "
         "(CAS) is used instead.",
         "使用保留邊緣的高品質影像升頻，輸出至任意目標解析度。\n縮放超過 2x2 "
         "時會執行多次升頻處理。\n若未進行升頻，則改用 Contrast Adaptive "
         "Sharpening (CAS)。"},
        {"FXAA is highly recommended when using CAS or FSR.",
         "使用 CAS 或 FSR 時，強烈建議啟用 FXAA。"},
        {"FSR sharpness reduction when upscaling (lower is sharper):",
         "FSR 升頻銳利度降低（越低越銳利）："},
        {"CAS additional sharpness when not upscaling (higher is sharper):",
         "未使用升頻時的 CAS 額外銳利度（越高越銳利）："},
        {"CAS additional sharpness (higher is sharper):",
         "CAS 額外銳利度（越高越銳利）："},
        {"Reset", "重設"},
        {"Dithering", "Dithering（色彩抖動）"},
        {"Dither the final output to 8bpc to make gradients smoother",
         "對最終輸出套用 8bpc 色彩抖動，使漸層更平滑"},

        {"KBM Controller Settings", "KBM 控制器設定"},
        {"The KBM Controller HID backend is not active. These settings will "
         "be saved, but input requires hid = \"kbm\" on the next launch.",
         "KBM 控制器 HID 後端尚未啟用。設定仍會儲存，但下次啟動時必須使用 hid "
         "= \"kbm\" 才能輸入。"},
        {"Press a key or mouse button...", "請按下按鍵或滑鼠按鈕..."},
        {"Click, then press a key or mouse button. Hold Ctrl, Alt, Shift, or "
         "Win to create a chord.",
         "點擊後按下鍵盤或滑鼠按鈕；按住 Ctrl、Alt、Shift 或 Win "
         "可建立組合鍵。"},
        {"Add alternative binding", "新增替代綁定"},
        {"Clear binding", "清除綁定"},
        {"KBM Controller", "KBM 控制器"},
        {"Enable KBM Controller", "啟用 KBM 控制器"},
        {"Controller slot", "控制器插槽"},
        {"Player 1", "玩家 1"},
        {"Player 2", "玩家 2"},
        {"Player 3", "玩家 3"},
        {"Player 4", "玩家 4"},
        {"Click a binding and press the key or mouse button you want. Hold "
         "Ctrl, Alt, Shift, or Win for a chord. Use + to add an alternative.",
         "點擊綁定欄位後按下想要的鍵盤或滑鼠按鈕。按住 Ctrl、Alt、Shift 或 Win "
         "可建立組合鍵；使用 + 新增替代按鍵。"},
        {"Xbox 360 input", "Xbox 360 輸入"},
        {"Keyboard / mouse", "鍵盤／滑鼠"},
        {"Mouse", "滑鼠"},
        {"Enable Raw Input mouse control", "啟用 Raw Input 滑鼠控制"},
        {"Enable KBM Controller to configure Raw Input mouse control.",
         "啟用 KBM 控制器後才能設定 Raw Input 滑鼠控制。"},
        {"Mouse sensitivity multiplier:", "滑鼠靈敏度倍率："},
        {"Initial reference: 3600 DPI at 10x. This is not universal; in-game "
         "settings and controller limits still apply.",
         "初始參考：3600 DPI 使用 "
         "10x。這不是通用設定；遊戲內靈敏度與搖桿限制仍會影響手感。"},
        {"Invert vertical look", "反轉垂直視角"},
        {"Capture mouse when KBM Controller starts",
         "KBM 控制器啟動時啟用滑鼠鎖定"},
        {"Mouse capture hotkey:", "滑鼠鎖定熱鍵："},
        {"Advanced tuning (optional)", "進階調校（選用）"},
        {"Mouse response curve (1.0 is linear):",
         "滑鼠反應曲線（1.0 為線性）："},
        {"Above 1.0 slows small movements; below 1.0 boosts them.",
         "高於 1.0 會降低小幅移動的輸出；低於 1.0 則會提高。"},
        {"Mouse smoothing (0 ms is direct):", "滑鼠平滑（0 ms 為直通）："},
        {"Time-based velocity smoothing. 4–12 ms usually improves fine aim; "
         "higher values add more lag.",
         "依時間平滑滑鼠速度。4–12 ms 通常可改善微調；數值越高延遲越明顯。"},
        {"Compensate analog-stick deadzone (Experimental)",
         "補償類比搖桿 Deadzone（實驗性）"},
        {"Minimum stick output:", "最小搖桿輸出："},
        {"Only affects non-zero Raw Input movement. Off preserves the original "
         "mouse response and never creates idle stick input.",
         "只影響非零的 Raw Input "
         "移動。關閉時保留原本滑鼠反應，靜止時絕不產生搖桿輸入。"},
        {"Full-stick speed at 1x (counts/s):",
         "1x 時達到搖桿滿輸出的速度（counts/s）："},
        {"At %.3fx, full-stick output begins near %.0f counts/s. Lower values "
         "reach the game's turn-speed cap sooner.",
         "在 %.3fx 時，約 %.0f counts/s "
         "會開始輸出滿搖桿。數值越低，越早達到遊戲的轉向速度上限。"},
        {"Diagnostics", "診斷"},
        {"Latest right-stick output: {:.0f}%{}", "最新右搖桿輸出：{:.0f}%{}"},
        {" (maximum)", "（最大值）"},
        {"Raw Input speed X %.0f / Y %.0f counts/s | Stick X %d / Y %d",
         "Raw Input 速度 X %.0f / Y %.0f counts/s｜Stick X %d / Y %d"},
        {"Raw Input requested: %s | registered: %s | Mouse capture requested: "
         "%s | active: %s",
         "Raw Input：要求 %s｜註冊 %s｜滑鼠擷取要求 %s｜目前狀態 %s"},
        {"registered", "已註冊"},
        {"on", "啟用"},
        {"off", "關閉"},
        {"active", "已啟用"},
        {"released", "已解除"},
        {"KBM Controller input is temporarily paused while this window is "
         "open.",
         "此設定視窗開啟時，KBM 控制器輸入會暫時停止傳送至遊戲。"},
        {"Runtime diagnostics are unavailable.", "執行階段診斷目前無法使用。"},
        {"Save", "儲存"},
        {"Cancel", "取消"},
        {"Reset all", "全部重設"},
        {"Settings file: kbm.toml", "設定檔：kbm.toml"},
        {"Failed to save kbm.toml. Check the log.",
         "無法儲存 kbm.toml，請檢查記錄檔。"},
        {"Save and close", "儲存並關閉"},
        {"Unsaved changes", "尚未儲存變更"},
        {"Settings saved", "設定已儲存"},
        {"Unbound", "未綁定"},
        {"Bind Escape", "綁定 Esc"},
        {"Cancel capture", "取消擷取"},
        {"Last gameplay sample (not live while this window is open)",
         "上次遊戲輸入樣本（此視窗開啟時非即時更新）"},
        {"Input sampling", "輸入取樣"},
        {"Start 60-second capture", "開始 60 秒取樣"},
        {"Stop and save report", "停止並儲存報告"},
        {"Cancel sampling", "取消取樣"},
        {"Close this window, play normally, then reopen it to review the report.",
         "關閉此視窗後正常遊玩，完成後重新開啟此頁查看報告。"},
        {"Recording: %.0f s remaining | %zu samples",
         "正在取樣：剩餘 %.0f 秒｜%zu 筆樣本"},
        {"Report saved: %s", "報告已儲存：%s"},
        {"Failed to save the input report. Check the log.",
         "無法儲存輸入報告，請檢查記錄檔。"},
        {"No input report has been generated yet.", "尚未產生輸入報告。"},
        {"Xbox Guide", "Xbox 導覽"},
        {"D-pad Left", "方向鍵左"},
        {"D-pad Right", "方向鍵右"},
        {"D-pad Down", "方向鍵下"},
        {"D-pad Up", "方向鍵上"},
        {"Left Stick Left", "左搖桿左"},
        {"Left Stick Right", "左搖桿右"},
        {"Left Stick Down", "左搖桿下"},
        {"Left Stick Up", "左搖桿上"},
        {"Left Stick Click (LS)", "左搖桿按下（LS）"},
        {"Right Stick Up", "右搖桿上"},
        {"Right Stick Down", "右搖桿下"},
        {"Right Stick Right", "右搖桿右"},
        {"Right Stick Left", "右搖桿左"},
        {"Right Stick Click (RS)", "右搖桿按下（RS）"},
        {"X Button", "X 按鈕"},
        {"B Button", "B 按鈕"},
        {"A Button", "A 按鈕"},
        {"Y Button", "Y 按鈕"},
        {"Left Trigger (LT)", "左扳機（LT）"},
        {"Right Trigger (RT)", "右扳機（RT）"},
        {"Back", "返回"},
        {"Start", "開始"},
        {"Left Bumper (LB)", "左肩鍵（LB）"},
        {"Right Bumper (RB)", "右肩鍵（RB）"},
        {"Mouse capture enabled", "滑鼠鎖定已啟用"},
        {"Mouse capture released", "滑鼠鎖定已解除"},
        {"Mouse captured by Xenia. Press {} to release.",
         "滑鼠已由 Xenia 鎖定。按下 {} 解除。"},
        {"the mouse capture hotkey", "滑鼠鎖定熱鍵"},
        {"Mouse capture released.", "滑鼠鎖定已解除。"},

        {"Controller Hotkeys", "控制器熱鍵"},
        {"Gameplay Hotkeys", "遊戲中熱鍵"},
        {" (Disabled)", "（已停用）"},
        {"A + Guide = Toggle Readback Resolve",
         "A + Guide = 切換 Readback Resolve"},
        {"B + Guide = Toggle between loglevel set in config and the 'Disabled' "
         "loglevel.",
         "B + Guide = 在 config 設定的 loglevel 與 Disabled 之間切換。"},
        {"Y + Guide = Toggle Fullscreen", "Y + Guide = 切換全螢幕"},
        {"X + Guide = Toggle Clear Memory Page State",
         "X + Guide = 切換 Clear Memory Page State"},
        {"Right Shoulder + Guide = Clear GPU Cache",
         "Right Bumper (RB) + Guide = 清除 GPU Cache"},
        {"Left Shoulder + Guide = Toggle Controller Vibration",
         "Left Bumper (LB) + Guide = 切換控制器震動"},
        {"D-PAD Down + Guide = Half CPU Scalar",
         "D-Pad 向下 + Guide = CPU Scalar 減半"},
        {"D-PAD Up + Guide = Double CPU Scalar",
         "D-Pad 向上 + Guide = CPU Scalar 加倍"},
        {"D-PAD Right + Guide = Reset CPU Scalar",
         "D-Pad 向右 + Guide = 重設 CPU Scalar"},
        {"Y = Toggle Fullscreen", "Y = 切換全螢幕"},
        {"Start = Run Selected Title", "Start = 啟動選取的遊戲"},
        {"Back + Start = Toggle between loglevel set in config and the "
         "'Disabled' loglevel.",
         "Back + Start = 在 config 設定的 loglevel 與 Disabled 之間切換。"},
        {"D-PAD Down = Title Selection +1", "D-Pad 向下 = 選擇下一個遊戲"},
        {"D-PAD Up = Title Selection -1", "D-Pad 向上 = 選擇上一個遊戲"},
        {"Readback Resolve", "Readback Resolve"},
        {"Clear Memory Page State", "Clear Memory Page State"},
        {"Controller Hotkeys", "控制器熱鍵"},
        {"Enabled", "已啟用"},
        {"Disabled", "已停用"},
        {"Title Selection", "選擇遊戲"},

        {"Console settings", "主機設定"},
        {"Unknown", "未知"},
        {"Resolution", "解析度"},
        {"Timezone", "時區"},
        {"User", "使用者"},
        {"Time", "時間"},
        {"Disable Daylight-Saving Time", "停用日光節約時間"},
        {"24H Time", "24 小時制"},
        {"Locale", "地區設定"},
        {"Language", "語言"},
        {"Country", "國家／地區"},
        {"Profile", "玩家設定檔"},
        {"Default Profile", "預設玩家設定檔"},
        {"Parental Control", "家長監護"},
        {"Retail Options", "Retail 選項"},
        {"Dashboard Initialized", "Dashboard 已初始化"},
        {"IPTV Initialized", "IPTV 已初始化"},
        {"DVR Initialized", "DVR 已初始化"},
        {"Kinect Initialized", "Kinect 已初始化"},
        {"System", "系統"},
        {"Video Options", "視訊選項"},
        {"AV Region", "AV 區域"},
        {"Widescreen", "寬螢幕"},
        {"Audio Options", "音訊選項"},
        {"Mono", "單聲道"},
        {"Low Latency (unsupported)", "Low Latency（尚未支援）"},
        {"Audio player volume", "Audio player 音量"},
        {"Network", "網路"},
        {"MAC Address: ", "MAC 位址："},
        {"Network ID: ", "Network ID："},
        {"Save", "儲存"},
        {"Settings Saved!", "設定已儲存！"},
        {"Reset", "重設"},
        {"English", "英文"},
        {"Japanese", "日文"},
        {"German", "德文"},
        {"French", "法文"},
        {"Spanish", "西班牙文"},
        {"Italian", "義大利文"},
        {"Korean", "韓文"},
        {"Traditional Chinese", "繁體中文"},
        {"Portuguese", "葡萄牙文"},
        {"Polish", "波蘭文"},
        {"Russian", "俄文"},
        {"Swedish", "瑞典文"},
        {"Turkish", "土耳其文"},
        {"Norwegian", "挪威文"},
        {"Dutch", "荷蘭文"},
        {"Simplified Chinese", "簡體中文"},
    }};

}  // namespace

Language GetLanguage() {
  return cvars::ui_language == "zh-TW" || cvars::ui_language == "zh-tw" ||
                 cvars::ui_language == "zh_TW" || cvars::ui_language == "zh_tw"
             ? Language::kTraditionalChinese
             : Language::kEnglish;
}

bool IsTraditionalChinese() {
  return GetLanguage() == Language::kTraditionalChinese;
}

void SetLanguage(Language language, bool save_config) {
  const char* value =
      language == Language::kTraditionalChinese ? "zh-TW" : "en";
  if (cvars::ui_language != value) {
    OVERRIDE_string(ui_language, value);
  }
  if (save_config) {
    config::SaveConfig();
  }
}

const char* Get(StringId id) {
  const size_t index = static_cast<size_t>(id);
  if (index >= kTranslations.size()) {
    return "";
  }
  const Translation& translation = kTranslations[index];
  return IsTraditionalChinese() ? translation.traditional_chinese
                                : translation.english;
}

}  // namespace xe::app::localization
