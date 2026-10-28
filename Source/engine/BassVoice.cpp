#include "BassVoice.h"

#include <algorithm>

namespace ms
{
namespace
{
constexpr float kCutSeconds = 0.003f;

// Smooths the corner of a jump in a naive waveform (PolyBLEP).
float blep (double t, double dt)
{
    if (t < dt)
    {
        t /= dt;
        return (float) (t + t - t * t - 1.0);
    }
    if (t > 1.0 - dt)
    {
        t = (t - 1.0) / dt;
        return (float) (t * t + t + t + 1.0);
    }
    return 0.0f;
}

float saw (double t, double dt) { return (float) (2.0 * t - 1.0) - blep (t, dt); }

float pulse (double t, double dt, double width)
{
    double t2 = t + 1.0 - width;
    t2 -= std::floor (t2);
    return (t < width ? 1.0f : -1.0f) + blep (t, dt) - blep (t2, dt);
}

float sine (double t) { return std::sin (2.0f * kPi * (float) t); }

double wrap (double p) { return p - std::floor (p); }

// per-sample multiplier that takes a value to 1/1000 of itself in `seconds`
float decayMultiplier (float seconds, double sr)
{
    return (float) std::exp (-6.9078 / std::max (1.0, (double) seconds * sr));
}
} // namespace

void BassVoice::prepare (double sampleRate)
{
    sr = sampleRate;
    cutMul = decayMultiplier (kCutSeconds, sr);
    reset (1u);
}

void BassVoice::reset (std::uint32_t)
{
    stage = Stage::Idle;
    env = filterEnv = 0.0f;
    gateLeft = -1.0;
    cutting = false;
    phase1 = phase2 = phaseSub = 0.0;
    lastMod = held = holdCount = 0.0f;
    age = 0.0;
    filter1.reset();
    filter2.reset();
    filterCountdown = 0;
}

void BassVoice::setRow (const BassRow& row)
{
    sound = row.sound;
    const auto& p = row.params;
    auto value = [&] (int i) { return bassParamValue (sound, i, p[(size_t) i]); };
    tone = value (bpTone);
    cutoff = value (bpCutoff);
    q = value (bpResonance);
    envAmount = value (bpEnvAmount);
    filterMul = decayMultiplier (value (bpFilterDecay) * 0.001f, sr);
    drive = value (bpDrive);
    driveNorm = 1.0f / std::tanh (drive);
    const float glideMs = value (bpGlide);
    glideCoef = glideMs < 1.0f ? 1.0f : 1.0f - (float) std::exp (-1.0 / (glideMs * 0.001 * sr * 0.3));
    attackInc = 1.0f / std::max (1.0f, value (bpAttack) * 0.001f * (float) sr);
    decayMul = decayMultiplier (value (bpDecay) * 0.001f, sr);
    sustain = value (bpSustain);
    releaseMul = decayMultiplier (value (bpRelease) * 0.001f, sr);
}

void BassVoice::noteOn (int midiNote, float vel, double gateSamples)
{
    const bool legato = stage != Stage::Idle && stage != Stage::Release && ! cutting;
    targetNote = (float) midiNote;
    if (stage == Stage::Idle || glideCoef >= 1.0f)
        note = targetNote;
    gateLeft = gateSamples;
    velocity = vel;
    cutting = false;

    if (! legato)
    {
        stage = Stage::Attack;
        filterEnv = 1.0f;
        age = 0.0;
        if (sound == BassSound::Boom)
            phase1 = 0.0; // the 808 always starts from zero crossing
    }
}

void BassVoice::noteOff()
{
    if (stage != Stage::Idle)
        stage = Stage::Release;
    gateLeft = -1.0;
}

void BassVoice::cut()
{
    if (stage == Stage::Idle)
        return;
    stage = Stage::Release;
    cutting = true;
    gateLeft = -1.0;
}

void BassVoice::updateFilter()
{
    const float octaves = envAmount * filterEnv * (0.5f + 0.5f * velocity);
    const float hz = std::clamp (cutoff * std::pow (2.0f, octaves), 20.0f, (float) sr * 0.45f);
    filter1.setup (hz, sound == BassSound::Acid ? 0.6f : q, sr);
    filter2.setup (hz, q, sr);
}

float BassVoice::oscillator (double dt)
{
    switch (sound)
    {
        case BassSound::Sub:
            return sine (phase1) + tone * 0.5f * sine (wrap (phase1 * 2.0)) + tone * 0.25f * sine (wrap (phase1 * 3.0));
        case BassSound::Pulse:
            return pulse (phase1, dt, tone) * 0.8f;
        case BassSound::Saw:
            return saw (phase1, dt) * 0.8f + tone * sine (phaseSub);
        case BassSound::Acid:
            return (1.0f - tone) * saw (phase1, dt) + tone * pulse (phase1, dt, 0.5);
        case BassSound::Fm:
        {
            const float index = tone * (0.3f + 0.7f * filterEnv);
            const float mod = sine (wrap (phase2 + 0.12 * lastMod));
            lastMod = mod;
            return sine (wrap (phase1 + index * mod * 0.25f));
        }
        case BassSound::Reese:
            return 0.55f * (saw (phase1, dt) + saw (phase2, dt)) + 0.25f * sine (phaseSub);
        case BassSound::OneBit:
        {
            holdCount -= 1.0f;
            if (holdCount <= 0.0f)
            {
                held = phase1 < 0.5 ? 0.7f : -0.7f;
                holdCount += tone;
            }
            return held;
        }
        case BassSound::Boom:
            return sine (phase1);
        case BassSound::Count:
            break;
    }
    return 0.0f;
}

float BassVoice::next()
{
    if (stage == Stage::Idle)
        return 0.0f;

    // gate and envelopes
    if (gateLeft >= 0.0)
    {
        gateLeft -= 1.0;
        if (gateLeft < 0.0)
            stage = Stage::Release;
    }
    switch (stage)
    {
        case Stage::Attack:
            env += attackInc;
            if (env >= 1.0f)
            {
                env = 1.0f;
                stage = Stage::Decay;
            }
            break;
        case Stage::Decay:
            env = sustain + (env - sustain) * decayMul;
            if (env - sustain < 0.0005f)
                stage = Stage::Sustain;
            break;
        case Stage::Sustain:
            env = sustain;
            if (sustain < 0.0001f)
                stage = Stage::Idle;
            break;
        case Stage::Release:
            env *= cutting ? cutMul : releaseMul;
            if (env < 0.00002f)
            {
                stage = Stage::Idle;
                env = 0.0f;
                cutting = false;
            }
            break;
        case Stage::Idle:
            break;
    }
    filterEnv *= filterMul;

    // pitch, with glide
    note += (targetNote - note) * glideCoef;
    float hz = noteHz (note);
    if (sound == BassSound::Boom)
        hz *= std::pow (2.0f, tone * std::exp (-(float) (age / sr) / 0.05f));
    const double dt = std::min (0.5, (double) hz / sr);

    float x = oscillator (dt);

    phase1 = wrap (phase1 + dt);
    phaseSub = wrap (phaseSub + dt * 0.5);
    if (sound == BassSound::Reese)
        phase2 = wrap (phase2 + dt * std::pow (2.0, tone / 1200.0));
    else
        phase2 = wrap (phase2 + dt);
    age += 1.0;

    // filter (coefficients refreshed every 16 samples), drive, level
    if (--filterCountdown <= 0)
    {
        updateFilter();
        filterCountdown = 16;
    }
    x = filter1.lowpass (x);
    if (sound == BassSound::Acid || sound == BassSound::Saw || sound == BassSound::Reese)
        x = filter2.lowpass (x);
    x = std::tanh (x * drive) * driveNorm;

    return x * env * velocity * 0.5f;
}
} // namespace ms
