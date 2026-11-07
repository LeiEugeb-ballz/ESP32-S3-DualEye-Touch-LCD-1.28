#include "../main/boards/waveshare/esp32-s3-dualeye-lcd-1.28/eye_conversation_state.h"

using namespace JapaEyeState;

constexpr Conversation idle{};
constexpr auto connecting = WithStatus(idle, EYE_STATE_THINKING);
constexpr auto connecting_neutral = WithEmotion(connecting, EYE_STATE_IDLE);
static_assert(Resolve(connecting_neutral).behavior == EYE_STATE_THINKING,
              "Application connecting then neutral must keep thoughtful attention");

constexpr auto listening = WithStatus(connecting_neutral, EYE_STATE_LISTENING);
constexpr auto listening_neutral = WithEmotion(listening, EYE_STATE_IDLE);
static_assert(Resolve(listening_neutral).behavior == EYE_STATE_LISTENING,
              "Application listening then neutral must keep eye contact");
static_assert(WithStatus(listening_neutral, EYE_STATE_LISTENING).Pack() == listening_neutral.Pack(),
              "Repeated listening status must not create a transition");
static_assert(WithEmotion(listening_neutral, EYE_STATE_IDLE).Pack() == listening_neutral.Pack(),
              "Repeated neutral must not create a transition");

constexpr auto preannounced_happy = WithEmotion(listening, EYE_STATE_HAPPY);
static_assert(Resolve(preannounced_happy).behavior == EYE_STATE_LISTENING &&
              Resolve(preannounced_happy).expression == EYE_STATE_IDLE,
              "A pre-TTS expression cannot replace listening attention");
constexpr auto speaking_happy = WithStatus(preannounced_happy, EYE_STATE_SPEAKING);
static_assert(Resolve(speaking_happy).behavior == EYE_STATE_SPEAKING &&
              Resolve(speaking_happy).expression == EYE_STATE_HAPPY,
              "Speaking must use conversational gaze with the announced expression");
static_assert(WithStatus(speaking_happy, EYE_STATE_SPEAKING).Pack() == speaking_happy.Pack(),
              "Repeated speaking status must retain its current expression");
static_assert(Resolve(WithEmotion(speaking_happy, EYE_STATE_IDLE)).behavior == EYE_STATE_SPEAKING,
              "Neutral during speech must not fall back to idle gaze");
static_assert(Resolve(WithEmotion(speaking_happy, EYE_STATE_IDLE)).expression == EYE_STATE_IDLE,
              "Neutral clears expression without cancelling the turn");
static_assert(Resolve(WithEmotion(speaking_happy, EYE_STATE_SAD_SLEEPY)).behavior == EYE_STATE_SPEAKING,
              "Sad speech must keep speaking behavior");
static_assert(Resolve(WithEmotion(speaking_happy, EYE_STATE_ANGRY)).expression == EYE_STATE_ANGRY,
              "Angry speech retains its expression overlay");
static_assert(Resolve(WithEmotion(speaking_happy, EYE_STATE_SURPRISED)).behavior == EYE_STATE_SPEAKING,
              "Surprised speech must keep speaking behavior");

constexpr auto interrupted = WithStatus(speaking_happy, EYE_STATE_LISTENING);
static_assert(Resolve(interrupted).behavior == EYE_STATE_LISTENING &&
              interrupted.emotion == EYE_STATE_IDLE,
              "Interruption returns attention and clears the old speaking expression");
static_assert(!IsSpeechMotionActive(interrupted, true),
              "A late PCM callback cannot keep speech pulses after interruption");
constexpr auto disconnected = WithStatus(speaking_happy, EYE_STATE_IDLE);
static_assert(disconnected.Pack() == idle.Pack(),
              "Failure or completed session returns to neutral idle");
static_assert(!IsSpeechMotionActive(disconnected, true),
              "A late PCM callback cannot revive speaking after session failure");
static_assert(Resolve(WithStatus(speaking_happy, EYE_STATE_THINKING)).behavior == EYE_STATE_THINKING,
              "Reconnect clears the previous speaking behavior");
static_assert(WithStatus(speaking_happy, EYE_STATE_THINKING).emotion == EYE_STATE_IDLE,
              "Reconnect clears a stale emotional expression");
static_assert(!IsSpeechMotionActive(speaking_happy, false),
              "TTS metadata alone cannot trigger PCM-driven eye pulses");
static_assert(IsSpeechMotionActive(speaking_happy, true),
              "Actual PCM playback enables speech energy motion");

constexpr auto idle_alert = WithEmotion(idle, EYE_STATE_SURPRISED);
static_assert(Resolve(idle_alert).behavior == EYE_STATE_SURPRISED,
              "Deliberate idle UI alerts preserve their existing expression profile");
static_assert(Resolve(WithEmotion(idle_alert, EYE_STATE_IDLE)).behavior == EYE_STATE_IDLE,
              "Clearing an idle alert restores ordinary idle exploration");
static_assert(Conversation::Unpack(speaking_happy.Pack()).Pack() == speaking_happy.Pack(),
              "Packed cross-core snapshot preserves status and emotion together");

constexpr bool RepeatedEventsKeepGazePhase() {
    auto conversation = speaking_happy;
    const auto behavior = Resolve(conversation).behavior;
    for (int frame = 0; frame < 100; ++frame) {
        conversation = WithStatus(conversation, EYE_STATE_SPEAKING);
        conversation = WithEmotion(conversation, frame % 2 ? EYE_STATE_IDLE : EYE_STATE_HAPPY);
        if (Resolve(conversation).behavior != behavior) return false;
    }
    return true;
}
static_assert(RepeatedEventsKeepGazePhase(),
              "Neutral/expression traffic must not repeatedly reset the renderer's speaking gaze clock");

int main() { return 0; }
