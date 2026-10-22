#include "eye_model.h"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <limits>

namespace passport {
namespace {

constexpr uint32_t kEmotionDurationMs = 3000;
constexpr uint32_t kSubtitleDwellMs = 1800;
constexpr uint32_t kSubtitlePixelMs = 50;
constexpr uint32_t kCyan = 0x69E7FF;
constexpr uint32_t kMint = 0x8EF0CC;
constexpr uint32_t kAmber = 0xFFD19A;
constexpr uint32_t kCoral = 0xFF9C94;

int Wave(uint32_t elapsed, uint32_t period, int amplitude) {
    constexpr float kTau = 6.28318530718f;
    const float angle = static_cast<float>(elapsed % period) * kTau / period;
    return static_cast<int>(std::lround(std::sin(angle) * amplitude));
}

EyeMood StateMood(DeviceState state) {
    switch (state) {
        case kDeviceStateListening:
        case kDeviceStateAudioTesting:
            return EyeMood::Listening;
        case kDeviceStateSpeaking:
        case kDeviceStateNotifying:
            return EyeMood::Speaking;
        case kDeviceStateStarting:
        case kDeviceStateConnecting:
        case kDeviceStateUpgrading:
        case kDeviceStateActivating:
            return EyeMood::Thinking;
        case kDeviceStateFatalError:
            return EyeMood::Error;
        default:
            return EyeMood::Idle;
    }
}

bool IsOneOf(const char* value, const char* const* choices, size_t count) {
    for (size_t i = 0; i < count; ++i) {
        if (std::strcmp(value, choices[i]) == 0) {
            return true;
        }
    }
    return false;
}

void Blink(EyeFrame& frame, uint32_t elapsed) {
    const uint32_t blink = elapsed % 6200;
    if (blink < 5000 || blink >= 5180) {
        return;
    }
    const int progress = static_cast<int>(blink - 5000);
    const int opening = std::abs(progress - 90);
    frame.left.height = 6 + (frame.left.height - 6) * opening / 90;
    frame.right.height = 6 + (frame.right.height - 6) * opening / 90;
}

uint64_t SubtitleTravelMs(int text_height, int viewport_height) {
    if (text_height <= 0 || viewport_height <= 0 || text_height <= viewport_height) {
        return 0;
    }
    return static_cast<uint64_t>(text_height - viewport_height) * kSubtitlePixelMs;
}

}  // namespace

void EyeModel::SetDeviceState(DeviceState state, uint32_t now) {
    // Display status refreshes are not user activity and must not postpone sleep.
    if (state == state_) {
        return;
    }
    state_ = state;
    phase_ = StateMood(state);
    state_since_ms_ = now;
    Touch(now);
    // An alert is often followed immediately by Idle. Let it remain visible.
    // LLM emotion can arrive before TTS starts. Keep that expression while the
    // reply begins; a fresh connection/listening turn supersedes the old one.
    if (state != kDeviceStateIdle && state != kDeviceStateSpeaking &&
        state != kDeviceStateNotifying) {
        has_emotion_ = false;
    }
}

void EyeModel::SetEmotion(const char* emotion, uint32_t now) {
    if (emotion == nullptr) {
        return;
    }
    static constexpr const char* kHappy[] = {"happy", "laughing",  "funny",   "loving",
                                             "kissy", "confident", "winking", "excited",
                                             "cool",  "relaxed"};
    static constexpr const char* kError[] = {"error",     "sad",        "angry",   "crying",
                                             "shocked",   "confused",   "worried", "cancel",
                                             "cloud_off", "embarrassed"};
    if (IsOneOf(emotion, kHappy, sizeof(kHappy) / sizeof(kHappy[0]))) {
        emotion_ = EyeMood::Happy;
    } else if (IsOneOf(emotion, kError, sizeof(kError) / sizeof(kError[0]))) {
        emotion_ = EyeMood::Error;
    } else {
        // In particular, neutral must not erase an STT-derived Thinking phase.
        return;
    }
    has_emotion_ = true;
    emotion_since_ms_ = now;
    Touch(now);
}

void EyeModel::OnMessage(const char* role, uint32_t now) {
    if (role == nullptr) {
        return;
    }
    Touch(now);
    if (state_ == kDeviceStateFatalError) {
        return;
    }
    if (std::strcmp(role, "user") == 0) {
        phase_ = EyeMood::Thinking;
        state_since_ms_ = now;
        has_emotion_ = false;
    } else if (std::strcmp(role, "assistant") == 0) {
        phase_ = EyeMood::Speaking;
        state_since_ms_ = now;
    }
}

void EyeModel::Touch(uint32_t now) {
    last_activity_ms_ = now;
    sleeping_ = false;
}

void EyeModel::SetSleeping(bool sleeping, uint32_t now) {
    if (sleeping) {
        // A live conversation or setup operation must never be blanked.
        sleeping_ = state_ == kDeviceStateIdle;
    } else {
        Touch(now);
    }
}

EyeMood EyeModel::mood(uint32_t now) const {
    if (sleeping_) {
        return EyeMood::Sleeping;
    }
    if (state_ == kDeviceStateFatalError) {
        return EyeMood::Error;
    }
    if (has_emotion_ && now - emotion_since_ms_ < kEmotionDurationMs) {
        return emotion_;
    }
    return phase_;
}

EyeFrame EyeModel::Frame(uint32_t now) const {
    const uint32_t elapsed = now - state_since_ms_;
    EyeFrame frame = {{60, 84, 0, 0}, {60, 84, 0, 0}, kCyan, 64, false};
    switch (mood(now)) {
        case EyeMood::Idle:
            frame.left.height = frame.right.height = 82 + Wave(elapsed, 3600, 3);
            frame.left.dx = frame.right.dx = Wave(elapsed, 11000, 6);
            frame.left.dy = frame.right.dy = Wave(elapsed, 8400, 3);
            frame.glow = 55 + Wave(elapsed, 3600, 10);
            Blink(frame, elapsed);
            break;
        case EyeMood::Listening:
            frame.left.height = frame.right.height = 88 + Wave(elapsed, 1600, 5);
            frame.glow = 76 + Wave(elapsed, 1600, 14);
            Blink(frame, elapsed);
            break;
        case EyeMood::Thinking:
            frame.left = {58, 65 + Wave(elapsed, 2400, 4), 7, -5};
            frame.right = {58, 80 + Wave(elapsed, 2400, 4), 7, -5};
            frame.color = kAmber;
            frame.glow = 65 + Wave(elapsed, 2400, 15);
            break;
        case EyeMood::Speaking:
            frame.left.height = 74 + Wave(elapsed, 530, 12);
            frame.right.height = 78 + Wave(elapsed + 80, 530, 12);
            frame.glow = 73 + Wave(elapsed, 530, 17);
            break;
        case EyeMood::Happy:
            frame.left = {64, 42 + Wave(elapsed, 1200, 3), 0, -4};
            frame.right = frame.left;
            frame.color = kMint;
            frame.glow = 82 + Wave(elapsed, 1200, 10);
            frame.smile = true;
            break;
        case EyeMood::Error:
            frame.left = {58, 58 + Wave(elapsed, 2800, 2), -3, 5};
            frame.right = {58, 74 + Wave(elapsed, 2800, 2), -3, -3};
            frame.color = kCoral;
            frame.glow = 60 + Wave(elapsed, 2800, 12);
            break;
        case EyeMood::Sleeping:
            frame.left = frame.right = {56, 6, 0, 0};
            frame.glow = 0;
            break;
    }
    return frame;
}

bool EyeModel::ShouldSleep(uint32_t now) const {
    return state_ == kDeviceStateIdle && now - last_activity_ms_ >= kIdleTimeoutMs;
}

int SubtitleScrollOffset(uint32_t elapsed_ms, int text_height, int viewport_height) {
    const uint64_t travel = SubtitleTravelMs(text_height, viewport_height);
    if (travel == 0) {
        return 0;
    }
    const uint64_t cycle = travel + 2 * kSubtitleDwellMs;
    const uint64_t position = elapsed_ms % cycle;
    if (position <= kSubtitleDwellMs) {
        return 0;
    }
    return static_cast<int>(std::min(position - kSubtitleDwellMs, travel) / kSubtitlePixelMs);
}

uint32_t SubtitleReadTimeMs(int text_height, int viewport_height) {
    const uint64_t travel = SubtitleTravelMs(text_height, viewport_height);
    const uint64_t duration = travel == 0 ? kSubtitleDwellMs : travel + 2 * kSubtitleDwellMs;
    return static_cast<uint32_t>(
        std::min(duration, static_cast<uint64_t>(std::numeric_limits<uint32_t>::max())));
}

}  // namespace passport
