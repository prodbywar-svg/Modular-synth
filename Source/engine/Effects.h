#pragma once

#include "DspUtil.h"

#include <array>
#include <vector>

// The larger effects in the rack. Each processes stereo blocks in place and
// adds its wet signal on top of an untouched dry signal, except Psychosis,
// which crossfades because it rewrites the whole signal.

namespace ms
{
// Granular delay: overlapping windowed grains read from the past, optionally
// pitch-shifted and scattered ("spray"), with feedback.
class GrainDelay
{
public:
    void prepare (double sampleRate);
    void reset (std::uint32_t seed);
    void set (double delaySeconds, double grainSeconds, float pitchSemitones, float spray, float feedback, float mix);
    void process (float* left, float* right, int numSamples);

private:
    struct Grain
    {
        bool on = false;
        double pos = 0.0, step = 1.0;
        int age = 0, length = 1;
    };

    void spawn();
    float read (const std::vector<float>& buf, double pos) const;

    double sr = 48000.0;
    std::vector<float> bufL, bufR;
    int writePos = 0;
    std::array<Grain, 8> grains;
    double spawnCounter = 0.0;
    double delaySamples = 4800.0, grainSamples = 2400.0;
    double pitchRatio = 1.0;
    float spray = 0.0f, feedback = 0.0f, mix = 0.0f;
    float lastWetL = 0.0f, lastWetR = 0.0f;
    Noise noise;
};

// Feedback delay network reverb with an explicit decay time (RT60), so the
// tail can be set in seconds or in note lengths.
class FdnReverb
{
public:
    void prepare (double sampleRate);
    void reset();
    void set (double decaySeconds, float damping, float mix);
    void process (float* left, float* right, int numSamples);

private:
    static constexpr int kLines = 8;
    static constexpr int kAllpasses = 4;

    double sr = 48000.0;
    std::array<std::vector<float>, kLines> lines;
    std::array<int, kLines> lengths {}, positions {};
    std::array<float, kLines> gains {}, lowpass {};
    std::array<std::vector<float>, kAllpasses> allpasses;
    std::array<int, kAllpasses> apPositions {};
    float damp = 0.3f, mix = 0.0f;
};

// "Psychosis": up to six OTT-style stages in series. Each stage splits the
// signal into low / mid / high bands and squashes every band both downwards
// (loud parts) and upwards (quiet parts), which drags up detail and tails.
class Psychosis
{
public:
    static constexpr int kMaxStages = 6;

    void prepare (double sampleRate);
    void reset();
    void set (int stages, float depth, double releaseSeconds, float mix);
    void process (float* left, float* right, int numSamples);

private:
    struct Stage
    {
        // two Linkwitz-Riley crossovers (each = two cascaded Butterworth SVFs)
        std::array<Svf, 4> low1, high1, low2, high2; // [channel * 2 + cascade]
        std::array<float, 3> envelope {};
    };

    void processStage (Stage&, float& l, float& r);

    double sr = 48000.0;
    std::array<Stage, kMaxStages> stages;
    int numStages = 6;
    float depth = 0.6f, mix = 0.0f;
    float attackMul = 0.0f, releaseMul = 0.0f;
};
} // namespace ms
