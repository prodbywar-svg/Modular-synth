#include "FxChain.h"

#include <algorithm>

namespace ms
{
namespace
{
constexpr float kMaxDelaySeconds = 4.0f;

float distort (int type, float x, float drive)
{
    const float d = x * drive;
    switch (type)
    {
        case 0:  return std::tanh (d);                    // SOFT
        case 1:  return std::clamp (d, -1.0f, 1.0f);      // HARD
        case 2:  return std::sin (d * kPi * 0.5f);        // FOLD
        default: return std::abs (std::tanh (d)) * 2.0f - 1.0f; // RECT (DC removed after)
    }
}
} // namespace

void FxChain::prepare (double sampleRate)
{
    sr = sampleRate;
    const auto size = (size_t) (kMaxDelaySeconds * sr) + 2;
    delayL.assign (size, 0.0f);
    delayR.assign (size, 0.0f);
    chorus.prepare (sr);
    grain.prepare (sr);
    reverb.prepare (sr);
    psychosis.prepare (sr);
    reset();
}

void FxChain::reset()
{
    holdCounter = 0.0f;
    heldL = heldR = 0.0f;
    distToneL.reset();
    distToneR.reset();
    dcInL = dcInR = dcOutL = dcOutR = 0.0f;
    filterL.reset();
    filterR.reset();
    std::fill (delayL.begin(), delayL.end(), 0.0f);
    std::fill (delayR.begin(), delayR.end(), 0.0f);
    writePos = 0;
    chorus.reset();
    grain.reset (99u);
    reverb.reset();
    psychosis.reset();
    delayOn = grainOn = reverbOn = psyOn = filterOn = distOn = chorusOn = false;
    firstParams = true;
    setParams (current, currentBpm > 0.0 ? currentBpm : 120.0);
    delaySamples = delayTarget;
}

void FxChain::setParams (const FxParams& p, double bpm)
{
    if (! firstParams && p == current && std::abs (bpm - currentBpm) < 1.0e-9)
        return;
    current = p;
    currentBpm = bpm;
    firstParams = false;

    bits = crushBits (p[fxCrushBits]);
    holdFactor = crushFactor (p[fxCrushRate]);

    distOn = p[fxDistMix] > 0.0001f;
    distType = (int) fxParamValue (fxDistType, p[fxDistType]);
    distDrive = fxParamValue (fxDistDrive, p[fxDistDrive]);
    distNorm = distType == 0 ? 1.0f / std::max (0.3f, std::tanh (distDrive)) : 1.0f;
    distMix = p[fxDistMix];
    distToneOn = p[fxDistTone] < 0.999f;
    distToneL.setup (fxParamValue (fxDistTone, p[fxDistTone]), 0.707f, sr);
    distToneR.setup (fxParamValue (fxDistTone, p[fxDistTone]), 0.707f, sr);

    filterOn = p[fxCutoff] < 0.999f || p[fxResonance] > 0.001f;
    filterL.setup (cutoffHz (p[fxCutoff]), resonanceQ (p[fxResonance]), sr);
    filterR.setup (cutoffHz (p[fxCutoff]), resonanceQ (p[fxResonance]), sr);

    const bool wantChorus = p[fxChorusMix] > 0.0001f;
    if (wantChorus && ! chorusOn)
        chorus.reset();
    chorusOn = wantChorus;
    chorus.set (p[fxChorusMode] >= 0.5f, fxParamValue (fxChorusRate, p[fxChorusRate]), p[fxChorusDepth], p[fxChorusMix]);

    const bool wantDelay = p[fxDelayMix] > 0.0001f;
    if (wantDelay && ! delayOn)
    {
        std::fill (delayL.begin(), delayL.end(), 0.0f);
        std::fill (delayR.begin(), delayR.end(), 0.0f);
    }
    delayOn = wantDelay;
    delayTarget = (float) std::clamp (fxTimeSeconds (p, fxDelayTime, bpm) * sr, 1.0, (double) delayL.size() - 2.0);
    feedback = delayFeedback (p[fxDelayFeedback]);
    delayMix = p[fxDelayMix];

    const bool wantGrain = p[fxGrainMix] > 0.0001f;
    if (wantGrain && ! grainOn)
        grain.reset (99u);
    grainOn = wantGrain;
    grain.set (fxTimeSeconds (p, fxGrainTime, bpm), fxTimeSeconds (p, fxGrainSize, bpm),
               fxParamValue (fxGrainPitch, p[fxGrainPitch]), p[fxGrainSpray],
               fxParamValue (fxGrainFeedback, p[fxGrainFeedback]), p[fxGrainMix]);

    const bool wantReverb = p[fxReverbMix] > 0.0001f;
    if (wantReverb && ! reverbOn)
        reverb.reset();
    reverbOn = wantReverb;
    reverb.set (fxTimeSeconds (p, fxReverbTime, bpm), p[fxReverbDamp], p[fxReverbMix]);

    const bool wantPsy = p[fxPsyMix] > 0.0001f;
    if (wantPsy && ! psyOn)
        psychosis.reset();
    psyOn = wantPsy;
    psychosis.set ((int) fxParamValue (fxPsyStages, p[fxPsyStages]), p[fxPsyDepth],
                   fxTimeSeconds (p, fxPsyTime, bpm), p[fxPsyMix]);
}

void FxChain::process (float* left, float* right, int numSamples)
{
    const bool crushOn = bits < 15.5f || holdFactor > 1.01f;
    const int delaySize = (int) delayL.size();
    const float dcCoeff = 1.0f - (float) (2.0 * kPi * 20.0 / sr);

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

        if (distOn)
        {
            float dl = distort (distType, l, distDrive) * distNorm;
            float dr = distort (distType, r, distDrive) * distNorm;
            if (distType == 3) // remove the DC the rectifier adds
            {
                const float ol = dl - dcInL + dcCoeff * dcOutL;
                const float orr = dr - dcInR + dcCoeff * dcOutR;
                dcInL = dl;
                dcInR = dr;
                dcOutL = dl = ol;
                dcOutR = dr = orr;
            }
            if (distToneOn)
            {
                dl = distToneL.lowpass (dl);
                dr = distToneR.lowpass (dr);
            }
            l = l * (1.0f - distMix) + dl * distMix * 0.5f;
            r = r * (1.0f - distMix) + dr * distMix * 0.5f;
        }

        if (filterOn)
        {
            l = filterL.lowpass (l);
            r = filterR.lowpass (r);
        }

        left[i] = l;
        right[i] = r;
    }

    if (chorusOn)
        chorus.process (left, right, numSamples);

    for (int i = 0; i < numSamples && delayOn; ++i)
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

        delayL[(size_t) writePos] = left[i] + dl * feedback;
        delayR[(size_t) writePos] = right[i] + dr * feedback;
        writePos = (writePos + 1) % delaySize;

        left[i] += dl * delayMix;
        right[i] += dr * delayMix;
    }

    if (grainOn)
        grain.process (left, right, numSamples);
    if (reverbOn)
        reverb.process (left, right, numSamples);
    if (psyOn)
        psychosis.process (left, right, numSamples);
}
} // namespace ms
