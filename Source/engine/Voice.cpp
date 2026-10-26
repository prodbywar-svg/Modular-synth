#include "Voice.h"

#include <algorithm>

namespace ms
{
namespace
{
constexpr float kSilence = 0.0001f; // -80 dB
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
    type = track.voice;
    velocity = vel;
    age = 0;
    phase = 0.0;
    active = true;

    auto p = [&] (int i) { return voiceParamValue (type, i, track.voiceParams[(size_t) i]); };

    switch (type)
    {
        case VoiceType::Kick:
        {
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
        }
        case VoiceType::ClickHat:
            env = 1.0f;
            envMul = decayMultiplier (p (0), sr);
            filterA.setup (p (1), 0.707f, sr);
            b = p (2); // noise amount
            break;

        case VoiceType::NoiseSnare:
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
            freq = p (0);
            length = std::max<std::int64_t> (8, (std::int64_t) (p (1) * 0.001 * sr));
            freqMul = std::pow (2.0f, p (2) * 4.0f / (float) length);
            break;

        case VoiceType::Glitch:
            holdPeriod = sr / p (0);
            holdCounter = 0.0;
            b = p (1); // bits
            length = std::max<std::int64_t> (8, (std::int64_t) (p (2) * 0.001 * sr));
            break;

        case VoiceType::Sample:
        {
            sample = track.sample;
            if (sample == nullptr || sample->samples.empty())
            {
                active = false;
                break;
            }
            sampleStep = p (0) * sample->sampleRate / sr;
            samplePos = p (2) * (double) sample->samples.size();
            env = 1.0f;
            envMul = track.voiceParams[1] >= 0.999f ? 1.0f : decayMultiplier (p (1), sr);
            break;
        }
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
    switch (type)
    {
        case VoiceType::Kick:       out = nextKick(); break;
        case VoiceType::ClickHat:   out = nextClickHat(); break;
        case VoiceType::NoiseSnare: out = nextNoiseSnare(); break;
        case VoiceType::SineTone:   out = nextSineTone(); break;
        case VoiceType::Glitch:     out = nextGlitch(); break;
        case VoiceType::Sample:     out = nextSample(); break;
        case VoiceType::Count:      active = false; break;
    }

    ++age;
    return out * velocity * kVoiceHeadroom;
}

float Voice::nextKick()
{
    const float f = freqTarget + a;
    a *= freqMul;
    phase += f / sr;
    if (phase >= 1.0)
        phase -= 1.0;

    const float body = std::sin (2.0f * kPi * (float) phase) * env;
    const float impulse = age == 0 ? 1.0f : 0.0f;
    const float click = (impulse + noise.next() * 0.6f) * env2 * c;

    env *= envMul;
    env2 *= env2Mul;
    if (env < kSilence)
        active = false;

    // Hard drive into tanh, normalised so full drive still peaks at 1.
    return std::tanh (b * (body + click)) / std::tanh (b);
}

float Voice::nextClickHat()
{
    const float impulse = age == 0 ? 1.0f : 0.0f;
    const float x = impulse + noise.next() * b * env;
    env *= envMul;
    if (env < kSilence && age > 2)
        active = false;

    return filterA.highpass (x);
}

float Voice::nextNoiseSnare()
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

float Voice::nextSineTone()
{
    const float fade = (float) std::min<std::int64_t> (length / 4, (std::int64_t) (0.0005 * sr) + 1);
    const float g = std::min ({ 1.0f, (float) age / fade, (float) (length - age) / fade });

    phase += freq / sr;
    if (phase >= 1.0)
        phase -= std::floor (phase);
    freq = std::min (freq * freqMul, (float) sr * 0.45f);

    if (age + 1 >= length)
        active = false;

    return std::sin (2.0f * kPi * (float) phase) * std::max (0.0f, g);
}

float Voice::nextGlitch()
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
