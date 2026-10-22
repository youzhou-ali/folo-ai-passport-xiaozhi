#include "eye_display.h"

#include <esp_lcd_panel_ops.h>
#include <esp_log.h>
#include <material_symbols.h>

#include <algorithm>
#include <cstring>

#include "application.h"
#include "assets/lang_config.h"
#include "audio_codec.h"
#include "board.h"
#include "lvgl_theme.h"

namespace {
constexpr const char* kTag = "PassportEyes";
constexpr const char* kPrompt = "按 OK 开始对话";
constexpr const char* kThinking = "思考中";
constexpr uint32_t kFramePeriodMs = 50;
}  // namespace

PassportEyeDisplay::~PassportEyeDisplay() {
    // These inherited ESP timers are unused by this scene. Retire them before
    // removing any widgets; the only active animation belongs to the LVGL task.
    if (notification_timer_) {
        esp_timer_stop(notification_timer_);
        esp_timer_delete(notification_timer_);
        notification_timer_ = nullptr;
    }
    if (preview_timer_) {
        esp_timer_stop(preview_timer_);
        esp_timer_delete(preview_timer_);
        preview_timer_ = nullptr;
    }
    DisplayLockGuard lock(this);
    if (lock) {
        if (frame_timer_) {
            lv_timer_delete(frame_timer_);
            frame_timer_ = nullptr;
        }
        view_.Destroy();
    }
}

void PassportEyeDisplay::SetupUI() {
    DisplayLockGuard lock(this);
    if (!lock || setup_ui_called_) {
        return;
    }
    if (!current_theme_) {
        current_theme_ = LvglThemeManager::GetInstance().GetTheme("dark");
    }
    auto theme = static_cast<LvglTheme*>(current_theme_);
    view_.Create(lv_display_get_screen_active(display_), width_, height_,
                 theme->text_font()->font(), theme->icon_font()->font());
    status_text_ = Lang::Strings::INITIALIZING;
    view_.SetStatus(status_text_.c_str());
    view_.SetSubtitle(kPrompt);
    setup_ui_called_ = true;
    model_.SetDeviceState(Application::GetInstance().GetDeviceState(), lv_tick_get());
    frame_timer_ = lv_timer_create(
        [](lv_timer_t* timer) {
            static_cast<PassportEyeDisplay*>(lv_timer_get_user_data(timer))->RenderFrame();
        },
        kFramePeriodMs, this);
    RenderFrame();
    CheckFont(theme->text_font()->font());
}

void PassportEyeDisplay::CheckFont(const lv_font_t* font) {
    bool covered = true;
    for (char32_t codepoint : U"按开始对话思考中") {
        if (codepoint == 0) {
            continue;
        }
        lv_font_glyph_dsc_t glyph{};
        if (!lv_font_get_glyph_dsc(font, &glyph, codepoint, 0) || glyph.is_placeholder) {
            ESP_LOGW(kTag, "UI font missing U+%04lx", static_cast<unsigned long>(codepoint));
            covered = false;
        }
    }
    if (covered) {
        ESP_LOGI(kTag, "Chinese UI font glyph coverage: PASS");
    }
}

void PassportEyeDisplay::RenderFrame() {
    const uint32_t now = lv_tick_get();
    const auto mood = model_.mood(now);
    view_.Render(model_.Frame(now), mood, now);
    view_.ScrollSubtitle(passport::SubtitleScrollOffset(
        now - caption_started_, view_.SubtitleHeight(), view_.SubtitleViewportHeight()));
    if (notification_duration_ != 0 && now - notification_started_ >= notification_duration_) {
        notification_duration_ = 0;
    }
    if (notification_duration_ == 0) {
        view_.SetStatus(mood == passport::EyeMood::Thinking && state_ == kDeviceStateListening
                            ? kThinking
                            : status_text_.c_str());
    }
}

