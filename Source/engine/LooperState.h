#pragma once

#include "ProjectState.h"

#include <functional>
#include <memory>
#include <vector>

namespace ms
{
// The looper: audio from the interface, recorded on the studio clock into one
// of four loop slots (P1-P4), each 1 to 8 bars long. Every bar of a loop is
// cut into 16 slices, and a strip of 16 steps says which slice each step
// plays, or mutes it.
constexpr int kLooperTracks = 2; // what the signal boxes show
constexpr int kLoopTrack = 0;    // the looper's output
constexpr int kInputTrack = 1;   // the input, after its gain
constexpr int kNumLoopLengths = 4;
constexpr int kLoopLengths[kNumLoopLengths] = { 1, 2, 4, 8 }; // bars
constexpr int kMuteSlice = -1;

// A recorded loop. The samples never change once the recording is done, so
// the audio thread, exports and undo can all share one by pointer; it is owned
// on the UI side, which keeps it alive while anything can still play it.
struct LoopAudio
{
    std::vector<float> left, right; // room for the recording; `length` is used
    int length = 0;                 // samples
    int bars = 1;                   // the loop spans this many bars, at any tempo
    float bpm = 132.0f;             // the tempo it was recorded at
    double sampleRate = 48000.0;
#if ! MS_NO_JUCE
    juce::String file; // where it was last saved or opened from (UI side only)
#endif
};

// The input of a take, for the export to play through the loopers again.
// Chunks are allocated off the audio thread and handed to it as it records.
struct InputRecording
{
    static constexpr int kChunkSamples = 1 << 16;
    static constexpr int kMaxChunks = 1 << 13; // over three hours at 48 kHz

    InputRecording() { chunks.reserve ((size_t) kMaxChunks); }
    ~InputRecording()
    {
        for (auto* c : chunks)
            delete[] c;
    }
    InputRecording (const InputRecording&) = delete;
    InputRecording& operator= (const InputRecording&) = delete;

    static float* makeChunk() { return new float[(size_t) (2 * kChunkSamples)] {}; }

    // Audio thread. Returns false (and drops the samples) when it needs a
    // new chunk and next has none to give.
    bool append (const float* l, const float* r, int n, const std::function<float*()>& next);
    // Silence past the end.
    void read (std::int64_t position, float* l, float* r, int n) const;

    std::vector<float*> chunks; // each holds kChunkSamples left, then kChunkSamples right
    std::int64_t length = 0;
};

using SlicePattern = std::array<std::int8_t, kNumSteps>; // the slice each step plays, kMuteSlice = silent

struct LoopSlot
{
    const LoopAudio* audio = nullptr; // nothing recorded
    int bars = 2;                     // the length of the next recording
    SlicePattern slices = straightSlices();

    static SlicePattern straightSlices()
    {
        SlicePattern s {};
        for (int i = 0; i < kNumSteps; ++i)
            s[(size_t) i] = (std::int8_t) i;
        return s;
    }
};

enum class LoopInput : std::uint8_t
{
    In1,    // input 1 on both sides
    In2,    // input 2 on both sides
    Stereo, // inputs 1 + 2
    Count
};

struct LooperState
{
    float bpm = 132.0f;
    float swing = 0.0f;
    LoopInput input = LoopInput::Stereo;
    bool monitor = false;          // hear the input through the rack
    float inputGain = 0.7071f;     // unity
    float masterLevel = 0.7071f;   // the looper's output, unity
    float pan = 0.5f;
    FxParams masterFx = defaultFxParams(); // the looper's rack: loop and monitored input
    FxAutomation masterFxAuto;
    std::array<LoopSlot, kNumPatterns> slots {};
    int pattern = 0;
    int queuedPattern = -1;
};

inline int viewedPattern (const LooperState& l) { return l.queuedPattern >= 0 ? l.queuedPattern : l.pattern; }

LooperState makeDefaultLooper();

const char* loopInputName (LoopInput);
bool straightSlices (const SlicePattern&);

// Applies a state-changing command for this looper (its tool already
// matched). RecordLoop and CancelLoopRecord only matter to the engine.
void applyToLooper (LooperState&, const Command&);

#if ! MS_NO_JUCE
// Loop audio is saved beside the project as WAV files, one per slot.
struct LoopFile
{
    juce::String file; // empty = no loop
    int bars = 1;
    float bpm = 132.0f;
};
using LoopFiles = std::array<LoopFile, kNumPatterns>;

// files[slot].file names each slot's audio file (empty = none saved).
juce::var looperToVar (const LooperState&, const LoopFiles& files);
// The audio is for the caller to load from the returned files.
bool looperFromVar (const juce::var&, LooperState&, LoopFiles& files);
#endif
} // namespace ms
