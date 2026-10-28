#pragma once

#include "FxChain.h"
#include "ProjectState.h"
#include "Voice.h"

#include <vector>

namespace ms
{
// The whole instrument: sequencer, 8 voices with their effects, mixer and
// master effects. It knows nothing about audio devices, threads or files, so
// the same code runs live, offline for export, and later inside a DAW.
class EngineCore
{
public:
    void prepare (double sampleRate, int maxBlockSize);

    // Silences voices, clears effect tails and rewinds the sequencer, so the
    // next samples are exactly what an offline render from here would produce.
    void reset();

    const ProjectState& state() const { return project; }
    void setState (const ProjectState&);

    void apply (const Command&);

    void setPlaying (bool shouldPlay);
    bool isPlaying() const { return playing; }
    // Starts playing from the middle of a step, so a sequencer added while the
    // others play lands exactly on their beat.
    void joinClock (int stepIndex, double phaseInSamples);
    double stepPhaseSamples() const { return stepPhase; }

    // -1 renders the full mix. A track index renders only that track's share of
    // the master (after its effects and the master effects): one stem.
    void setSoloRender (int track) { soloRender = track; }

    // Overwrites left/right with numSamples of output.
    void process (float* left, float* right, int numSamples);

    // ---- read-outs for the UI (written by process) ------------------------
    int currentStep() const { return step; }
    int currentPattern() const { return project.pattern; }
    int queuedPattern() const { return project.queuedPattern; }
    float trackPeak (int t) const { return peaks[(size_t) t]; }
    float masterPeak() const { return masterPeakValue; }
    // A track's output (mono, after its effects and level, silent when muted)
    // for the last chunk process() rendered: the whole call when numSamples
    // is no more than the prepared maxBlockSize.
    const float* trackOutput (int t) const { return taps.data() + t * maxBlock; }
    // True if the track was hit since the last clearTriggered().
    bool trackTriggered (int t) const { return triggeredThisBlock[(size_t) t]; }
    void clearTriggered() { triggeredThisBlock.fill (false); }

    double getSampleRate() const { return sr; }
    double samplesPerStep() const;

private:
    void fireStep (int stepIndex);
    bool trackAudible (int t) const;

    double sr = 48000.0;
    int maxBlock = 512;
    ProjectState project = makeDefaultProject();

    std::array<Voice, kNumTracks> voices;
    std::array<FxChain, kNumTracks> trackFx;
    FxChain masterFx;

    std::vector<float> voiceBuf; // kNumTracks * maxBlock mono
    std::vector<float> trackL, trackR;
    std::vector<float> taps; // kNumTracks * maxBlock mono, see trackOutput()

    bool playing = false;
    int step = 0;
    double stepPhase = 0.0;
    bool stepFired = false;
    int soloRender = -1;

    std::array<float, kNumTracks> peaks {};
    std::array<bool, kNumTracks> triggeredThisBlock {};
    float masterPeakValue = 0.0f;
};
} // namespace ms
