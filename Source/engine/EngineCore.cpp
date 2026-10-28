#include "EngineCore.h"

#include <algorithm>
#include <cmath>

namespace ms
{
void EngineCore::prepare (double sampleRate, int maxBlockSize)
{
    sr = sampleRate;
    maxBlock = std::max (1, maxBlockSize);
    voiceBuf.assign ((size_t) (kNumTracks * maxBlock), 0.0f);
    trackL.assign ((size_t) maxBlock, 0.0f);
    trackR.assign ((size_t) maxBlock, 0.0f);
    taps.assign ((size_t) (kNumTracks * maxBlock), 0.0f);

    for (auto& v : voices)
        v.prepare (sr);
    for (auto& fx : trackFx)
        fx.prepare (sr);
    masterFx.prepare (sr);

    reset();
}

void EngineCore::reset()
{
    for (int t = 0; t < kNumTracks; ++t)
    {
        voices[(size_t) t].reset ((std::uint32_t) (t + 1) * 7919u);
        trackFx[(size_t) t].setParams (project.tracks[(size_t) t].fx, project.bpm);
        trackFx[(size_t) t].reset();
    }
    masterFx.setParams (project.masterFx, project.bpm);
    masterFx.reset();

    step = 0;
    stepPhase = 0.0;
    stepFired = false;
}

void EngineCore::setState (const ProjectState& s)
{
    project = s;
}

void EngineCore::apply (const Command& c)
{
    if (c.type == CommandType::Trigger)
    {
        if (c.track >= 0 && c.track < kNumTracks)
        {
            voices[(size_t) c.track].trigger (project.tracks[(size_t) c.track], std::clamp (c.value, 0.0f, 1.0f));
            triggeredThisBlock[(size_t) c.track] = true;
        }
        return;
    }

    if (c.type == CommandType::SelectPattern && ! playing)
    {
        // nothing to wait for while stopped: switch straight away
        auto now = c;
        now.value = 0.0f;
        applyToState (project, now);
        return;
    }

    applyToState (project, c);
}

void EngineCore::setPlaying (bool shouldPlay)
{
    if (shouldPlay && ! playing)
    {
        if (project.queuedPattern >= 0)
        {
            project.pattern = project.queuedPattern;
            project.queuedPattern = -1;
        }
        step = 0;
        stepPhase = 0.0;
        stepFired = false;
    }
    playing = shouldPlay;
}

double EngineCore::samplesPerStep() const
{
    return sr * 60.0 / (double) project.bpm / 4.0;
}

void EngineCore::fireStep (int s)
{
    for (int t = 0; t < kNumTracks; ++t)
    {
        const float velocity = project.tracks[(size_t) t].patterns[(size_t) project.pattern][(size_t) s];
        if (velocity > 0.0f)
        {
            voices[(size_t) t].trigger (project.tracks[(size_t) t], velocity);
            triggeredThisBlock[(size_t) t] = true;
        }
    }
}

bool EngineCore::trackAudible (int t) const
{
    bool anySolo = false;
    for (const auto& tr : project.tracks)
        anySolo = anySolo || tr.solo;

    const auto& tr = project.tracks[(size_t) t];
    if (tr.mute)
        return false;
    return ! anySolo || tr.solo;
}

void EngineCore::process (float* left, float* right, int numSamples)
{
    for (int offset = 0; offset < numSamples; offset += maxBlock)
    {
        const int n = std::min (maxBlock, numSamples - offset);
        float* outL = left + offset;
        float* outR = right + offset;

        // 1. sequencer + voices, sample by sample so steps land sample-accurately
        const double stepLen = samplesPerStep();
        const double swingDelay = (double) project.swing * 0.5 * stepLen;

        for (int i = 0; i < n; ++i)
        {
            if (playing)
            {
                const double triggerAt = (step % 2 == 1) ? swingDelay : 0.0;
                if (! stepFired && stepPhase >= triggerAt)
                {
                    fireStep (step);
                    stepFired = true;
                }

                stepPhase += 1.0;
                if (stepPhase >= stepLen)
                {
                    stepPhase -= stepLen;
                    step = (step + 1) % kNumSteps;
                    stepFired = false;
                    // a queued pattern takes over exactly when the current one ends
                    if (step == 0 && project.queuedPattern >= 0)
                    {
                        project.pattern = project.queuedPattern;
                        project.queuedPattern = -1;
                    }
                }
            }

            for (int t = 0; t < kNumTracks; ++t)
                voiceBuf[(size_t) (t * maxBlock + i)] = voices[(size_t) t].next();
        }

        // 2. per-track pan, effects and level, summed into the output
        std::fill (outL, outL + n, 0.0f);
        std::fill (outR, outR + n, 0.0f);

        for (int t = 0; t < kNumTracks; ++t)
        {
            const auto& tr = project.tracks[(size_t) t];
            const float* mono = voiceBuf.data() + t * maxBlock;

            // equal-power pan
            const float angle = (tr.pan) * kPi * 0.5f;
            const float gl = std::cos (angle) * 1.41421356f;
            const float gr = std::sin (angle) * 1.41421356f;
            for (int i = 0; i < n; ++i)
            {
                trackL[(size_t) i] = mono[i] * gl;
                trackR[(size_t) i] = mono[i] * gr;
            }

            auto& fx = trackFx[(size_t) t];
            fx.setParams (tr.fx, project.bpm);
            fx.process (trackL.data(), trackR.data(), n);

            const float gain = levelToGain (tr.level);
            float peak = 0.0f;
            for (int i = 0; i < n; ++i)
            {
                trackL[(size_t) i] *= gain;
                trackR[(size_t) i] *= gain;
                peak = std::max ({ peak, std::abs (trackL[(size_t) i]), std::abs (trackR[(size_t) i]) });
            }

            const bool audible = trackAudible (t);
            peaks[(size_t) t] = audible ? peak : 0.0f;
            float* tap = taps.data() + t * maxBlock;
            for (int i = 0; i < n; ++i)
                tap[i] = audible ? 0.5f * (trackL[(size_t) i] + trackR[(size_t) i]) : 0.0f;

            if (audible && (soloRender < 0 || soloRender == t))
            {
                for (int i = 0; i < n; ++i)
                {
                    outL[i] += trackL[(size_t) i];
                    outR[i] += trackR[(size_t) i];
                }
            }
        }

        // 3. master effects and level
        masterFx.setParams (project.masterFx, project.bpm);
        masterFx.process (outL, outR, n);

        const float masterGain = levelToGain (project.masterLevel);
        float mp = 0.0f;
        for (int i = 0; i < n; ++i)
        {
            outL[i] *= masterGain;
            outR[i] *= masterGain;
            mp = std::max ({ mp, std::abs (outL[i]), std::abs (outR[i]) });
        }
        masterPeakValue = mp;
    }
}
} // namespace ms
