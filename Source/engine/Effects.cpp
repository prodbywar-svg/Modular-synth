#include "Effects.h"

#include <algorithm>
#include <cmath>

namespace ms
{
// ---- GrainDelay -------------------------------------------------------------

namespace
{
constexpr double kGrainBufferSeconds = 4.5;
}

void GrainDelay::prepare (double sampleRate)
{
    sr = sampleRate;
    const auto size = (size_t) (kGrainBufferSeconds * sr);
    bufL.assign (size, 0.0f);
    bufR.assign (size, 0.0f);
    reset (99u);
}

void GrainDelay::reset (std::uint32_t seed)
{
    std::fill (bufL.begin(), bufL.end(), 0.0f);
    std::fill (bufR.begin(), bufR.end(), 0.0f);
    for (auto& g : grains)
        g = {};
    writePos = 0;
    spawnCounter = 0.0;
    lastWetL = lastWetR = 0.0f;
    noise.seed (seed);
}

void GrainDelay::set (double delaySeconds, double grainSeconds, float pitchSemitones, float sprayAmount, float fb, float wet)
{
    const double maxDelay = (double) bufL.size() * 0.5;
    grainSamples = std::clamp (grainSeconds * sr, 32.0, maxDelay * 0.5);
    delaySamples = std::clamp (delaySeconds * sr, 1.0, maxDelay);
    pitchRatio = std::pow (2.0, pitchSemitones / 12.0);
    spray = sprayAmount;
    feedback = fb;
    mix = wet;
}

float GrainDelay::read (const std::vector<float>& buf, double pos) const
{
    const int size = (int) buf.size();
    const int i0 = (int) pos;
    const int i1 = (i0 + 1) % size;
    const float frac = (float) (pos - (double) i0);
    return buf[(size_t) i0] + (buf[(size_t) i1] - buf[(size_t) i0]) * frac;
}

void GrainDelay::spawn()
{
    for (auto& g : grains)
    {
        if (g.on)
            continue;

        const double size = (double) bufL.size();
        g.on = true;
        g.age = 0;
        g.length = (int) grainSamples;
        g.step = pitchRatio;
        const double scatter = spray * (noise.next() * 0.5 + 0.5) * grainSamples * 2.0;
        // a grain played faster than real time needs a head start so it never
        // overtakes the write position
        const double headroom = pitchRatio > 1.0 ? (pitchRatio - 1.0) * grainSamples : 0.0;
        double pos = (double) writePos - delaySamples - scatter - headroom;
        while (pos < 0.0)
            pos += size;
        g.pos = std::fmod (pos, size);
        return;
    }
}

void GrainDelay::process (float* left, float* right, int numSamples)
{
    const double size = (double) bufL.size();

    for (int i = 0; i < numSamples; ++i)
    {
        spawnCounter -= 1.0;
        if (spawnCounter <= 0.0)
        {
            spawn();
            spawnCounter += std::max (1.0, grainSamples * 0.5); // two grains overlap
        }

        float wl = 0.0f, wr = 0.0f;
        for (auto& g : grains)
        {
            if (! g.on)
                continue;
            const float w = 0.5f - 0.5f * std::cos (2.0f * kPi * (float) g.age / (float) g.length);
            wl += read (bufL, g.pos) * w;
            wr += read (bufR, g.pos) * w;
            g.pos += g.step;
            if (g.pos >= size)
                g.pos -= size;
            if (++g.age >= g.length)
                g.on = false;
        }

        bufL[(size_t) writePos] = left[i] + wl * feedback;
        bufR[(size_t) writePos] = right[i] + wr * feedback;
        writePos = (writePos + 1) % (int) size;

        left[i] += wl * mix;
        right[i] += wr * mix;
    }
}

// ---- FdnReverb --------------------------------------------------------------

namespace
{
constexpr float kLineMs[8] = { 29.7f, 37.1f, 41.1f, 43.7f, 53.3f, 59.9f, 67.7f, 73.1f };
constexpr float kAllpassMs[4] = { 4.77f, 3.59f, 12.73f, 9.31f };
constexpr float kAllpassGain = 0.6f;

void hadamard8 (std::array<float, 8>& x)
{
    for (int len = 1; len < 8; len <<= 1)
        for (int i = 0; i < 8; i += len << 1)
            for (int j = i; j < i + len; ++j)
            {
                const float a = x[(size_t) j], b = x[(size_t) (j + len)];
                x[(size_t) j] = a + b;
                x[(size_t) (j + len)] = a - b;
            }
    for (auto& v : x)
        v *= 0.35355339f; // 1 / sqrt(8)
}
} // namespace

void FdnReverb::prepare (double sampleRate)
{
    sr = sampleRate;
    for (int i = 0; i < kLines; ++i)
    {
        lengths[(size_t) i] = std::max (8, (int) (kLineMs[i] * 0.001 * sr));
        lines[(size_t) i].assign ((size_t) lengths[(size_t) i], 0.0f);
    }
    for (int i = 0; i < kAllpasses; ++i)
        allpasses[(size_t) i].assign ((size_t) std::max (4, (int) (kAllpassMs[i] * 0.001 * sr)), 0.0f);
    reset();
}

void FdnReverb::reset()
{
    for (auto& l : lines)
        std::fill (l.begin(), l.end(), 0.0f);
    for (auto& a : allpasses)
        std::fill (a.begin(), a.end(), 0.0f);
    positions.fill (0);
    apPositions.fill (0);
    lowpass.fill (0.0f);
}

void FdnReverb::set (double decaySeconds, float damping, float wet)
{
    const double rt60 = std::max (0.05, decaySeconds);
    for (int i = 0; i < kLines; ++i)
        gains[(size_t) i] = (float) std::pow (10.0, -3.0 * lengths[(size_t) i] / (rt60 * sr));
    damp = damping * 0.85f;
    mix = wet;
}

void FdnReverb::process (float* left, float* right, int numSamples)
{
    std::array<float, kLines> taps {};

    for (int n = 0; n < numSamples; ++n)
    {
        float x = 0.5f * (left[n] + right[n]);

        // input diffusion
        for (int k = 0; k < kAllpasses; ++k)
        {
            auto& buf = allpasses[(size_t) k];
            auto& pos = apPositions[(size_t) k];
            const float delayed = buf[(size_t) pos];
            const float w = x - kAllpassGain * delayed;
            x = delayed + kAllpassGain * w;
            buf[(size_t) pos] = w;
            pos = (pos + 1) % (int) buf.size();
        }

        for (int i = 0; i < kLines; ++i)
        {
            const float d = lines[(size_t) i][(size_t) positions[(size_t) i]];
            lowpass[(size_t) i] = (1.0f - damp) * d + damp * lowpass[(size_t) i];
            taps[(size_t) i] = lowpass[(size_t) i];
        }

        const float wetL = taps[0] - taps[1] + taps[2] - taps[3] + taps[4] - taps[5] + taps[6] - taps[7];
        const float wetR = taps[0] + taps[1] - taps[2] - taps[3] + taps[4] + taps[5] - taps[6] - taps[7];

        hadamard8 (taps);
        for (int i = 0; i < kLines; ++i)
        {
            auto& pos = positions[(size_t) i];
            lines[(size_t) i][(size_t) pos] = x * 0.5f + gains[(size_t) i] * taps[(size_t) i];
            pos = (pos + 1) % lengths[(size_t) i];
        }

        left[n] += wetL * 0.25f * mix;
        right[n] += wetR * 0.25f * mix;
    }
}

// ---- Psychosis --------------------------------------------------------------

namespace
{
constexpr float kLowMid = 120.0f, kMidHigh = 2500.0f;
constexpr float kDownThreshold = -30.0f, kDownRatio = 6.0f;
constexpr float kUpThreshold = -42.0f, kUpRatio = 3.0f, kMaxBoostDb = 24.0f;
constexpr float kGateDb = -80.0f; // never lift what's effectively silence
constexpr float kMakeup = 1.4f;

float bandGain (float envelope)
{
    const float db = 20.0f * std::log10 (envelope + 1.0e-9f);
    float g = 0.0f;
    if (db > kDownThreshold)
        g -= (db - kDownThreshold) * (1.0f - 1.0f / kDownRatio);
    else if (db < kUpThreshold && db > kGateDb)
        g += std::min (kMaxBoostDb, (kUpThreshold - db) * (1.0f - 1.0f / kUpRatio));
    return std::pow (10.0f, g / 20.0f);
}
} // namespace

void Psychosis::prepare (double sampleRate)
{
    sr = sampleRate;
    for (auto& s : stages)
    {
        for (auto& f : s.low1) f.setup (kLowMid, 0.7071f, sr);
        for (auto& f : s.high1) f.setup (kLowMid, 0.7071f, sr);
        for (auto& f : s.low2) f.setup (kMidHigh, 0.7071f, sr);
        for (auto& f : s.high2) f.setup (kMidHigh, 0.7071f, sr);
    }
    attackMul = (float) std::exp (-1.0 / (0.001 * sr));
    reset();
}

void Psychosis::reset()
{
    for (auto& s : stages)
    {
        for (auto* bank : { &s.low1, &s.high1, &s.low2, &s.high2 })
            for (auto& f : *bank)
                f.reset();
        s.envelope.fill (0.0f);
    }
}

void Psychosis::set (int numberOfStages, float depthAmount, double releaseSeconds, float wet)
{
    numStages = std::clamp (numberOfStages, 1, kMaxStages);
    depth = depthAmount;
    releaseMul = (float) std::exp (-1.0 / (std::max (0.001, releaseSeconds) * sr));
    mix = wet;
}

void Psychosis::processStage (Stage& s, float& l, float& r)
{
    float bands[2][3];
    const float in[2] = { l, r };
    for (int ch = 0; ch < 2; ++ch)
    {
        const auto c = (size_t) ch * 2;
        const float x = in[ch];
        const float lo = s.low1[c + 1].lowpass (s.low1[c].lowpass (x));
        const float rest = s.high1[c + 1].highpass (s.high1[c].highpass (x));
        bands[ch][0] = lo;
        bands[ch][1] = s.low2[c + 1].lowpass (s.low2[c].lowpass (rest));
        bands[ch][2] = s.high2[c + 1].highpass (s.high2[c].highpass (rest));
    }

    float out[2] = { 0.0f, 0.0f };
    for (int b = 0; b < 3; ++b)
    {
        const float level = std::max (std::abs (bands[0][b]), std::abs (bands[1][b]));
        auto& e = s.envelope[(size_t) b];
        const float k = level > e ? attackMul : releaseMul;
        e = level + k * (e - level);
        const float g = bandGain (e) * kMakeup;
        out[0] += bands[0][b] * g;
        out[1] += bands[1][b] * g;
    }

    l = l * (1.0f - depth) + out[0] * depth;
    r = r * (1.0f - depth) + out[1] * depth;
}

void Psychosis::process (float* left, float* right, int numSamples)
{
    for (int n = 0; n < numSamples; ++n)
    {
        float l = left[n], r = right[n];
        for (int s = 0; s < numStages; ++s)
            processStage (stages[(size_t) s], l, r);
        // keep six stages of make-up gain from exploding
        l = std::tanh (l);
        r = std::tanh (r);
        left[n] = left[n] * (1.0f - mix) + l * mix;
        right[n] = right[n] * (1.0f - mix) + r * mix;
    }
}
} // namespace ms
