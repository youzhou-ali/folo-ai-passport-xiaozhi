#include "eye_model.h"

#include <cassert>
#include <climits>
#include <cstdint>
#include <cstring>
#include <initializer_list>

using passport::EyeFrame;
using passport::EyeModel;
using passport::EyeMood;
using passport::SubtitleReadTimeMs;
using passport::SubtitleScrollOffset;

namespace {

void IdleAndWake() {
    EyeModel model;
    assert(!model.ShouldSleep(100000));  // Startup is not idle.
    model.SetDeviceState(kDeviceStateIdle, 100);
    assert(model.mood(100) == EyeMood::Idle);
    assert(!model.ShouldSleep(60099));
    // A periodic status update must not keep the display awake forever.
    model.SetDeviceState(kDeviceStateIdle, 59000);
    assert(model.ShouldSleep(60100));
    model.SetSleeping(true, 60100);
    assert(model.mood(60100) == EyeMood::Sleeping);
    model.SetDeviceState(kDeviceStateIdle, 60110);
    assert(model.mood(60110) == EyeMood::Sleeping);
    model.Touch(60500);
    assert(model.mood(60500) == EyeMood::Idle);
    assert(!model.ShouldSleep(120499));
    assert(model.ShouldSleep(120500));
    model.SetSleeping(true, 120500);
    model.SetSleeping(false, 121000);
    assert(model.mood(121000) == EyeMood::Idle);
    assert(!model.ShouldSleep(121001));
}

void ActiveStatesStayAwake() {
    for (DeviceState state :
         {kDeviceStateUnknown, kDeviceStateStarting, kDeviceStateWifiConfiguring,
          kDeviceStateConnecting, kDeviceStateListening, kDeviceStateSpeaking,
          kDeviceStateNotifying, kDeviceStateUpgrading, kDeviceStateActivating,
          kDeviceStateAudioTesting, kDeviceStateFatalError}) {
        EyeModel model;
        model.SetDeviceState(state, 100);
        assert(!model.ShouldSleep(100 + 10 * EyeModel::kIdleTimeoutMs));
        model.SetSleeping(true, 100000);
        assert(model.mood(100000) != EyeMood::Sleeping);
    }
}

void ConversationTimeline() {
    EyeModel model;
    model.SetDeviceState(kDeviceStateIdle, 0);
    model.SetDeviceState(kDeviceStateConnecting, 100);
    assert(model.mood(100) == EyeMood::Thinking);
    model.SetDeviceState(kDeviceStateListening, 200);
    assert(model.mood(200) == EyeMood::Listening);
    model.OnMessage("user", 300);
    assert(model.mood(300) == EyeMood::Thinking);
    model.SetEmotion("neutral", 301);
    model.SetDeviceState(kDeviceStateListening, 302);
    assert(model.mood(302) == EyeMood::Thinking);
    model.OnMessage("assistant", 400);
    assert(model.mood(400) == EyeMood::Speaking);
    model.SetDeviceState(kDeviceStateSpeaking, 401);
    assert(model.mood(401) == EyeMood::Speaking);
    model.SetDeviceState(kDeviceStateListening, 500);
    assert(model.mood(500) == EyeMood::Listening);
    model.SetDeviceState(kDeviceStateIdle, 600);
    assert(model.mood(600) == EyeMood::Idle);
    model.OnMessage(nullptr, 610);
    model.SetEmotion(nullptr, 610);
    assert(model.mood(610) == EyeMood::Idle);
}

void EmotionsAndErrors() {
    EyeModel model;
    model.SetDeviceState(kDeviceStateListening, 0);
    model.SetEmotion("laughing", 100);
    assert(model.mood(100) == EyeMood::Happy);
    // The server commonly sends LLM emotion before the TTS start event.
    model.SetDeviceState(kDeviceStateSpeaking, 101);
    assert(model.mood(101) == EyeMood::Happy);
    model.SetEmotion("neutral", 200);
    assert(model.mood(3099) == EyeMood::Happy);
    assert(model.mood(3100) == EyeMood::Speaking);
    model.OnMessage("user", 3200);
    model.SetEmotion("loving", 3300);
    assert(model.mood(3300) == EyeMood::Happy);
    assert(model.mood(6300) == EyeMood::Thinking);
    model.SetEmotion("cloud_off", 6400);
    model.SetDeviceState(kDeviceStateIdle, 6401);
    model.SetEmotion("neutral", 6402);
    assert(model.mood(6402) == EyeMood::Error);
    assert(model.mood(9399) == EyeMood::Error);
    assert(model.mood(9400) == EyeMood::Idle);
    // Starting a new interaction supersedes the old transient alert.
    model.SetEmotion("confused", 9500);
    model.SetDeviceState(kDeviceStateListening, 9501);
    assert(model.mood(9501) == EyeMood::Listening);
    model.SetDeviceState(kDeviceStateFatalError, 9600);
    model.SetEmotion("happy", 9601);
    model.OnMessage("user", 9602);
    assert(model.mood(9602) == EyeMood::Error);
    assert(model.mood(100000) == EyeMood::Error);
}

void ClockRollover() {
    constexpr uint32_t start = UINT32_MAX - 999;
    EyeModel model;
    model.SetDeviceState(kDeviceStateIdle, start);
    assert(!model.ShouldSleep(start + EyeModel::kIdleTimeoutMs - 1));
    assert(model.ShouldSleep(start + EyeModel::kIdleTimeoutMs));
    model.SetEmotion("happy", start);
    assert(model.mood(start + 2999) == EyeMood::Happy);
    assert(model.mood(start + 3000) == EyeMood::Idle);
    model.Touch(start + 1000);
    assert(!model.ShouldSleep(start + 1000 + EyeModel::kIdleTimeoutMs - 1));
}

bool Different(const EyeFrame& first, const EyeFrame& second) {
    return first.left.width != second.left.width || first.left.height != second.left.height ||
           first.left.dx != second.left.dx || first.left.dy != second.left.dy ||
           first.right.height != second.right.height || first.glow != second.glow;
}

void CheckFrame(const EyeFrame& frame) {
    for (const auto& eye : {frame.left, frame.right}) {
        assert(eye.width >= 50 && eye.width <= 70);
        assert(eye.height >= 6 && eye.height <= 100);
        assert(eye.dx >= -8 && eye.dx <= 8);
        assert(eye.dy >= -6 && eye.dy <= 6);
    }
    assert(frame.glow >= 0 && frame.glow <= 100);
    assert(frame.color <= 0xFFFFFF);
}

void AnimatedPoses() {
    for (DeviceState state : {kDeviceStateIdle, kDeviceStateListening, kDeviceStateConnecting,
                              kDeviceStateSpeaking, kDeviceStateFatalError}) {
        EyeModel model;
        model.SetDeviceState(state, 100);
        const EyeFrame first = model.Frame(100);
        bool animated = false;
        for (uint32_t elapsed = 0; elapsed <= 20000; elapsed += 17) {
            const EyeFrame frame = model.Frame(100 + elapsed);
            CheckFrame(frame);
            animated = animated || Different(first, frame);
        }
        assert(animated);
    }
    EyeModel model;
    model.SetDeviceState(kDeviceStateIdle, 100);
    assert(model.Frame(5190).left.height == 6);  // Natural idle blink.
    model.SetEmotion("happy", 6000);
    assert(model.Frame(6000).smile);
    CheckFrame(model.Frame(6600));
    assert(!model.Frame(9000).smile);
    model.SetSleeping(true, 9000);
    assert(model.Frame(9000).left.height == 6);
    assert(model.Frame(9000).glow == 0);
    CheckFrame(model.Frame(UINT32_MAX));
}

void SubtitleTiming() {
    for (uint32_t elapsed : {0u, 1800u, 5000u, UINT32_MAX}) {
        assert(SubtitleScrollOffset(elapsed, 80, 80) == 0);
        assert(SubtitleScrollOffset(elapsed, 40, 80) == 0);
        assert(SubtitleScrollOffset(elapsed, -1, 80) == 0);
        assert(SubtitleScrollOffset(elapsed, 200, 0) == 0);
    }
    assert(SubtitleReadTimeMs(40, 80) == 1800);
    // 220 px overflow takes 11 s, with a 1.8 s pause at each end.
    constexpr uint32_t duration = 14600;
    assert(SubtitleReadTimeMs(300, 80) == duration);
    assert(SubtitleScrollOffset(0, 300, 80) == 0);
    assert(SubtitleScrollOffset(1799, 300, 80) == 0);
    assert(SubtitleScrollOffset(1800, 300, 80) == 0);
    assert(SubtitleScrollOffset(1850, 300, 80) == 1);
    assert(SubtitleScrollOffset(6800, 300, 80) == 100);
    assert(SubtitleScrollOffset(12800, 300, 80) == 220);
    assert(SubtitleScrollOffset(duration - 1, 300, 80) == 220);
    assert(SubtitleScrollOffset(duration, 300, 80) == 0);
    assert(SubtitleScrollOffset(duration + 1850, 300, 80) == 1);
    assert(SubtitleReadTimeMs(INT_MAX, 1) == UINT32_MAX);
    assert(SubtitleScrollOffset(UINT32_MAX, INT_MAX, 1) > 0);
}

}  // namespace

int main(int argc, char** argv) {
    assert(argc == 2);
    if (std::strcmp(argv[1], "idle") == 0) {
        IdleAndWake();
    } else if (std::strcmp(argv[1], "active") == 0) {
        ActiveStatesStayAwake();
    } else if (std::strcmp(argv[1], "timeline") == 0) {
        ConversationTimeline();
    } else if (std::strcmp(argv[1], "emotions") == 0) {
        EmotionsAndErrors();
    } else if (std::strcmp(argv[1], "rollover") == 0) {
        ClockRollover();
    } else if (std::strcmp(argv[1], "animation") == 0) {
        AnimatedPoses();
    } else if (std::strcmp(argv[1], "subtitles") == 0) {
        SubtitleTiming();
    } else {
        assert(false);
    }
}
