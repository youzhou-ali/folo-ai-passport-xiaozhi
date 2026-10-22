#include "eye_view.h"

#include <cassert>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <iostream>
#include <iterator>
#include <string>
#include <vector>

namespace {
constexpr int kWidth = 240;
constexpr int kHeight = 320;
std::vector<uint32_t> framebuffer(kWidth* kHeight);

// CBIN stores the firmware's 32-bit structure layout. Rebase into native host
// structures while keeping all glyph descriptors and bitmap data unchanged.
class HostCbinFont {
public:
    explicit HostCbinFont(const char* path) {
        std::ifstream file(path, std::ios::binary);
        bytes_ = std::vector<uint8_t>(std::istreambuf_iterator<char>(file), {});
        assert(bytes_.size() >= 60);
        font_.get_glyph_dsc = lv_font_get_glyph_dsc_fmt_txt;
        font_.get_glyph_bitmap = lv_font_get_bitmap_fmt_txt;
        font_.line_height = U32(12);
        font_.base_line = U32(16);
        font_.subpx = bytes_[20] & 3;
        font_.kerning = (bytes_[20] >> 2) & 1;
        font_.static_bitmap = (bytes_[20] >> 3) & 1;
        font_.underline_position = static_cast<int8_t>(bytes_[21]);
        font_.underline_thickness = bytes_[22];
        const size_t base = U32(24);
        descriptor_.glyph_bitmap = Ptr<uint8_t>(base + U32(base));
        descriptor_.glyph_dsc = Ptr<lv_font_fmt_txt_glyph_dsc_t>(base + U32(base + 4));
        descriptor_.kern_scale = U16(base + 16);
        const uint16_t flags = U16(base + 18);
        descriptor_.cmap_num = flags & 0x1ff;
        descriptor_.bpp = (flags >> 9) & 0xf;
        descriptor_.kern_classes = (flags >> 13) & 1;
        descriptor_.bitmap_format = (flags >> 14) & 3;
        descriptor_.stride = bytes_[base + 20];
        assert(descriptor_.bpp == 4 && descriptor_.bitmap_format == 0);

        const size_t cmaps_base = base + U32(base + 8);
        cmaps_.resize(descriptor_.cmap_num);
        for (size_t i = 0; i < cmaps_.size(); ++i) {
            const size_t entry = cmaps_base + i * 20;
            auto& cmap = cmaps_[i];
            cmap.range_start = U32(entry);
            cmap.range_length = U16(entry + 4);
            cmap.glyph_id_start = U16(entry + 6);
            const uint32_t unicode = U32(entry + 8);
            const uint32_t glyphs = U32(entry + 12);
            cmap.unicode_list = unicode ? Ptr<uint16_t>(cmaps_base + unicode) : nullptr;
            cmap.glyph_id_ofs_list = glyphs ? Ptr<uint8_t>(cmaps_base + glyphs) : nullptr;
            cmap.list_length = U16(entry + 16);
            cmap.type = static_cast<lv_font_fmt_txt_cmap_type_t>(bytes_[entry + 18]);
        }
        descriptor_.cmaps = cmaps_.data();
        const uint32_t kern_offset = U32(base + 12);
        if (kern_offset != 0) {
            const size_t kern_base = base + kern_offset;
            assert(descriptor_.kern_classes == 1);
            classes_.class_pair_values = Ptr<int8_t>(kern_base + U32(kern_base));
            classes_.left_class_mapping = Ptr<uint8_t>(kern_base + U32(kern_base + 4));
            classes_.right_class_mapping = Ptr<uint8_t>(kern_base + U32(kern_base + 8));
            classes_.left_class_cnt = bytes_[kern_base + 12];
            classes_.right_class_cnt = bytes_[kern_base + 13];
            descriptor_.kern_dsc = &classes_;
        }
        font_.dsc = &descriptor_;
    }

    const lv_font_t* get() const { return &font_; }

