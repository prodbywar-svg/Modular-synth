#pragma once

#include "BassVoice.h"
#include "FxChain.h"

#include <vector>

namespace ms
{
// One bass synth: sequencer, 8 row voices with their effects and cut groups,
// mixer and bass bus. The same shape as EngineCore, the drum machine.
class BassEngine
{
public:
    void prepare (double sampleRate, int maxBlockSize);
    void reset();

    const BassState& state() const { return bass; }
    void setState (const BassState&);

    void apply (const Command&);

    void setPlaying (bool shouldPlay);
    bool isPlaying() const { return playing; }
    void joinClock (int stepIndex, double phaseInSamples);

    // -1 renders the bass synth's whole output, a row index only that row.
    void setSoloRender (int row) { soloRender = row; }

    // Overwrites left/right with numSamples of output.
    void process (float* left, float* right, int numSamples);

    // ---- read-outs ---------------------------------------------------------
    int currentStep() const { return step; }
    int currentPattern() const { return bass.pattern; }
    int queuedPattern() const { return bass.queuedPattern; }
    float trackPeak (int r) const { return peaks[(size_t) r]; }
    float masterPeak() const { return masterPeakValue; }
    const float* trackOutput (int r) const { return taps.data() + r * maxBlock; }
    bool trackTriggered (int r) const { return triggeredThisBlock[(size_t) r]; }
    void clearTriggered() { triggeredThisBlock.fill (false); }
    double samplesPerStep() const;

private:
    void fireStep (int stepIndex);
    void play (int row, int note, float velocity, double gateSamples);
    bool rowAudible (int r) const;

    double sr = 48000.0;
    int maxBlock = 512;
    BassState bass = makeDefaultBass();
    std::array<int, kBassRows> groups {};

    std::array<BassVoice, kBassRows> voices;
    std::array<FxChain, kBassRows> rowFx;
    FxChain busFx;

    std::vector<float> voiceBuf, rowL, rowR, taps;

    bool playing = false;
    int step = 0;
    double stepPhase = 0.0;
    bool stepFired = false;
    int soloRender = -1;

    std::array<float, kBassRows> peaks {};
    std::array<bool, kBassRows> triggeredThisBlock {};
    float masterPeakValue = 0.0f;
};
} // namespace ms
