#pragma once

#include <lvgl.h>

#include "eye_model.h"

namespace passport {

// LVGL-only scene, shared by the device and the host rendering check.
class EyeView {
public:
    void Create(lv_obj_t* screen, int width, int height, const lv_font_t* text_font,
                const lv_font_t* icon_font);
    void Destroy();
    void SetFonts(const lv_font_t* text_font, const lv_font_t* icon_font);
    void SetStatus(const char* text);
    void SetSubtitle(const char* text);
    void SetSubtitleHidden(bool hidden);
    void SetIndicators(const char* network, const char* battery, bool muted);
    void Render(const EyeFrame& frame, EyeMood mood, uint32_t now);
    void ScrollSubtitle(int offset);
    int SubtitleHeight() const;
    int SubtitleViewportHeight() const { return caption_height_; }

private:
    struct EyeObjects {
        lv_obj_t* halo[2]{};
        lv_obj_t* core = nullptr;
        lv_obj_t* shine = nullptr;
        lv_obj_t* smile_mask = nullptr;
    };
    lv_obj_t* root_ = nullptr;
    lv_obj_t* status_ = nullptr;
    lv_obj_t* network_ = nullptr;
    lv_obj_t* battery_ = nullptr;
    lv_obj_t* mute_ = nullptr;
    lv_obj_t* caption_box_ = nullptr;
    lv_obj_t* caption_ = nullptr;
    lv_obj_t* dots_[3]{};
    EyeObjects eyes_[2]{};
    int width_ = 240;
    int eye_y_ = 148;
    int caption_height_ = 60;

    void RenderEye(EyeObjects& objects, const EyeShape& shape, int center_x, const EyeFrame& frame);
};

}  // namespace passport