    void VerifyGlyphs(const char* text) const {
        const auto* cursor = reinterpret_cast<const uint8_t*>(text);
        while (*cursor) {
            uint32_t codepoint = *cursor++;
            int continuation = 0;
            if (codepoint >= 0xf0) {
                continuation = 3;
                codepoint &= 7;
            } else if (codepoint >= 0xe0) {
                continuation = 2;
                codepoint &= 15;
            } else if (codepoint >= 0xc0) {
                continuation = 1;
                codepoint &= 31;
            }
            for (int i = 0; i < continuation; ++i) {
                assert((*cursor & 0xc0) == 0x80);
                codepoint = (codepoint << 6) | (*cursor++ & 0x3f);
            }
            if (codepoint == '\n') {
                continue;
            }
            lv_font_glyph_dsc_t glyph{};
            if (!lv_font_get_glyph_dsc(&font_, &glyph, codepoint, 0) || glyph.is_placeholder) {
                std::cerr << "Missing subtitle glyph U+" << std::hex << codepoint << '\n';
                std::abort();
            }
        }
    }

private:
    uint32_t U32(size_t offset) const {
        assert(offset + 4 <= bytes_.size());
        uint32_t result;
        std::memcpy(&result, bytes_.data() + offset, 4);
        return result;
    }
    uint16_t U16(size_t offset) const {
        assert(offset + 2 <= bytes_.size());
        uint16_t result;
        std::memcpy(&result, bytes_.data() + offset, 2);
        return result;
    }
    template <typename T>
    const T* Ptr(size_t offset) const {
        assert(offset + sizeof(T) <= bytes_.size());
        return reinterpret_cast<const T*>(bytes_.data() + offset);
    }
    std::vector<uint8_t> bytes_;
    std::vector<lv_font_fmt_txt_cmap_t> cmaps_;
    lv_font_fmt_txt_kern_classes_t classes_{};
    lv_font_fmt_txt_dsc_t descriptor_{};
    lv_font_t font_{};
};

void Flush(lv_display_t* display, const lv_area_t* area, uint8_t* pixels) {
    const auto* source = reinterpret_cast<const uint32_t*>(pixels);
    for (int y = area->y1; y <= area->y2; ++y) {
        for (int x = area->x1; x <= area->x2; ++x) {
            framebuffer[y * kWidth + x] = *source++;
        }
    }
    lv_display_flush_ready(display);
}

void WriteFrame(const std::string& directory, const std::string& name) {
    lv_obj_update_layout(lv_screen_active());
    lv_refr_now(nullptr);
    std::ofstream file(directory + "/" + name + ".ppm", std::ios::binary);
    file << "P6\n" << kWidth << ' ' << kHeight << "\n255\n";
    for (uint32_t pixel : framebuffer) {
        const char rgb[] = {static_cast<char>((pixel >> 16) & 255),
                            static_cast<char>((pixel >> 8) & 255), static_cast<char>(pixel & 255)};
        file.write(rgb, 3);
    }
    assert(file.good());
}

}  // namespace

