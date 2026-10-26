#pragma once

#include "DspUtil.h"
#include "Params.h"

#include <juce_audio_basics/juce_audio_basics.h>

#include <vector>

namespace ms
{
// Stereo effects rack: bitcrusher -> filter -> delay -> reverb. Used on every
// track and on the master. Each stage is skipped while it is switched off.
class FxChain
{
public:
    void prepare (double sampleRate);
    void reset();

    // Call once per block before process(); cheap when nothing changed.
    void setParams (const FxParams&);
    void process (float* left, float* right, int numSamples);

private:
    double sr = 48000.0;
    FxParams current {};
    bool firstParams = true;

    // bitcrusher
    float bits = 16.0f, holdFactor = 1.0f;
    float holdCounter = 0.0f, heldL = 0.0f, heldR = 0.0f;

    // filter
    bool filterOn = false;
    Svf filterL, filterR;

    // delay
    bool delayOn = false;
    std::vector<float> delayL, delayR;
    int writePos = 0;
    float delaySamples = 0.0f, delayTarget = 0.0f, feedback = 0.0f, delayMix = 0.0f;

    // reverb
    bool reverbOn = false;
    juce::Reverb reverb;
};
} // namespace ms