void PassportEyeDisplay::OnDeviceStateChanged(DeviceState state) {
    SetPowerSaveMode(false);
    DisplayLockGuard lock(this);
    if (!lock) {
        return;
    }
    const bool new_conversation = state == kDeviceStateConnecting ||
                                  (state == kDeviceStateListening && state_ == kDeviceStateIdle);
    state_ = state;
    model_.SetDeviceState(state, lv_tick_get());
    if (setup_ui_called_) {
        if (new_conversation) {
            has_caption_ = false;
            conversational_caption_ = false;
            caption_started_ = lv_tick_get();
            view_.SetSubtitle(kPrompt);
        }
        RenderFrame();
    }
}

void PassportEyeDisplay::SetStatus(const char* status) {
    SetPowerSaveMode(false);
    DisplayLockGuard lock(this);
    if (!lock) {
        return;
    }
    status_text_ = status ? status : "";
    model_.Touch(lv_tick_get());
    notification_duration_ = 0;
    if (setup_ui_called_) {
        view_.SetStatus(status_text_.c_str());
    }
}

void PassportEyeDisplay::SetEmotion(const char* emotion) {
    if (!emotion || emotion[0] == '\0') {
        return;
    }
    SetPowerSaveMode(false);
    DisplayLockGuard lock(this);
    if (lock) {
        model_.SetEmotion(emotion, lv_tick_get());
        if (setup_ui_called_) {
            RenderFrame();
        }
    }
}

void PassportEyeDisplay::SetChatMessage(const char* role, const char* content) {
    SetPowerSaveMode(false);
    DisplayLockGuard lock(this);
    if (!lock || !setup_ui_called_) {
        return;
    }
    const bool incoming_text = content && content[0];
    // End-of-session callbacks send an empty message before changing to Idle.
    // Retain conversational subtitles here as well as in ClearChatMessages().
    if (!incoming_text && has_caption_ && conversational_caption_ &&
        state_ != kDeviceStateConnecting) {
        return;
    }
    caption_started_ = lv_tick_get();
    has_caption_ = incoming_text;
    conversational_caption_ =
        role && (std::strcmp(role, "assistant") == 0 || std::strcmp(role, "user") == 0);
    view_.SetSubtitle(has_caption_ ? content : kPrompt);
    view_.SetSubtitleHidden(hide_subtitle_);
    if (has_caption_) {
        model_.OnMessage(role, caption_started_);
    }
    RenderFrame();
}

void PassportEyeDisplay::ClearChatMessages() {
    DisplayLockGuard lock(this);
    if (!lock || !setup_ui_called_) {
        return;
    }
    // Leave the last reply available for reading when the audio session ends.
    // The next conversation's empty system message explicitly resets it.
    if (state_ != kDeviceStateIdle || !has_caption_) {
        has_caption_ = false;
        view_.SetSubtitle(kPrompt);
        caption_started_ = lv_tick_get();
    }
}

void PassportEyeDisplay::ShowNotification(const char* text, int duration_ms) {
    OnUserActivity();
    DisplayLockGuard lock(this);
    if (!lock || !setup_ui_called_) {
        return;
    }
    notification_started_ = lv_tick_get();
    notification_duration_ = std::clamp(duration_ms, 1, 60000);
    view_.SetStatus(text);
}

void PassportEyeDisplay::SetTheme(Theme* theme) {
    if (!theme) {
        return;
    }
    DisplayLockGuard lock(this);
    if (!lock) {
        return;
    }
    auto lvgl_theme = static_cast<LvglTheme*>(theme);
    if (setup_ui_called_) {
        // Keep the eye scene dark, including after cloud font/theme updates.
        view_.SetFonts(lvgl_theme->text_font()->font(), lvgl_theme->icon_font()->font());
        caption_started_ = lv_tick_get();
        CheckFont(lvgl_theme->text_font()->font());
    }
    Display::SetTheme(theme);
}

