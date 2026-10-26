#include "FxChain.h"

#include <algorithm>

namespace ms
{
namespace
{
constexpr float kMaxDelaySeconds = 2.0f;
}

void FxChain::prepare (double sampleRate)
{
    sr = sampleRate;
    const auto size = (size_t) (kMaxDelaySeconds * sr) + 2;
    delayL.assign (size, 0.0f);
    delayR.assign (size, 0.0f);
    reverb.setSampleRate (sr);
    reset();
}

void FxChain::reset()
{
    holdCounter = 0.0f;
    heldL = heldR = 0.0f;
    filterL.reset();
    filterR.reset();
    std::fill (delayL.begin(), delayL.end(), 0.0f);
    std::fill (delayR.begin(), delayR.end(), 0.0f);
    writePos = 0;
    reverb.reset();
    delayOn = reverbOn = filterOn = false;
    firstParams = true;
    setParams (current);
    delaySamples = delayTarget;
}

void FxChain::setParams (const FxParams& p)
{
    if (! firstParams && p == current)
        return;
    current = p;
    firstParams = false;

    bits = crushBits (p[fxCrushBits]);
    holdFactor = crushFactor (p[fxCrushRate]);

    filterOn = p[fxCutoff] < 0.999f || p[fxResonance] > 0.001f;
    filterL.setup (cutoffHz (p[fxCutoff]), resonanceQ (p[fxResonance]), sr);
    filterR.setup (cutoffHz (p[fxCutoff]), resonanceQ (p[fxResonance]), sr);

    const bool wantDelay = p[fxDelayMix] > 0.0001f;
    if (wantDelay && ! delayOn)
    {
        std::fill (delayL.begin(), delayL.end(), 0.0f);
        std::fill (delayR.begin(), delayR.end(), 0.0f);
    }
    delayOn = wantDelay;
    delayTarget = (float) (delayMs (p[fxDelayTime]) * 0.001 * sr);
    feedback = delayFeedback (p[fxDelayFeedback]);
    delayMix = p[fxDelayMix];

    const bool wantReverb = p[fxReverbMix] > 0.0001f;
    if (wantReverb && ! reverbOn)
        reverb.reset();
    reverbOn = wantReverb;

    juce::Reverb::Parameters rp;
    rp.roomSize = reverbRoom (p[fxReverbSize]);
    rp.damping = 0.35f;
    // juce::Reverb scales dry by 2 and wet by 3 internally; undo that so the
    // dry signal stays at unity and REV MIX 1.0 means wet at roughly unity.
    rp.wetLevel = p[fxReverbMix] / 3.0f;
    rp.dryLevel = 0.5f;
    rp.width = 1.0f;
    rp.freezeMode = 0.0f;
    reverb.setParameters (rp);
}

void FxChain::process (float* left, float* right, int numSamples)
{
    const bool crushOn = bits < 15.5f || holdFactor > 1.01f;
    const int delaySize = (int) delayL.size();

    for (int i = 0; i < numSamples; ++i)
    {
        float l = left[i], r = right[i];

        if (crushOn)
        {
            holdCounter -= 1.0f;
            if (holdCounter <= 0.0f)
            {
                const float steps = std::pow (2.0f, std::round (bits) - 1.0f);
                heldL = std::round (l * steps) / steps;
                heldR = std::round (r * steps) / steps;
                holdCounter += holdFactor;
            }
            l = heldL;
            r = heldR;
        }

        if (filterOn)
        {
            l = filterL.lowpass (l);
            r = filterR.lowpass (r);
        }

        if (delayOn)
        {
            // glide the delay time to avoid zipper clicks when it's dragged
            delaySamples += (delayTarget - delaySamples) * 0.001f;
            float readPos = (float) writePos - delaySamples;
            if (readPos < 0.0f)
                readPos += (float) delaySize;
            const int i0 = (int) readPos;
            const int i1 = (i0 + 1) % delaySize;
            const float frac = readPos - (float) i0;
            const float dl = delayL[(size_t) i0] + (delayL[(size_t) i1] - delayL[(size_t) i0]) * frac;
            const float dr = delayR[(size_t) i0] + (delayR[(size_t) i1] - delayR[(size_t) i0]) * frac;

            delayL[(size_t) writePos] = l + dl * feedback;
            delayR[(size_t) writePos] = r + dr * feedback;
            writePos = (writePos + 1) % delaySize;

            l += dl * delayMix;
            r += dr * delayMix;
        }

        left[i] = l;
        right[i] = r;
    }

    if (reverbOn)
        reverb.processStereo (left, right, numSamples);
}
} // namespace ms
