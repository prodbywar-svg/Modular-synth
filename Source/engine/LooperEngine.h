#pragma once

#include "FxChain.h"
#include "LooperState.h"

#include <algorithm>
#include <vector>

namespace ms
{
// One looper: plays the loop slot of the pattern playing, locked to the bar,
// through its slice steps; records the input into a slot from the next bar;
// passes the input through when monitoring. Then pan, the rack and the level.
// A recording can start with a count-in: clicks on the beats for a bar or more.
class LooperEngine
{
public:
    enum class Status
    {
        Idle,
        Waiting,   // for the next bar
        CountIn,   // clicking the count-in bars
        Recording, // until the loop's last bar ends (plus the input latency)
    };

    void prepare (double sampleRate, int maxBlockSize);
    // Silences it, cancels a recording and rewinds the clock.
    void reset();

    const LooperState& state() const { return looper; }
    void setState (const LooperState&);

    void apply (const Command&);

    void setPlaying (bool shouldPlay);
    bool isPlaying() const { return playing; }
    void joinClock (int stepIndex, double phaseInSamples);

    // The audio input for the next process() call (null = silence).
    void setInput (const float* left, const float* right)
    {
        inL = left;
        inR = right;
    }
    // Live, recordings write the input into their loop; when a take is
    // played back for an export they only switch the slot over on time.
    void setRecordsInput (bool b) { live = b; }

    // Overwrites left/right with numSamples of output.
    void process (float* left, float* right, int numSamples);

    // ---- read-outs ---------------------------------------------------------
    int currentStep() const { return step; }
    int currentPattern() const { return looper.pattern; }
    int queuedPattern() const { return looper.queuedPattern; }
    float trackPeak (int t) const { return t >= 0 && t < kLooperTracks ? peaks[(size_t) t] : 0.0f; }
    float masterPeak() const { return peaks[kLoopTrack]; }
    const float* trackOutput (int t) const { return taps.data() + std::clamp (t, 0, kLooperTracks - 1) * maxBlock; }
    // True when a step jumped to another slice (the loop starting counts).
    bool trackTriggered (int t) const { return t == kLoopTrack && sliceStarted; }
    void clearTriggered() { sliceStarted = false; }

    Status status() const { return rec.status; }
    int recordSlot() const { return rec.slot; }
    bool recordOverdub() const { return rec.overdub; }
    LoopAudio* recordBuffer() const { return rec.buffer; }
    int recordLatency() const { return rec.latency; }
    // Beats of count-in still to click (4, 3, 2, 1), 0 when not counting in.
    int countInBeats() const;
    // How far through its bars the recording is, 0..1.
    double recordProgress() const;
    // Where the loop playing is, 0..1 of its length; -1 when nothing plays.
    double loopPosition() const;

    // Counts finished recordings; the last one put `audio` into `slot`.
    int finishedCount() const { return finished; }
    int lastFinishedSlot() const { return finishedSlot; }
    LoopAudio* lastFinishedAudio() const { return finishedAudio; }

    double samplesPerStep() const;

private:
    void onBarStart();
    void finishRecording();
    // The slice a step of the loop plays, as a step of the whole loop
    // (0 .. bars * 16), or -1 when muted.
    int sourceStep (const LoopSlot&, int bars, std::int64_t loopStep) const;
    int loopBar (int slot, int bars) const;

    double sr = 48000.0;
    int maxBlock = 512;
    LooperState looper = makeDefaultLooper();
    FxChain fx;
    std::vector<float> bufL, bufR, dryL, dryR, taps;
    const float* inL = nullptr;
    const float* inR = nullptr;
    bool live = false;

    bool playing = false;
    int step = 0;
    double stepPhase = 0.0;
    std::int64_t barCount = 0;  // bars since play started
    bool barStart = false;      // a bar starts at the next sample
    std::array<std::int64_t, kNumPatterns> origin {}; // the bar each slot's loop started on
    bool cutIn = false, cutOut = false; // the step playing jumps in / out of its slice
    bool stepChanged = true;

    struct Recording
    {
        Status status = Status::Idle;
        int slot = 0;
        bool overdub = false;
        LoopAudio* buffer = nullptr;
        std::int64_t startBar = 0;
        std::int64_t count = 0;     // samples since it started
        std::int64_t endCount = -1; // count when its last bar ended
        int latency = 0;            // samples the input arrives late
        int countLeft = 0;          // count-in bars still to start
    } rec;

    // the count-in click (live only, after the rack: never recorded or exported)
    std::vector<float> clickBuf;
    int clickAge = -1;
    bool clickAccent = false;

    int finished = 0, finishedSlot = 0;
    LoopAudio* finishedAudio = nullptr;

    std::array<float, kLooperTracks> peaks {};
    bool sliceStarted = false;
};
} // namespace ms
