#include "Voice.h"

#include "Hr16Bank.h"

#include <algorithm>

namespace ms
{
namespace
{
constexpr float kSilence = 0.0001f;     // -80 dB
constexpr float kVoiceHeadroom = 0.5f; // each voice peaks at -6 dBFS so the mix has room

// Per-sample multiplier that decays to -60 dB over the given time.
float decayMultiplier (float ms, double sr)
{
    const double samples = std::max (1.0, ms * 0.001 * sr);
    return (float) std::exp (std::log (0.001) / samples);
}

// Per-sample multiplier for an exponential with the given time constant.
float timeConstantMultiplier (float seconds, double sr)
{
    return (float) std::exp (-1.0 / std::max (1.0, seconds * sr));
}

float quantise (float x, float bits)
{
    const float steps = std::pow (2.0f, std::round (bits)) - 1.0f;
    const float u = std::round ((x * 0.5f + 0.5f) * steps) / steps;
    return u * 2.0f - 1.0f;
}

float sine (double phase) { return std::sin (2.0f * kPi * (float) phase); }

void advance (double& phase, float hz, double sr)
{
    phase += hz / sr;
    if (phase >= 1.0)
        phase -= std::floor (phase);
}

// The TR-808's six hi-hat oscillator frequencies, as ratios.
constexpr float kMetalRatios[6] = { 1.0f, 1.4826f, 1.8003f, 2.5460f, 2.6303f, 3.8966f };
} // namespace

void Voice::prepare (double sampleRate)
{
    sr = sampleRate;
}

void Voice::reset (std::uint32_t seed)
{
    active = false;
    age = 0;
    filterA.reset();
    filterB.reset();
    noise.seed (seed);
}

void Voice::trigger (const TrackState& track, float vel)
{
    velocity = vel;
    age = 0;
    phase = 0.0;
    phases.fill (0.0);
    active = true;
    filterA.reset();
    filterB.reset();

    const auto type = track.voice;
    const auto character = track.character;
    auto p = [&] (int i) { return voiceParamValue (type, character, i, track.voiceParams[(size_t) i]); };

    // ---- samples: the HR-16 bank, or a loaded file ----------------------------
    if (type == VoiceType::Sample || character == Character::Hr16)
    {
        mode = Mode::Sample;
        const bool hr16 = type != VoiceType::Sample;
        sample = hr16 ? hr16Sample (type, (int) p (0)) : track.sample;
        if (sample == nullptr || sample->samples.size() < 2)
        {
            active = false;
            return;
        }
        const float ratio = hr16 ? std::pow (2.0f, p (1) / 12.0f) : p (0);
        sampleStep = ratio * sample->sampleRate / sr;
        samplePos = hr16 ? 0.0 : p (2) * (double) sample->samples.size();
        const int decayIndex = hr16 ? 2 : 1;
        env = 1.0f;
        envMul = track.voiceParams[(size_t) decayIndex] >= 0.999f ? 1.0f : decayMultiplier (p (decayIndex), sr);
        return;
    }

    if (character == Character::Live)
    {
        switch (type)
        {
            case VoiceType::Kick:
                mode = Mode::LiveKick;
                freqTarget = p (0);
                a = 1.2f; // pitch envelope depth
                freqMul = timeConstantMultiplier (0.015f, sr);
                env = 1.0f;
                envMul = decayMultiplier (p (1), sr);
                b = p (2); // beater
                c = p (3); // shell
                env2 = 1.0f;
                env2Mul = timeConstantMultiplier (0.06f, sr);
                env3 = 1.0f;
                env3Mul = timeConstantMultiplier (0.003f, sr);
                filterA.setup (3500.0f, 1.2f, sr);
                return;

            case VoiceType::ClickHat:
                mode = Mode::LiveHat;
                env = 1.0f;
                envMul = decayMultiplier (p (0), sr);
                for (int i = 0; i < 6; ++i)
                    freqs[(size_t) i] = 205.3f * kMetalRatios[i] * (p (1) / 8000.0f);
                filterA.setup (p (1), 1.0f, sr);
                filterB.setup (p (1) * 0.55f, 0.707f, sr);
                b = p (2); // metal vs noise
                return;

            case VoiceType::NoiseSnare:
                mode = Mode::LiveSnare;
                freqTarget = p (0);
                a = 0.25f;
                freqMul = timeConstantMultiplier (0.01f, sr);
                env = 1.0f; // wires
                envMul = decayMultiplier (p (1), sr);
                env2 = 1.0f; // shell
                env2Mul = decayMultiplier (std::min (p (1), 180.0f), sr);
                b = p (2); // wires amount
                filterA.setup (p (3), 0.707f, sr);
                filterB.setup (1200.0f, 0.707f, sr);
                return;

            case VoiceType::SineTone:
                mode = Mode::LiveTom;
                freqTarget = p (0);
                a = p (3) * 0.8f; // bend depth
                freqMul = timeConstantMultiplier (0.06f, sr);
                env = 1.0f;
                envMul = decayMultiplier (p (1), sr);
                env2 = 1.0f;
                env2Mul = decayMultiplier (p (1) * 0.4f, sr);
                env3 = 1.0f;
                env3Mul = timeConstantMultiplier (0.004f, sr);
                b = p (2); // stick
                filterA.setup (2000.0f, 0.707f, sr);
                return;

            case VoiceType::Glitch:
                mode = Mode::LiveClap;
                filterA.setup (p (1), 1.5f, sr);
                b = (float) (p (2) * 0.001 * sr); // burst spacing in samples
                envMul = decayMultiplier (p (0), sr);
                env3Mul = timeConstantMultiplier (0.0025f, sr);
                env = 1.0f;
                return;

            case VoiceType::Sample:
            case VoiceType::Count:
                break;
        }
    }

    // ---- digital (the datamatics originals) -----------------------------------
    switch (type)
    {
        case VoiceType::Kick:
            mode = Mode::DigitalKick;
            freqTarget = p (0);
            a = freqTarget * 6.0f; // pitch drop starts ~2.8 octaves up
            freqMul = timeConstantMultiplier (0.022f, sr);
            env = 1.0f;
            envMul = decayMultiplier (p (1), sr);
            b = p (2); // drive
            c = p (3); // click amount
            env2 = 1.0f;
            env2Mul = timeConstantMultiplier (0.0015f, sr);
            break;

        case VoiceType::ClickHat:
            mode = Mode::DigitalHat;
            env = 1.0f;
            envMul = decayMultiplier (p (0), sr);
            filterA.setup (p (1), 0.707f, sr);
            b = p (2); // noise amount
            break;

        case VoiceType::NoiseSnare:
            mode = Mode::DigitalSnare;
            env = 1.0f;
            envMul = decayMultiplier (p (0), sr);
            filterA.setup (p (1), 0.707f, sr);
            filterB.setup (p (3), 0.707f, sr);
            c = p (3); // colour cutoff, >= 19 kHz means no low-pass
            b = p (2) * 2.0f; // snap
            env2 = 1.0f;
            env2Mul = timeConstantMultiplier (0.003f, sr);
            break;

        case VoiceType::SineTone:
            mode = Mode::DigitalSine;
            freq = p (0);
            length = std::max<std::int64_t> (8, (std::int64_t) (p (1) * 0.001 * sr));
            freqMul = std::pow (2.0f, p (2) * 4.0f / (float) length);
            break;

        case VoiceType::Glitch:
            mode = Mode::DigitalGlitch;
            holdPeriod = sr / p (0);
            holdCounter = 0.0;
            b = p (1); // bits
            length = std::max<std::int64_t> (8, (std::int64_t) (p (2) * 0.001 * sr));
            break;

        case VoiceType::Sample:
        case VoiceType::Count:
            active = false;
            break;
    }
}

float Voice::next()
{
    if (! active)
        return 0.0f;

    float out = 0.0f;
    switch (mode)
    {
        case Mode::DigitalKick:   out = nextDigitalKick(); break;
        case Mode::DigitalHat:    out = nextDigitalHat(); break;
        case Mode::DigitalSnare:  out = nextDigitalSnare(); break;
        case Mode::DigitalSine:   out = nextDigitalSine(); break;
        case Mode::DigitalGlitch: out = nextDigitalGlitch(); break;
        case Mode::LiveKick:      out = nextLiveKick(); break;
        case Mode::LiveHat:       out = nextLiveHat(); break;
        case Mode::LiveSnare:     out = nextLiveSnare(); break;
        case Mode::LiveTom:       out = nextLiveTom(); break;
        case Mode::LiveClap:      out = nextLiveClap(); break;
        case Mode::Sample:        out = nextSample(); break;
    }

    ++age;
    return out * velocity * kVoiceHeadroom;
}

// ---- digital ----------------------------------------------------------------

float Voice::nextDigitalKick()
{
    const float f = freqTarget + a;
    a *= freqMul;
    advance (phase, f, sr);

    const float body = sine (phase) * env;
    const float impulse = age == 0 ? 1.0f : 0.0f;
    const float click = (impulse + noise.next() * 0.6f) * env2 * c;

    env *= envMul;
    env2 *= env2Mul;
    if (env < kSilence)
        active = false;

    // Hard drive into tanh, normalised so full drive still peaks at 1.
    return std::tanh (b * (body + click)) / std::tanh (b);
}

float Voice::nextDigitalHat()
{
    const float impulse = age == 0 ? 1.0f : 0.0f;
    const float x = impulse + noise.next() * b * env;
    env *= envMul;
    if (env < kSilence && age > 2)
        active = false;

    return filterA.highpass (x);
}

float Voice::nextDigitalSnare()
{
    const float gain = env * (1.0f + b * env2) / (1.0f + b);
    float x = filterA.highpass (noise.next() * gain);
    if (c < 19000.0f)
        x = filterB.lowpass (x);

    env *= envMul;
    env2 *= env2Mul;
    if (env < kSilence)
        active = false;

    return x;
}

float Voice::nextDigitalSine()
{
    const float fade = (float) std::min<std::int64_t> (length / 4, (std::int64_t) (0.0005 * sr) + 1);
    const float g = std::min ({ 1.0f, (float) age / fade, (float) (length - age) / fade });

    advance (phase, freq, sr);
    freq = std::min (freq * freqMul, (float) sr * 0.45f);

    if (age + 1 >= length)
        active = false;

    return sine (phase) * std::max (0.0f, g);
}

float Voice::nextDigitalGlitch()
{
    holdCounter -= 1.0;
    if (holdCounter <= 0.0)
    {
        held = quantise (noise.next(), b);
        holdCounter += holdPeriod;
    }

    const float fade = (float) std::min<std::int64_t> (length / 4, (std::int64_t) (0.0003 * sr) + 1);
    const float g = std::min ({ 1.0f, (float) age / fade, (float) (length - age) / fade });

    if (age + 1 >= length)
        active = false;

    return held * std::max (0.0f, g);
}

// ---- live -------------------------------------------------------------------

float Voice::nextLiveKick()
{
    const float f = freqTarget * (1.0f + a);
    a *= freqMul;
    advance (phases[0], f, sr);
    advance (phases[1], f * 1.59f, sr);
    advance (phases[2], f * 2.14f, sr);

    const float body = sine (phases[0]) * env;
    const float shell = c * (0.35f * sine (phases[1]) + 0.2f * sine (phases[2])) * env2;
    const float impulse = age == 0 ? 1.0f : 0.0f;
    float lp, bp, hp;
    filterA.process (noise.next() + impulse * 4.0f, lp, bp, hp);
    const float beater = b * bp * env3 * 1.5f;

    env *= envMul;
    env2 *= env2Mul;
    env3 *= env3Mul;
    if (env < kSilence)
        active = false;

    return std::tanh (1.6f * (body + shell + beater)) / std::tanh (1.6f);
}

float Voice::nextLiveHat()
{
    float metal = 0.0f;
    for (size_t i = 0; i < 6; ++i)
    {
        advance (phases[i], freqs[i], sr);
        metal += phases[i] < 0.5 ? 1.0f : -1.0f;
    }
    const float x = b * metal / 6.0f + (1.0f - b) * noise.next();

    float lp, bp, hp;
    filterA.process (x, lp, bp, hp);
    const float y = filterB.highpass (bp) * env * 2.5f;

    env *= envMul;
    if (env < kSilence)
        active = false;

    return std::clamp (y, -1.0f, 1.0f);
}

float Voice::nextLiveSnare()
{
    const float f = freqTarget * (1.0f + a);
    a *= freqMul;
    advance (phases[0], f, sr);
    advance (phases[1], f * 1.505f, sr);

    const float shell = (0.7f * sine (phases[0]) + 0.4f * sine (phases[1])) * env2;
    const float wires = filterB.highpass (filterA.lowpass (noise.next())) * env * 1.4f;

    env *= envMul;
    env2 *= env2Mul;
    if (env < kSilence && env2 < kSilence)
        active = false;

    const float x = shell * (1.0f - 0.4f * b) + wires * b;
    return std::tanh (1.3f * x) / std::tanh (1.3f);
}

float Voice::nextLiveTom()
{
    const float f = freqTarget * (1.0f + a);
    a *= freqMul;
    advance (phases[0], f, sr);
    advance (phases[1], f * 1.5f, sr);

    const float body = sine (phases[0]) * env + 0.25f * sine (phases[1]) * env2;
    const float stick = b * filterA.highpass (noise.next()) * env3;

    env *= envMul;
    env2 *= env2Mul;
    env3 *= env3Mul;
    if (env < kSilence)
        active = false;

    return std::tanh (1.2f * (body + stick)) / std::tanh (1.2f);
}

float Voice::nextLiveClap()
{
    // three quick hand bursts, then a decaying tail
    const auto spacing = std::max<std::int64_t> (1, (std::int64_t) b);
    float e;
    if (age < spacing * 3)
    {
        if (age % spacing == 0)
            env3 = 1.0f - 0.15f * (float) (age / spacing);
        e = env3;
        env3 *= env3Mul;
    }
    else
    {
        if (age == spacing * 3)
            env = 1.0f;
        e = env;
        env *= envMul;
        if (env < kSilence)
            active = false;
    }

    float lp, bp, hp;
    filterA.process (noise.next(), lp, bp, hp);
    return std::clamp (bp * e * 2.5f, -1.0f, 1.0f);
}

// ---- samples ----------------------------------------------------------------

float Voice::nextSample()
{
    const auto& s = sample->samples;
    const auto i = (size_t) samplePos;
    if (i + 1 >= s.size() || env < kSilence)
    {
        active = false;
        return 0.0f;
    }

    const float frac = (float) (samplePos - (double) i);
    const float x = s[i] + (s[i + 1] - s[i]) * frac;
    samplePos += sampleStep;
    const float out = x * env;
    env *= envMul;
    return out;
}
} // namespace ms
