#include "VocalEngine.h"

#include <algorithm>
#include <cmath>

namespace ms
{
namespace
{
float balanceLeft (float pan) { return pan <= 0.5f ? 1.0f : 2.0f * (1.0f - pan); }
float balanceRight (float pan) { return pan >= 0.5f ? 1.0f : 2.0f * pan; }
} // namespace

void VocalEngine::prepare (double sampleRate, int maxBlockSize)
{
    sr = sampleRate;
    maxBlock = std::max (1, maxBlockSize);
    for (auto* b : { &bufL, &bufR, &dryL, &dryR, &clipL, &clipR, &recL, &recR, &clickBuf })
        b->assign ((size_t) maxBlock, 0.0f);
    taps.assign ((size_t) (kVocalTracks * maxBlock), 0.0f);
    fx.prepare (sr);
    reset();
}

void VocalEngine::reset()
{
    fx.setParams (vocal.masterFx, vocal.bpm);
    fx.reset();
    step = 0;
    stepPhase = 0.0;
    timeline = 0;
    barStart = playing;
    rec = {};
    clickAge = -1;
    peaks.fill (0.0f);
}

void VocalEngine::setState (const VocalState& s)
{
    vocal = s;
}

double VocalEngine::samplesPerStep() const
{
    return sr * 60.0 / (double) vocal.bpm / 4.0;
}

void VocalEngine::apply (const Command& c)
{
    if (c.type == CommandType::RecordVocal)
    {
        if (c.vocal == nullptr)
            return;
        rec = {};
        rec.status = Status::Waiting;
        rec.buffer = c.vocal;
        rec.latency = std::max (0, c.track);
        rec.countLeft = vocal.countIn;
        return;
    }
    if (c.type == CommandType::StopVocalRecord)
    {
        finishRecording();
        return;
    }
    applyToVocal (vocal, c);
}

void VocalEngine::setPlaying (bool shouldPlay)
{
    if (shouldPlay && ! playing)
    {
        step = 0;
        stepPhase = 0.0;
        timeline = 0;
        barStart = true;
    }
    if (! shouldPlay && playing)
        finishRecording(); // what was sung up to Stop is kept
    playing = shouldPlay;
}

void VocalEngine::joinClock (int stepIndex, double phaseInSamples, std::int64_t position)
{
    setPlaying (true);
    step = ((stepIndex % kNumSteps) + kNumSteps) % kNumSteps;
    stepPhase = std::max (0.0, phaseInSamples);
    timeline = std::max<std::int64_t> (0, position);
    barStart = step == 0 && stepPhase <= 0.0;
}

int VocalEngine::countInBeats() const
{
    if (rec.status != Status::CountIn)
        return 0;
    const int stepsLeft = rec.countLeft * kNumSteps + (kNumSteps - step);
    return (stepsLeft + 3) / 4;
}

void VocalEngine::onBarStart()
{
    if ((rec.status == Status::Waiting || rec.status == Status::CountIn) && rec.countLeft > 0)
    {
        rec.status = Status::CountIn;
        --rec.countLeft;
    }
    else if (rec.status == Status::Waiting || rec.status == Status::CountIn)
    {
        rec.status = Status::Recording;
        rec.start = timeline;
        rec.count = 0;
    }
}

void VocalEngine::finishRecording()
{
    if (rec.status == Status::Recording && rec.buffer != nullptr && rec.count > rec.latency)
    {
        lastDone.recording = rec.buffer;
        lastDone.segment.source = rec.buffer;
        lastDone.segment.start = rec.start;
        lastDone.segment.from = rec.latency;
        lastDone.segment.length = live ? std::min<std::int64_t> (rec.count, rec.buffer->audio.length) - rec.latency : rec.count - rec.latency;
        if (lastDone.segment.length > 0)
            ++finished;
    }
    rec = {};
}

void VocalEngine::process (float* left, float* right, int numSamples)
{
    for (int offset = 0; offset < numSamples; offset += maxBlock)
    {
        const int n = std::min (maxBlock, numSamples - offset);
        float* outL = left + offset;
        float* outR = right + offset;
        const float* blockInL = inL != nullptr ? inL + offset : nullptr;
        const float* blockInR = inR != nullptr ? inR + offset : nullptr;

        const double stepLen = samplesPerStep();
        const double autoPosition = (double) step + stepPhase / stepLen;
        const double autoRate = playing ? 1.0 / stepLen : 0.0;
        const float inGain = levelToGain (vocal.inputGain);
        const float panL = balanceLeft (vocal.pan), panR = balanceRight (vocal.pan);
        float* inTap = taps.data() + kVocalInputTrack * maxBlock;
        float inPeak = 0.0f;
        int recorded = 0; // samples of this block for the recording

        // the clip, where the song is (silent while it is being recorded over)
        if (playing && vocal.clip != nullptr)
            vocal.clip->read (timeline, clipL.data(), clipR.data(), n);
        else
        {
            std::fill (clipL.begin(), clipL.begin() + n, 0.0f);
            std::fill (clipR.begin(), clipR.begin() + n, 0.0f);
        }

        for (int i = 0; i < n; ++i)
        {
            if (playing && barStart)
            {
                barStart = false;
                onBarStart();
            }

            // the input, as the track hears it
            float il = 0.0f, ir = 0.0f;
            if (blockInL != nullptr && blockInR != nullptr)
            {
                switch (vocal.input)
                {
                    case LoopInput::In1: il = ir = blockInL[i]; break;
                    case LoopInput::In2: il = ir = blockInR[i]; break;
                    case LoopInput::Stereo:
                    case LoopInput::Count: il = blockInL[i]; ir = blockInR[i]; break;
                }
                il *= inGain;
                ir *= inGain;
            }
            inTap[i] = 0.5f * (il + ir);
            inPeak = std::max ({ inPeak, std::abs (il), std::abs (ir) });

            float vl = clipL[(size_t) i], vr = clipR[(size_t) i];
            bool monitor = vocal.monitor;
            if (rec.status == Status::Recording)
            {
                vl = vr = 0.0f;
                if (live)
                {
                    recL[(size_t) recorded] = il;
                    recR[(size_t) recorded] = ir;
                    ++recorded;
                }
                else
                    monitor = true; // played back, the take is heard as it will play
                ++rec.count;
            }

            // the input heard through the rack goes in here; heard dry, it
            // waits and joins after the rack
            const bool dry = monitor && vocal.monitorDry;
            const float throughRack = monitor && ! dry ? 1.0f : 0.0f;
            bufL[(size_t) i] = (vl + il * throughRack) * panL;
            bufR[(size_t) i] = (vr + ir * throughRack) * panR;
            dryL[(size_t) i] = dry ? il * panL : 0.0f;
            dryR[(size_t) i] = dry ? ir * panR : 0.0f;

            // the count-in and the metronome: a click on every beat, higher on the bar
            const bool clicking = rec.status == Status::CountIn || vocal.metronome == kClickPlaying
                                  || (vocal.metronome == kClickRecording && rec.status == Status::Recording);
            if (live && playing && clicking && step % 4 == 0 && stepPhase < 1.0)
            {
                clickAge = 0;
                clickAccent = step == 0;
            }
            float click = 0.0f;
            if (clickAge >= 0)
            {
                const double t = (double) clickAge / sr;
                const double freq = clickAccent ? 1760.0 : 1320.0;
                click = (float) (std::sin (2.0 * 3.141592653589793 * freq * t) * std::exp (-t / 0.008)) * (clickAccent ? 0.35f : 0.25f);
                if (++clickAge > (int) (0.06 * sr))
                    clickAge = -1;
            }
            clickBuf[(size_t) i] = click;

            if (playing)
            {
                ++timeline;
                stepPhase += 1.0;
                if (stepPhase >= stepLen)
                {
                    stepPhase -= stepLen;
                    if (++step >= kNumSteps)
                    {
                        step = 0;
                        barStart = true;
                    }
                }
            }
        }

        // into the recording; out of room ends it, keeping what it has
        if (recorded > 0 && rec.buffer != nullptr)
        {
            static const std::function<float*()> none = [] { return (float*) nullptr; };
            if (! rec.buffer->audio.append (recL.data(), recR.data(), recorded, chunkSource ? chunkSource : none))
                finishRecording();
        }

        fx.processAutomated (vocal.masterFx, vocal.masterFxAuto, 0, autoPosition, autoRate, vocal.bpm, bufL.data(), bufR.data(), n);

        const float gain = levelToGain (vocal.masterLevel);
        float peak = 0.0f;
        float* outTap = taps.data() + kVocalTrack * maxBlock;
        for (int i = 0; i < n; ++i)
        {
            outL[i] = (bufL[(size_t) i] + dryL[(size_t) i]) * gain;
            outR[i] = (bufR[(size_t) i] + dryR[(size_t) i]) * gain;
            outTap[i] = 0.5f * (outL[i] + outR[i]);
            peak = std::max ({ peak, std::abs (outL[i]), std::abs (outR[i]) });
            outL[i] += clickBuf[(size_t) i];
            outR[i] += clickBuf[(size_t) i];
        }
        peaks[kVocalTrack] = peak;
        peaks[kVocalInputTrack] = inPeak;
    }
    inL = inR = nullptr;
}
} // namespace ms
