#pragma once

#include <cstdint>

// Conversation owns attention and gaze. Emotion is an independent expression,
// never an instruction to leave listening, thinking or speaking.
enum EyeAnimationState {
    EYE_STATE_IDLE,
    EYE_STATE_LISTENING,
    EYE_STATE_THINKING,
    EYE_STATE_SPEAKING,
    EYE_STATE_HAPPY,
    EYE_STATE_SAD_SLEEPY,
    EYE_STATE_ANGRY,
    EYE_STATE_SURPRISED
};

namespace JapaEyeState {

struct Conversation {
    EyeAnimationState status = EYE_STATE_IDLE;
    EyeAnimationState emotion = EYE_STATE_IDLE; // IDLE means neutral expression.

    constexpr uint32_t Pack() const {
        return static_cast<uint32_t>(status) |
               (static_cast<uint32_t>(emotion) << 8);
    }

    static constexpr Conversation Unpack(uint32_t value) {
        return {static_cast<EyeAnimationState>(value & 0xff),
                static_cast<EyeAnimationState>((value >> 8) & 0xff)};
    }
};

constexpr Conversation WithStatus(Conversation previous, EyeAnimationState status) {
    // Repeated status messages do not restart a turn or discard its expression.
    if (previous.status == status) return previous;
    // Servers can announce an expression before TTS starts. Keep it on entry to
    // speaking, but clear the previous turn on return to idle/listening/thinking.
    return {status, status == EYE_STATE_SPEAKING ? previous.emotion : EYE_STATE_IDLE};
}

constexpr Conversation WithEmotion(Conversation previous, EyeAnimationState emotion) {
    return {previous.status, emotion};
}

struct Visual {
    EyeAnimationState behavior;
    EyeAnimationState expression;
};

constexpr Visual Resolve(Conversation conversation) {
    if (conversation.status == EYE_STATE_LISTENING ||
        conversation.status == EYE_STATE_THINKING) {
        return {conversation.status, EYE_STATE_IDLE};
    }
    if (conversation.status == EYE_STATE_SPEAKING) {
        return {EYE_STATE_SPEAKING, conversation.emotion};
    }
    // Preserve the existing expressive idle profiles for deliberate UI alerts.
    return {conversation.emotion, conversation.emotion};
}

constexpr bool IsSpeechMotionActive(Conversation conversation, bool pcm_active) {
    // A late audio callback must not revive speaking after cancellation/failure.
    return conversation.status == EYE_STATE_SPEAKING && pcm_active;
}

} // namespace JapaEyeState
