#include "BassEngine.h"

#include <algorithm>
#include <cmath>

namespace ms
{
void BassEngine::prepare (double sampleRate, int maxBlockSize)
{
    sr = sampleRate;
    maxBlock = std::max (1, maxBlockSize);
    voiceBuf.assign ((size_t) (kBassRows * maxBlock), 0.0f);
    rowL.assign ((size_t) maxBlock, 0.0f);
    rowR.assign ((size_t) maxBlock, 0.0f);
    taps.assign ((size_t) (kBassRows * maxBlock), 0.0f);

    for (auto& v : voices)
        v.prepare (sr);
    for (auto& fx : rowFx)
        fx.prepare (sr);
    busFx.prepare (sr);
    reset();
}

void BassEngine::reset()
{
    for (int r = 0; r < kBassRows; ++r)
    {
        voices[(size_t) r].reset ((std::uint32_t) (r + 1) * 104729u);
        rowFx[(size_t) r].setParams (bass.rows[(size_t) r].fx, bass.bpm);
        rowFx[(size_t) r].reset();
    }
    busFx.setParams (bass.masterFx, bass.bpm);
    busFx.reset();
    step = 0;
    stepPhase = 0.0;
    stepFired = false;
}

void BassEngine::setState (const BassState& s)
{
    bass = s;
    groups = cutGroups (bass);
}

void BassEngine::play (int row, int note, float velocity, double gateSamples)
{
    auto& voice = voices[(size_t) row];
    voice.setRow (bass.rows[(size_t) row]);
    // rows joined by cables cut each other
    for (int other = 0; other < kBassRows; ++other)
        if (other != row && groups[(size_t) other] == groups[(size_t) row])
            voices[(size_t) other].cut();
    voice.noteOn (note, std::clamp (velocity, 0.0f, 1.0f), gateSamples);
    triggeredThisBlock[(size_t) row] = true;
}

void BassEngine::apply (const Command& c)
{
    const bool validRow = c.track >= 0 && c.track < kBassRows;
    if (c.type == CommandType::Trigger)
    {
        if (validRow)
            play (c.track, snapToScale (c.index, bass.key, bass.scale), c.value, -1.0);
        return;
    }
    if (c.type == CommandType::NoteOff)
    {
        if (validRow)
            voices[(size_t) c.track].noteOff();
        return;
    }
    if (c.type == CommandType::SelectPattern && ! playing)
    {
        auto now = c;
        now.value = 0.0f;
        applyToBass (bass, now);
        return;
    }

    applyToBass (bass, c);
    if (c.type == CommandType::SetCable)
        groups = cutGroups (bass);
}

void BassEngine::setPlaying (bool shouldPlay)
{
    if (shouldPlay && ! playing)
    {
        if (bass.queuedPattern >= 0)
        {
            bass.pattern = bass.queuedPattern;
            bass.queuedPattern = -1;
        }
        step = 0;
        stepPhase = 0.0;
        stepFired = false;
    }
    if (! shouldPlay && playing)
        for (auto& v : voices)
            v.noteOff();
    playing = shouldPlay;
}

void BassEngine::joinClock (int stepIndex, double phaseInSamples)
{
    setPlaying (true);
    step = ((stepIndex % kNumSteps) + kNumSteps) % kNumSteps;
    stepPhase = std::max (0.0, phaseInSamples);
    const double triggerAt = (step % 2 == 1) ? (double) bass.swing * 0.5 * samplesPerStep() : 0.0;
    stepFired = stepPhase > triggerAt;
}

double BassEngine::samplesPerStep() const
{
    return sr * 60.0 / (double) bass.bpm / 4.0;
}

void BassEngine::fireStep (int s)
{
    const double stepLen = samplesPerStep();
    for (int r = 0; r < kBassRows; ++r)
    {
        const auto& row = bass.rows[(size_t) r];
        const float velocity = row.velocity[(size_t) bass.pattern][(size_t) s];
        if (velocity <= 0.0f)
            continue;
        // the note holds for its length, letting go a little before the end
        // of its last step so the next note starts the envelope again
        const int length = row.lengths[(size_t) bass.pattern][(size_t) s];
        play (r, row.notes[(size_t) bass.pattern][(size_t) s], velocity, ((double) length - 0.15) * stepLen);
    }
}

bool BassEngine::rowAudible (int r) const
{
    bool anySolo = false;
    for (const auto& row : bass.rows)
        anySolo = anySolo || row.solo;
    const auto& row = bass.rows[(size_t) r];
    return ! row.mute && (! anySolo || row.solo);
}

void BassEngine::process (float* left, float* right, int numSamples)
{
    for (int offset = 0; offset < numSamples; offset += maxBlock)
    {
        const int n = std::min (maxBlock, numSamples - offset);
        float* outL = left + offset;
        float* outR = right + offset;

        for (int r = 0; r < kBassRows; ++r)
            voices[(size_t) r].setRow (bass.rows[(size_t) r]);

        // 1. sequencer + voices, sample by sample so notes land sample-accurately
        const double stepLen = samplesPerStep();
        const double swingDelay = (double) bass.swing * 0.5 * stepLen;
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
                    if (step == 0 && bass.queuedPattern >= 0)
                    {
                        bass.pattern = bass.queuedPattern;
                        bass.queuedPattern = -1;
                    }
                }
            }
            for (int r = 0; r < kBassRows; ++r)
                voiceBuf[(size_t) (r * maxBlock + i)] = voices[(size_t) r].next();
        }

        // 2. per-row pan, effects and level, summed
        std::fill (outL, outL + n, 0.0f);
        std::fill (outR, outR + n, 0.0f);
        for (int r = 0; r < kBassRows; ++r)
        {
            const auto& row = bass.rows[(size_t) r];
            const float* mono = voiceBuf.data() + r * maxBlock;
            const float angle = row.pan * kPi * 0.5f;
            const float gl = std::cos (angle) * 1.41421356f;
            const float gr = std::sin (angle) * 1.41421356f;
            for (int i = 0; i < n; ++i)
            {
                rowL[(size_t) i] = mono[i] * gl;
                rowR[(size_t) i] = mono[i] * gr;
            }

            auto& fx = rowFx[(size_t) r];
            fx.setParams (row.fx, bass.bpm);
            fx.process (rowL.data(), rowR.data(), n);

            const float gain = levelToGain (row.level);
            float peak = 0.0f;
            for (int i = 0; i < n; ++i)
            {
                rowL[(size_t) i] *= gain;
                rowR[(size_t) i] *= gain;
                peak = std::max ({ peak, std::abs (rowL[(size_t) i]), std::abs (rowR[(size_t) i]) });
            }

            const bool audible = rowAudible (r);
            peaks[(size_t) r] = audible ? peak : 0.0f;
            float* tap = taps.data() + r * maxBlock;
            for (int i = 0; i < n; ++i)
                tap[i] = audible ? 0.5f * (rowL[(size_t) i] + rowR[(size_t) i]) : 0.0f;

            if (audible && (soloRender < 0 || soloRender == r))
            {
                for (int i = 0; i < n; ++i)
                {
                    outL[i] += rowL[(size_t) i];
                    outR[i] += rowR[(size_t) i];
                }
            }
        }

        // 3. bass bus effects and level
        busFx.setParams (bass.masterFx, bass.bpm);
        busFx.process (outL, outR, n);
        const float busGain = levelToGain (bass.masterLevel);
        float mp = 0.0f;
        for (int i = 0; i < n; ++i)
        {
            outL[i] *= busGain;
            outR[i] *= busGain;
            mp = std::max ({ mp, std::abs (outL[i]), std::abs (outR[i]) });
        }
        masterPeakValue = mp;
    }
}
} // namespace ms
