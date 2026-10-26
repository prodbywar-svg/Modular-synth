#pragma once

#include "Effects.h"
#include "Params.h"

#include <vector>

namespace ms
{
// Stereo effects rack, in order: bitcrusher -> distortion -> filter -> delay ->
// grain delay -> reverb -> Psychosis. Used on every track and on the master.
// Each stage is skipped while it is switched off.
class FxChain
{
public:
    void prepare (double sampleRate);
    void reset();

    // Call once per block before process(); cheap when nothing changed. The
    // tempo is needed for note-synced times.
    void setParams (const FxParams&, double bpm);
    void process (float* left, float* right, int numSamples);

private:
    double sr = 48000.0;
    FxParams current {};
    double currentBpm = 0.0;
    bool firstParams = true;

    // bitcrusher
    float bits = 16.0f, holdFactor = 1.0f;
    float holdCounter = 0.0f, heldL = 0.0f, heldR = 0.0f;

    // distortion
    bool distOn = false, distToneOn = false;
    int distType = 0;
    float distDrive = 1.0f, distMix = 0.0f, distNorm = 1.0f;
    Svf distToneL, distToneR;
    float dcInL = 0.0f, dcInR = 0.0f, dcOutL = 0.0f, dcOutR = 0.0f;

    // filter
    bool filterOn = false;
    Svf filterL, filterR;

    // delay
    bool delayOn = false;
    std::vector<float> delayL, delayR;
    int writePos = 0;
    float delaySamples = 0.0f, delayTarget = 0.0f, feedback = 0.0f, delayMix = 0.0f;

    bool grainOn = false;
    GrainDelay grain;

    bool reverbOn = false;
    FdnReverb reverb;

    bool psyOn = false;
    Psychosis psychosis;
};
} // namespace ms
