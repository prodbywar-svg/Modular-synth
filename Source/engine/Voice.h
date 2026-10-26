#pragma once

#include "DspUtil.h"
#include "ProjectState.h"

namespace ms
{
// One mono drum voice. A new trigger cuts the previous hit (like a classic
// drum machine), which keeps the sound tight and stark.
class Voice
{
public:
    void prepare (double sampleRate);
    void reset (std::uint32_t seed);

    void trigger (const TrackState& track, float velocity);
    float next();

    bool isActive() const { return active; }

private:
    enum class Mode
    {
        DigitalKick,
        DigitalHat,
        DigitalSnare,
        DigitalSine,
        DigitalGlitch,
        LiveKick,
        LiveHat,
        LiveSnare,
        LiveTom,
        LiveClap,
        Sample,
    };

    float nextDigitalKick();
    float nextDigitalHat();
    float nextDigitalSnare();
    float nextDigitalSine();
    float nextDigitalGlitch();
    float nextLiveKick();
    float nextLiveHat();
    float nextLiveSnare();
    float nextLiveTom();
    float nextLiveClap();
    float nextSample();

    double sr = 48000.0;
    bool active = false;
    Mode mode = Mode::DigitalKick;
    float velocity = 1.0f;
    std::int64_t age = 0; // samples since trigger
    std::int64_t length = 0;

    // shared state, meaning depends on the mode
    double phase = 0.0;
    std::array<double, 6> phases {};
    std::array<float, 6> freqs {};
    float env = 0.0f, envMul = 1.0f;
    float env2 = 0.0f, env2Mul = 1.0f;
    float env3 = 0.0f, env3Mul = 1.0f;
    float freq = 0.0f, freqTarget = 0.0f, freqMul = 1.0f;
    float a = 0.0f, b = 0.0f, c = 0.0f, d = 0.0f;
    float held = 0.0f;
    double holdCounter = 0.0, holdPeriod = 1.0;
    double samplePos = 0.0, sampleStep = 1.0;
    const SampleData* sample = nullptr;

    Svf filterA, filterB;
    Noise noise;
};
} // namespace ms
