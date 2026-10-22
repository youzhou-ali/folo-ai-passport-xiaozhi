#pragma once

#include <string>

#include "display/lcd_display.h"
#include "eye_model.h"
#include "eye_view.h"

class PassportEyeDisplay : public SpiLcdDisplay {
public:
    using SpiLcdDisplay::ShowNotification;
    using SpiLcdDisplay::SpiLcdDisplay;
    ~PassportEyeDisplay() override;

    void SetupUI() override;
    void OnDeviceStateChanged(DeviceState state) override;
    void SetStatus(const char* status) override;
    void SetEmotion(const char* emotion) override;
    void SetChatMessage(const char* role, const char* content) override;
    void ClearChatMessages() override;
    void ShowNotification(const char* text, int duration_ms = 3000) override;
    void SetTheme(Theme* theme) override;
    void SetHideSubtitle(bool hide) override;
    void SetPowerSaveMode(bool on) override;
    void UpdateStatusBar(bool update_all = false) override;
    void SetPreviewImage(std::unique_ptr<LvglImage> image) override {}

    // Called on the application task by all three physical buttons.
    void OnUserActivity();

private:
    passport::EyeModel model_;
    passport::EyeView view_;
    lv_timer_t* frame_timer_ = nullptr;
    DeviceState state_ = kDeviceStateUnknown;
    std::string status_text_;
    uint32_t caption_started_ = 0;
    uint32_t notification_started_ = 0;
    uint32_t notification_duration_ = 0;
    bool has_caption_ = false;
    bool conversational_caption_ = false;
    bool sleeping_ = false;
    bool warned_low_battery_ = false;

    void RenderFrame();  // LVGL task or DisplayLockGuard required.
    void CheckFont(const lv_font_t* font);
};
