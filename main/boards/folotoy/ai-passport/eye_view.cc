#include "eye_view.h"

#include <material_symbols.h>

#include <algorithm>
#include <cstring>

namespace passport {
namespace {
constexpr uint32_t kBackground = 0x03090f;
constexpr uint32_t kQuietText = 0x7c9aaa;

lv_obj_t* Box(lv_obj_t* parent, uint32_t color, int radius = 0) {
    auto object = lv_obj_create(parent);
    lv_obj_remove_style_all(object);
    lv_obj_set_style_bg_color(object, lv_color_hex(color), 0);
    lv_obj_set_style_bg_opa(object, LV_OPA_COVER, 0);
    lv_obj_set_style_radius(object, radius, 0);
    lv_obj_remove_flag(object, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_remove_flag(object, LV_OBJ_FLAG_CLICKABLE);
    return object;
}

lv_obj_t* Label(lv_obj_t* parent, const lv_font_t* font, uint32_t color) {
    auto label = lv_label_create(parent);
    lv_obj_set_style_text_font(label, font, 0);
    lv_obj_set_style_text_color(label, lv_color_hex(color), 0);
    lv_label_set_text(label, "");
    return label;
}

void Place(lv_obj_t* object, int cx, int cy, int width, int height) {
    lv_obj_set_size(object, width, height);
    lv_obj_set_pos(object, cx - width / 2, cy - height / 2);
}
}  // namespace

void EyeView::Create(lv_obj_t* screen, int width, int height, const lv_font_t* text_font,
                     const lv_font_t* icon_font) {
    width_ = width;
    eye_y_ = height * 46 / 100;
    root_ = Box(screen, kBackground);
    lv_obj_set_size(root_, width, height);
    lv_obj_set_pos(root_, 0, 0);

    network_ = Label(root_, icon_font, kQuietText);
    lv_obj_set_pos(network_, 16, 12);
    battery_ = Label(root_, icon_font, kQuietText);
    lv_obj_set_size(battery_, 30, 24);
    lv_obj_set_pos(battery_, width - 42, 12);
    mute_ = Label(root_, icon_font, kQuietText);
    lv_obj_set_pos(mute_, width - 72, 12);

    status_ = Label(root_, text_font, kQuietText);
    lv_obj_set_width(status_, width - 32);
    lv_obj_set_style_text_align(status_, LV_TEXT_ALIGN_CENTER, 0);
    lv_label_set_long_mode(status_, LV_LABEL_LONG_SCROLL_CIRCULAR);
    lv_obj_set_pos(status_, 16, 44);

    // Layered rounded shapes create a glow without a framebuffer or blur cache.
    for (auto& eye : eyes_) {
        eye.halo[0] = Box(root_, 0x102d3d, LV_RADIUS_CIRCLE);
        eye.halo[1] = Box(root_, 0x23596b, LV_RADIUS_CIRCLE);
        eye.core = Box(root_, 0x7ae9ff, LV_RADIUS_CIRCLE);
        eye.shine = Box(root_, 0xe5fbff, LV_RADIUS_CIRCLE);
        eye.smile_mask = Box(root_, kBackground, LV_RADIUS_CIRCLE);
        lv_obj_add_flag(eye.smile_mask, LV_OBJ_FLAG_HIDDEN);
    }
    for (int i = 0; i < 3; ++i) {
        dots_[i] = Box(root_, 0x2a7189, LV_RADIUS_CIRCLE);
        Place(dots_[i], width / 2 + (i - 1) * 14, height * 68 / 100, 4, 4);
    }

    auto divider = Box(root_, 0x142632);
    lv_obj_set_size(divider, width - 56, 1);
    lv_obj_set_pos(divider, 28, height - 79);

    caption_box_ = Box(root_, kBackground);
    caption_height_ = 60;
    lv_obj_set_size(caption_box_, width - 36, caption_height_);
    lv_obj_set_pos(caption_box_, 18, height - 68);
    caption_ = Label(caption_box_, text_font, 0xc4d8e2);
    lv_label_set_long_mode(caption_, LV_LABEL_LONG_WRAP);
    lv_obj_set_width(caption_, width - 36);
    lv_obj_set_height(caption_, LV_SIZE_CONTENT);
    lv_obj_set_style_text_align(caption_, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_set_style_text_line_space(caption_, 5, 0);
}

void EyeView::Destroy() {
    if (root_ != nullptr) {
        lv_obj_delete(root_);
        root_ = nullptr;
    }
}

void EyeView::SetFonts(const lv_font_t* text_font, const lv_font_t* icon_font) {
    for (auto label : {status_, caption_}) {
        lv_obj_set_style_text_font(label, text_font, 0);
    }
    for (auto label : {network_, battery_, mute_}) {
        lv_obj_set_style_text_font(label, icon_font, 0);
    }
    lv_obj_update_layout(caption_);
}

void EyeView::SetStatus(const char* text) {
    const char* next = text ? text : "";
    if (std::strcmp(lv_label_get_text(status_), next) != 0) {
        lv_label_set_text(status_, next);
    }
}

void EyeView::SetSubtitle(const char* text) {
    lv_label_set_text(caption_, text ? text : "");
    lv_obj_set_y(caption_, 0);
    lv_obj_update_layout(caption_);
}

void EyeView::SetSubtitleHidden(bool hidden) {
    if (hidden) {
        lv_obj_add_flag(caption_box_, LV_OBJ_FLAG_HIDDEN);
    } else {
        lv_obj_remove_flag(caption_box_, LV_OBJ_FLAG_HIDDEN);
    }
}

void EyeView::SetIndicators(const char* network, const char* battery, bool muted) {
    const char* values[] = {network ? network : "", battery ? battery : "",
                            muted ? MATERIAL_SYMBOLS_VOLUME_OFF : ""};
    lv_obj_t* labels[] = {network_, battery_, mute_};
    for (int i = 0; i < 3; ++i) {
        if (std::strcmp(lv_label_get_text(labels[i]), values[i]) != 0) {
            lv_label_set_text(labels[i], values[i]);
        }
    }
}

void EyeView::RenderEye(EyeObjects& objects, const EyeShape& shape, int center_x,
                        const EyeFrame& frame) {
    const int x = center_x + shape.dx;
    const int y = eye_y_ + shape.dy;
    auto color = lv_color_hex(frame.color);
    auto background = lv_color_hex(kBackground);
    for (int layer = 0; layer < 2; ++layer) {
        const int spread = layer == 0 ? 12 : 6;
        Place(objects.halo[layer], x, y, shape.width + spread, shape.height + spread);
        lv_obj_set_style_bg_color(objects.halo[layer],
                                  lv_color_mix(color, background, layer == 0 ? 22 : 55), 0);
        lv_obj_set_style_opa(objects.halo[layer], 150 + frame.glow, 0);
    }
    Place(objects.core, x, y, shape.width, shape.height);
    lv_obj_set_style_bg_color(objects.core, color, 0);
    lv_obj_set_style_radius(objects.core, std::min(shape.width, shape.height) / 2, 0);
    Place(objects.shine, x - shape.width / 5, y - shape.height / 5, std::max(3, shape.width / 8),
          std::max(2, shape.height / 3));
    lv_obj_set_style_opa(objects.shine, 90, 0);
    if (frame.smile) {
        // An offset, dark ellipse leaves the upper arch of a smiling eye.
        Place(objects.smile_mask, x, y + 13, shape.width + 14, shape.height);
        lv_obj_remove_flag(objects.smile_mask, LV_OBJ_FLAG_HIDDEN);
        lv_obj_add_flag(objects.shine, LV_OBJ_FLAG_HIDDEN);
    } else {
        lv_obj_add_flag(objects.smile_mask, LV_OBJ_FLAG_HIDDEN);
        lv_obj_remove_flag(objects.shine, LV_OBJ_FLAG_HIDDEN);
    }
}

void EyeView::Render(const EyeFrame& frame, EyeMood mood, uint32_t now) {
    RenderEye(eyes_[0], frame.left, width_ / 2 - 46, frame);
    RenderEye(eyes_[1], frame.right, width_ / 2 + 46, frame);
    for (int i = 0; i < 3; ++i) {
        const bool active = mood == EyeMood::Thinking || mood == EyeMood::Speaking;
        const int pulse = static_cast<int>((now / 240) % 3) == i ? 7 : 4;
        lv_obj_set_size(dots_[i], active ? pulse : 3, active ? pulse : 3);
        lv_obj_set_style_bg_color(dots_[i], lv_color_hex(frame.color), 0);
        lv_obj_set_style_opa(dots_[i], active ? 170 : 45, 0);
    }
}

int EyeView::SubtitleHeight() const { return lv_obj_get_height(caption_); }

void EyeView::ScrollSubtitle(int offset) { lv_obj_set_y(caption_, -offset); }

}  // namespace passport