void PassportEyeDisplay::SetHideSubtitle(bool hide) {
    DisplayLockGuard lock(this);
    if (lock) {
        hide_subtitle_ = hide;
        if (setup_ui_called_) {
            view_.SetSubtitleHidden(hide);
        }
    }
}

void PassportEyeDisplay::OnUserActivity() {
    SetPowerSaveMode(false);
    DisplayLockGuard lock(this);
    if (lock) {
        model_.Touch(lv_tick_get());
    }
}

void PassportEyeDisplay::SetPowerSaveMode(bool on) {
    // Network callbacks can wake the display concurrently with the idle tick.
    // Keep the state, animation and physical panel transition in one lock.
    DisplayLockGuard lock(this);
    const uint32_t now = lv_tick_get();
    if (!lock || !setup_ui_called_ || sleeping_ == on || (on && !model_.ShouldSleep(now))) {
        return;
    }
    sleeping_ = on;
    model_.SetSleeping(on, now);
    if (on) {
        lv_timer_pause(frame_timer_);
    } else {
        lv_timer_resume(frame_timer_);
        lv_timer_ready(frame_timer_);
    }
    auto backlight = Board::GetInstance().GetBacklight();
    if (on) {
        if (backlight) {
            backlight->SetBrightness(0);
        }
        esp_lcd_panel_disp_on_off(panel_, false);
    } else {
        esp_lcd_panel_disp_on_off(panel_, true);
        if (backlight) {
            backlight->RestoreBrightness();
        }
    }
    ESP_LOGI(kTag, "Screen %s", on ? "off (idle)" : "awake");
}

void PassportEyeDisplay::UpdateStatusBar(bool update_all) {
    auto& board = Board::GetInstance();
    auto codec = board.GetAudioCodec();
    int level = 0;
    bool charging = false, discharging = false;
    const bool has_battery = board.GetBatteryLevel(level, charging, discharging);
    const char* battery = "";
    if (has_battery) {
        static const char* levels[] = {
            MATERIAL_SYMBOLS_BATTERY_ANDROID_0,       MATERIAL_SYMBOLS_BATTERY_ANDROID_FRAME_1,
            MATERIAL_SYMBOLS_BATTERY_ANDROID_FRAME_2, MATERIAL_SYMBOLS_BATTERY_ANDROID_FRAME_3,
            MATERIAL_SYMBOLS_BATTERY_ANDROID_FRAME_4, MATERIAL_SYMBOLS_BATTERY_ANDROID_FRAME_5,
            MATERIAL_SYMBOLS_BATTERY_ANDROID_FRAME_6, MATERIAL_SYMBOLS_BATTERY_ANDROID_FRAME_FULL,
        };
        battery = charging ? MATERIAL_SYMBOLS_BATTERY_ANDROID_FRAME_BOLT
                           : levels[std::clamp(level * 7 / 100, 0, 7)];
    }
    const char* network = board.GetNetworkStateIcon();
    bool sleep = false;
    {
        DisplayLockGuard lock(this);
        if (!lock || !setup_ui_called_) {
            return;
        }
        const uint32_t now = lv_tick_get();
        const bool reading =
            !hide_subtitle_ && has_caption_ &&
            now - caption_started_ < passport::SubtitleReadTimeMs(view_.SubtitleHeight(),
                                                                  view_.SubtitleViewportHeight());
        sleep = !sleeping_ && model_.ShouldSleep(now) && !reading;
        if (!sleeping_) {
            view_.SetIndicators(network, battery, codec && codec->output_volume() == 0);
        }
    }
    if (has_battery && level <= 5 && discharging && !warned_low_battery_) {
        warned_low_battery_ = true;
        SetEmotion("error");
        ShowNotification(Lang::Strings::BATTERY_LOW);
        Application::GetInstance().PlaySound(Lang::Sounds::OGG_LOW_BATTERY);
        sleep = false;
    } else if (!has_battery || level > 5 || charging) {
        warned_low_battery_ = false;
    }
    if (sleep) {
        SetPowerSaveMode(true);
    }
}
