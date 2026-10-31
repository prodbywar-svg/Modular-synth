#pragma once

#include "FxChain.h"
#include "VocalState.h"

#include <functional>
#include <vector>

namespace ms
{
// One vocal track: plays its clip along the song from bar 1; records the
// input from the next bar (after any count-in) until told to stop; passes the
// input through when monitoring. Then pan, the rack and the level.
class VocalEngine
{
public:
    enum class Status
    {
        Idle,
        Waiting,   // for the next bar
        CountIn,   // clicking the count-in bars
        Recording, // until StopVocalRecord, Stop, or the recording runs out of room
    };

    void prepare (double sampleRate, int maxBlockSize);
    // Silences it, cancels a recording and rewinds the clock.
    void reset();

    const VocalState& state() const { return vocal; }
    void setState (const VocalState&);

    void apply (const Command&);

    // Stopping while recording keeps what was recorded.
    void setPlaying (bool shouldPlay);
    bool isPlaying() const { return playing; }
    // timeline = samples since PLAY, so the clip plays from where it is.
    void joinClock (int stepIndex, double phaseInSamples, std::int64_t timeline);

    // The audio input for the next process() call (null = silence).
    void setInput (const float* left, const float* right)
    {
        inL = left;
        inR = right;
    }
    // Live, recordings write the input into their recording (with chunks
    // from chunkSource); when a take is played back for an export they don't,
    // and what is being recorded is heard as it will play afterwards.
    void setRecordsInput (bool b) { live = b; }
    void setChunkSource (std::function<float*()> f) { chunkSource = std::move (f); }

    // Overwrites left/right with numSamples of output.
    void process (float* left, float* right, int numSamples);

    // ---- read-outs ---------------------------------------------------------
    float trackPeak (int t) const { return t >= 0 && t < kVocalTracks ? peaks[(size_t) t] : 0.0f; }
    float masterPeak() const { return peaks[kVocalTrack]; }
    const float* trackOutput (int t) const { return taps.data() + std::clamp (t, 0, kVocalTracks - 1) * maxBlock; }

    Status status() const { return rec.status; }
    VocalRecording* recordBuffer() const { return rec.buffer; }
    int recordLatency() const { return rec.latency; }
    // Where the recording started on the timeline, and how much it holds so far.
    std::int64_t recordStart() const { return rec.start; }
    std::int64_t recordedLength() const { return std::max<std::int64_t> (0, rec.count - rec.latency); }
    // Beats of count-in still to click (4, 3, 2, 1), 0 when not counting in.
    int countInBeats() const;
    // Samples since PLAY (0 when stopped).
    std::int64_t timelinePosition() const { return timeline; }

    // Counts finished recordings; the last one is described here, for the UI
    // to lay over the clip.
    struct Finished
    {
        VocalRecording* recording = nullptr;
        VocalSegment segment; // where it goes on the timeline, latency taken off
    };
    int finishedCount() const { return finished; }
    const Finished& lastFinished() const { return lastDone; }

    double samplesPerStep() const;

private:
    void onBarStart();
    void finishRecording();

    double sr = 48000.0;
    int maxBlock = 512;
    VocalState vocal = makeDefaultVocal();
    FxChain fx;
    std::vector<float> bufL, bufR, dryL, dryR, taps, clipL, clipR, recL, recR, clickBuf;
    const float* inL = nullptr;
    const float* inR = nullptr;
    bool live = false;
    std::function<float*()> chunkSource;

    bool playing = false;
    int step = 0;
    double stepPhase = 0.0;
    bool barStart = false; // a bar starts at the next sample
    std::int64_t timeline = 0;

    struct Recording
    {
        Status status = Status::Idle;
        VocalRecording* buffer = nullptr;
        std::int64_t start = 0; // on the timeline
        std::int64_t count = 0; // samples since it started
        int latency = 0;        // samples the input arrives late
        int countLeft = 0;      // count-in bars still to start
    } rec;

    int clickAge = -1;
    bool clickAccent = false;

    int finished = 0;
    Finished lastDone;

    std::array<float, kVocalTracks> peaks {};
};
} // namespace ms
