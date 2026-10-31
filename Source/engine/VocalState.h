#pragma once

#include "LooperState.h"

#include <memory>
#include <vector>

namespace ms
{
// The vocal track: the audio input recorded straight onto the song, from the
// first bar on. PLAY always starts at bar 1, so the recording plays back where
// it was sung. Recording again over part of it (a punch-in) replaces just
// that part; the rest stays.
constexpr int kVocalTracks = 2;     // what the signal boxes show
constexpr int kVocalTrack = 0;      // the vocal's output
constexpr int kVocalInputTrack = 1; // the input, after its gain

// One pass of the input, recorded by the audio thread into chunks handed to it
// as it goes. Never changes once the recording is done, so clips, undo and
// exports can share it.
struct VocalRecording
{
    static constexpr int kPeakWindow = 256; // samples per entry of peaks

    InputRecording audio;
    double sampleRate = 48000.0;
    std::vector<float> peaks; // filled in once it is done (UI side)

    void makePeaks();
};

// A piece of a recording on the song's timeline (samples from the start of bar 1).
struct VocalSegment
{
    const VocalRecording* source = nullptr;
    std::int64_t start = 0;  // where it plays on the timeline
    std::int64_t from = 0;   // where in the recording it starts
    std::int64_t length = 0; // samples
    std::int64_t end() const { return start + length; }
};

// What a vocal track plays: pieces of recordings in time order, never
// overlapping. Built on the UI side and never changed once the audio thread
// has it, like LoopAudio.
struct VocalClip
{
    std::vector<VocalSegment> segments;
    // Keeps the recordings the segments play alive (UI side).
    std::vector<std::shared_ptr<const VocalRecording>> sources;
#if ! MS_NO_JUCE
    juce::String file; // where it was last saved or opened from (UI side only)
#endif

    bool empty() const { return segments.empty(); }
    std::int64_t start() const { return segments.empty() ? 0 : segments.front().start; }
    std::int64_t end() const { return segments.empty() ? 0 : segments.back().end(); }
    // Overwrites l/r with the n samples from timeline position `position`,
    // silence between the pieces, with short fades where a piece starts or ends.
    void read (std::int64_t position, float* l, float* r, int n) const;
    // The loudest sample between two timeline positions (from the peaks).
    float peak (std::int64_t from, std::int64_t to) const;
};

// The clip with a new piece laid over it: what was under the piece is gone.
// base may be null.
VocalClip withSegment (const VocalClip* base, const VocalSegment&, std::shared_ptr<const VocalRecording> source);

struct VocalState
{
    float bpm = 132.0f;
    float swing = 0.0f;
    LoopInput input = LoopInput::In1; // a microphone is usually on input 1
    bool monitor = false;             // hear the input
    bool monitorDry = false;          // ... as it comes in, rather than through the rack
    int countIn = 1;                  // bars of clicks before a recording starts
    int metronome = kClickOff;
    float inputGain = 0.7071f;   // unity
    float masterLevel = 0.7071f; // the vocal's output, unity
    float pan = 0.5f;
    FxParams masterFx = defaultFxParams(); // the vocal's rack: the recording and the monitored input
    FxAutomation masterFxAuto;             // pattern 0 only: the moves loop every bar, like the studio master
    const VocalClip* clip = nullptr;       // nothing recorded
};

VocalState makeDefaultVocal();

// Applies a state-changing command for this vocal track (its tool already
// matched). RecordVocal and StopVocalRecord only matter to the engine.
void applyToVocal (VocalState&, const Command&);

#if ! MS_NO_JUCE
// The recording is saved beside the project as one WAV file.
struct VocalFile
{
    juce::String file; // empty = nothing recorded
    std::int64_t start = 0; // where the file starts on the timeline, in samples
};

juce::var vocalToVar (const VocalState&, const VocalFile&);
// The audio is for the caller to load from the returned file.
bool vocalFromVar (const juce::var&, VocalState&, VocalFile&);
#endif
} // namespace ms