int main(int argc, char** argv) {
    assert(argc == 3 || argc == 4);
    lv_init();
    auto display = lv_display_create(kWidth, kHeight);
    assert(display != nullptr);
    lv_display_set_color_format(display, LV_COLOR_FORMAT_XRGB8888);
    std::vector<uint32_t> draw_buffer(kWidth * kHeight);
    lv_display_set_buffers(display, draw_buffer.data(), nullptr,
                           static_cast<uint32_t>(draw_buffer.size() * 4),
                           LV_DISPLAY_RENDER_MODE_PARTIAL);
    lv_display_set_flush_cb(display, Flush);
    HostCbinFont font(argv[1]);
    constexpr const char* long_text =
        "今天我们一起去看星星吧。你说的话我都听见了，也会认真思考，再把答案慢慢告诉你。";
    font.VerifyGlyphs("按 OK 开始对话思考中我在听请稍等正在回答真开心出现异常");
    font.VerifyGlyphs(long_text);
    lv_font_glyph_dsc_t missing{};
    assert(!lv_font_get_glyph_dsc(font.get(), &missing, 0x10ffff, 0) || missing.is_placeholder);

    passport::EyeView view;
    view.Create(lv_screen_active(), kWidth, kHeight, font.get(), font.get());
    view.SetIndicators("", "", false);
    struct Scene {
        const char* name;
        DeviceState state;
        const char* emotion;
        const char* status;
        const char* subtitle;
    };
    const Scene scenes[] = {
        {"idle", kDeviceStateIdle, "neutral", "随身小智", "按 OK 开始对话"},
        {"listening", kDeviceStateListening, "neutral", "我在听", "今天我们一起去看星星吧"},
        {"thinking", kDeviceStateConnecting, "neutral", "思考中", "请稍等，我正在想"},
        {"speaking", kDeviceStateSpeaking, "neutral", "正在回答", "你说的话我都听见了"},
        {"happy", kDeviceStateSpeaking, "happy", "真开心", "能陪着你，我很开心"},
        {"error", kDeviceStateIdle, "error", "出现异常", "连接断开了，请稍后再试"},
    };
    for (const auto& scene : scenes) {
        font.VerifyGlyphs(scene.status);
        font.VerifyGlyphs(scene.subtitle);
        passport::EyeModel model;
        model.SetDeviceState(scene.state, 0);
        model.SetEmotion(scene.emotion, 0);
        view.SetStatus(scene.status);
        view.SetSubtitle(scene.subtitle);
        view.Render(model.Frame(400), model.mood(400), 400);
        WriteFrame(argv[2], scene.name);
    }

    passport::EyeModel model;
    model.SetDeviceState(kDeviceStateSpeaking, 0);
    view.SetStatus("正在回答");
    view.SetSubtitle(long_text);
    view.Render(model.Frame(400), model.mood(400), 400);
    const int text_height = view.SubtitleHeight();
    const int viewport_height = view.SubtitleViewportHeight();
    assert(text_height > viewport_height);
    const int offset = passport::SubtitleScrollOffset(5000, text_height, viewport_height);
    assert(offset > 0);
    WriteFrame(argv[2], "subtitle-start");
    const auto before = framebuffer;
    view.ScrollSubtitle(offset);
    WriteFrame(argv[2], "subtitle-scrolled");
    size_t caption_changed = 0;
    size_t outside_changed = 0;
    for (int y = 0; y < kHeight; ++y) {
        for (int x = 0; x < kWidth; ++x) {
            if (before[y * kWidth + x] == framebuffer[y * kWidth + x]) {
                continue;
            }
            if (y >= kHeight - 68 && y < kHeight - 8 && x >= 18 && x < kWidth - 18) {
                ++caption_changed;
            } else {
                ++outside_changed;
            }
        }
    }
    assert(caption_changed > 100);
    assert(outside_changed == 0);
    std::cout << "Chinese glyphs: PASS; subtitle " << text_height << " px in " << viewport_height
              << " px viewport; offset " << offset << " px; changed caption pixels "
              << caption_changed << "; pixels outside viewport " << outside_changed << '\n';

    if (argc == 4) {
        // One real conversation timeline, sampled at 10 fps. Start near the
        // idle blink so this short preview also shows the model's full blink.
        passport::EyeModel animated;
        animated.SetDeviceState(kDeviceStateIdle, 0);
        for (size_t scene_index = 0; scene_index < sizeof(scenes) / sizeof(scenes[0]);
             ++scene_index) {
            const auto& scene = scenes[scene_index];
            const uint32_t started = 4290 + static_cast<uint32_t>(scene_index) * 2000;
            if (scene_index == 2) {
                animated.OnMessage("user", started);  // STT ends while still Listening.
            } else {
                animated.SetDeviceState(scene.state, started);
            }
            animated.SetEmotion(scene.emotion, started);
            view.SetStatus(scene.status);
            view.SetSubtitle(scene.subtitle);
            for (int frame = 0; frame < 20; ++frame) {
                const uint32_t elapsed = frame * 100;
                const uint32_t now = started + elapsed;
                lv_tick_inc(100);
                view.Render(animated.Frame(now), animated.mood(now), now);
                view.ScrollSubtitle(passport::SubtitleScrollOffset(elapsed, view.SubtitleHeight(),
                                                                   view.SubtitleViewportHeight()));
                char name[32];
                std::snprintf(name, sizeof(name), "frame-%03u",
                              static_cast<unsigned>(scene_index * 20 + frame));
                WriteFrame(argv[3], name);
            }
        }
    }
    view.Destroy();
    lv_display_delete(display);
    lv_deinit();
}
