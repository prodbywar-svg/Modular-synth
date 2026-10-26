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

struct TrackState
{
    VoiceType voice = VoiceType::Kick;
    VoiceParams voiceParams = defaultVoiceParams (VoiceType::Kick);
    float level = 0.7071f; // unity gain
    float pan = 0.5f;
    bool mute = false;
    bool solo = false;
    FxParams fx = defaultFxParams();
    std::array<float, kNumSteps> steps {}; // velocity per step, 0 = off
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
};

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
    SetVoiceParam,  // track, index, value
    SetLevel,       // track, value
    SetPan,         // track, value
    SetMute,        // track, value 0/1
    SetSolo,        // track, value 0/1
    SetFxParam,     // track (-1 = master), index, value
    SetStep,        // track, index = step, value = velocity
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
