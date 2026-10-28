#pragma once

#include "BassState.h"
#include "DspUtil.h"

namespace ms
{
// One bass row's monophonic voice: oscillator(s) for its sound, a resonant
// low-pass with its own decaying envelope, drive, and an ADSR on the level.
// A note that arrives while the last one is still held slides into it
// (legato, with GLIDE) instead of starting the envelopes again.
class BassVoice
{
public:
    void prepare (double sampleRate);
    void reset (std::uint32_t seed);

    // Call once per block before next(): picks up the row's sound and controls.
    void setRow (const BassRow&);

    // gateSamples < 0 holds the note until noteOff().
    void noteOn (int midiNote, float velocity, double gateSamples);
    void noteOff();
    // Ends the note within a few milliseconds: another row in its cut group played.
    void cut();

    float next();
    bool isActive() const { return stage != Stage::Idle; }
    float currentNote() const { return note; }

private:
    enum class Stage
    {
        Idle,
        Attack,
        Decay,
        Sustain,
        Release
    };

    float oscillator (double dt);
    void updateFilter();

    double sr = 48000.0;
    BassSound sound = BassSound::Sub;
    float tone = 0.0f, cutoff = 1000.0f, q = 0.7f, envAmount = 0.0f, drive = 1.0f, driveNorm = 1.0f;
    float attackInc = 1.0f, decayMul = 0.0f, sustain = 1.0f, releaseMul = 0.0f, filterMul = 0.0f, glideCoef = 1.0f, cutMul = 0.0f;

    Stage stage = Stage::Idle;
    float env = 0.0f, filterEnv = 0.0f, velocity = 1.0f;
    float note = 36.0f, targetNote = 36.0f;
    double gateLeft = -1.0;
    bool cutting = false;

    double phase1 = 0.0, phase2 = 0.0, phaseSub = 0.0;
    float lastMod = 0.0f, held = 0.0f, holdCount = 0.0f;
    double age = 0.0; // samples since the envelopes last started (the BOOM drop)
    int filterCountdown = 0;
    Svf filter1, filter2;
};
} // namespace ms
