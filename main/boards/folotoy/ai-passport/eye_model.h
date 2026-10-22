#ifndef PASSPORT_EYE_MODEL_H_
#define PASSPORT_EYE_MODEL_H_

#include <cstdint>

#include "device_state.h"

namespace passport {

enum class EyeMood { Idle, Listening, Thinking, Speaking, Happy, Error, Sleeping };

struct EyeShape {
    int width;
    int height;
    int dx;
    int dy;
};

struct EyeFrame {
    EyeShape left;
    EyeShape right;
    uint32_t color;
    int glow;
    bool smile;
};

// Pure animation/state logic. All timestamps use the same wrapping millisecond clock.
class EyeModel {
public:
    static constexpr uint32_t kIdleTimeoutMs = 60000;

    void SetDeviceState(DeviceState state, uint32_t now);
    void SetEmotion(const char* emotion, uint32_t now);
    void OnMessage(const char* role, uint32_t now);
    void Touch(uint32_t now);
    void SetSleeping(bool sleeping, uint32_t now);
    EyeMood mood(uint32_t now) const;
    EyeFrame Frame(uint32_t now) const;
    bool ShouldSleep(uint32_t now) const;

private:
    DeviceState state_ = kDeviceStateUnknown;
    EyeMood phase_ = EyeMood::Idle;
    EyeMood emotion_ = EyeMood::Idle;
    uint32_t state_since_ms_ = 0;
    uint32_t emotion_since_ms_ = 0;
    uint32_t last_activity_ms_ = 0;
    bool has_emotion_ = false;
    bool sleeping_ = false;
};

// Long captions scroll vertically at 20 px/s, with time to read both ends.
int SubtitleScrollOffset(uint32_t elapsed_ms, int text_height, int viewport_height);
uint32_t SubtitleReadTimeMs(int text_height, int viewport_height);

}  // namespace passport

#endif  // PASSPORT_EYE_MODEL_H_
