#pragma once

#include "Params.h"

#include <juce_core/juce_core.h>

#include <cstdint>

namespace ms
{
// Mono sample loaded from disk. Owned by the UI side (kept alive for the whole
// session) so the audio thread can hold a plain pointer to it.
struct SampleData
{
    std::vector<float> samples;
    double sampleRate = 44100.0;
    juce::String path;
};

using Pattern = std::array<float, kNumSteps>; // velocity per step, 0 = off

struct TrackState
{
    VoiceType voice = VoiceType::Kick;
    Character character = Character::Digital;
    VoiceParams voiceParams = defaultVoiceParams (VoiceType::Kick, Character::Digital);
    float level = 0.7071f; // unity gain
    float pan = 0.5f;
    bool mute = false;
    bool solo = false;
    FxParams fx = defaultFxParams();
    std::array<Pattern, kNumPatterns> patterns {};
    const SampleData* sample = nullptr;
};

// Everything the engine needs to make sound. Plain data with no allocations, so
// the audio thread can copy it (e.g. as the starting point of a recording).
struct ProjectState
{
    float bpm = 132.0f;
    float swing = 0.0f; // 0..1
    std::array<TrackState, kNumTracks> tracks {};
    float masterLevel = 0.5f; // -6 dB of headroom for the sum
    FxParams masterFx = defaultFxParams();
    int pattern = 0;        // the pattern playing (or that will play)
    int queuedPattern = -1; // switches in when the current pattern ends
};

// The pattern being edited on screen: the queued one if any, else the current.
inline int viewedPattern (const ProjectState& p) { return p.queuedPattern >= 0 ? p.queuedPattern : p.pattern; }

constexpr float kMinBpm = 40.0f;
constexpr float kMaxBpm = 300.0f;

// The datamatics starter kit and pattern.
ProjectState makeDefaultProject();

// Every change to the project travels as a Command: from the UI to the audio
// thread, into a recorded performance, and back out during offline export.
enum class CommandType : std::uint8_t
{
    Trigger,        // track, value = velocity
    SetVoiceType,   // track, index = VoiceType (resets voice params)
    SetCharacter,   // track, index = Character (resets voice params)
    SetVoiceParam,  // track, index, value
    SetLevel,       // track, value
    SetPan,         // track, value
    SetMute,        // track, value 0/1
    SetSolo,        // track, value 0/1
    SetFxParam,     // track (-1 = master), index, value
    SetStep,        // track, index = pattern * kNumSteps + step, value = velocity
    SelectPattern,  // index = pattern, value 1 = wait for the current pattern to end
    SetBpm,         // value in BPM
    SetSwing,       // value
    SetMasterLevel, // value
    SetSample,      // track, sample
    // Transport: handled by the realtime processor, never recorded.
    Play,
    Stop,
    RecordStart,
    RecordStop,
};

struct Command
{
    CommandType type = CommandType::Trigger;
    int track = 0;
    int index = 0;
    float value = 0.0f;
    const SampleData* sample = nullptr;
};

struct TimedCommand
{
    std::int64_t time = 0; // in samples from the start of the recording
    Command command;
};

bool isTransport (CommandType);

// Applies a state-changing command. Trigger and transport commands are ignored.
void applyToState (ProjectState&, const Command&);

// ---- save / open ------------------------------------------------------------
juce::var projectToVar (const ProjectState&);
// Sample paths are returned so the caller can load them; sample pointers in the
// returned state are null.
bool projectFromVar (const juce::var&, ProjectState&, std::array<juce::String, kNumTracks>& samplePaths);
} // namespace ms
