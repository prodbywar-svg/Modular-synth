#pragma once

#include <cmath>
#include <cstdint>

namespace ms
{
constexpr float kPi = 3.14159265358979f;

// Topology-preserving state variable filter (Zavalishin). Gives low, band and
// high pass outputs from one update.
struct Svf
{
    void setup (float cutoffHz, float q, double sampleRate)
    {
        const float fc = std::fmin (cutoffHz, (float) sampleRate * 0.49f);
        g = std::tan (kPi * fc / (float) sampleRate);
        k = 1.0f / q;
        a1 = 1.0f / (1.0f + g * (g + k));
        a2 = g * a1;
        a3 = g * a2;
    }

    void reset() { ic1 = ic2 = 0.0f; }

    void process (float in, float& lp, float& bp, float& hp)
    {
        const float v3 = in - ic2;
        const float v1 = a1 * ic1 + a2 * v3;
        const float v2 = ic2 + a2 * ic1 + a3 * v3;
        ic1 = 2.0f * v1 - ic1;
        ic2 = 2.0f * v2 - ic2;
        lp = v2;
        bp = v1;
        hp = in - k * v1 - v2;
    }

    float lowpass (float in) { float lp, bp, hp; process (in, lp, bp, hp); return lp; }
    float highpass (float in) { float lp, bp, hp; process (in, lp, bp, hp); return hp; }

    float g = 0.0f, k = 1.0f, a1 = 0.0f, a2 = 0.0f, a3 = 0.0f;
    float ic1 = 0.0f, ic2 = 0.0f;
};

// Small deterministic noise source. Seeded per track so every export pass
// produces identical noise, which keeps stems and master in sync.
struct Noise
{
    void seed (std::uint32_t s) { state = s * 747796405u + 2891336453u; }

    float next() // white noise in -1..1
    {
        state ^= state << 13;
        state ^= state >> 17;
        state ^= state << 5;
        return (float) (std::int32_t) state * (1.0f / 2147483648.0f);
    }

    std::uint32_t state = 1;
};
} // namespace ms
