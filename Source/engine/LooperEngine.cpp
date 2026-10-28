#include "LooperEngine.h"

#include <algorithm>
#include <cmath>

namespace ms
{
namespace
{
std::int64_t positiveMod (std::int64_t a, std::int64_t b)
{
    return ((a % b) + b) % b;
}

float balanceLeft (float pan) { return pan <= 0.5f ? 1.0f : 2.0f * (1.0f - pan); }
float balanceRight (float pan) { return pan >= 0.5f ? 1.0f : 2.0f * pan; }

// A sample between two, the loop wrapping round.
float readLoop (const std::vector<float>& buf, int length, double pos)
{
    const auto i0 = (int) pos;
    const auto frac = (float) (pos - (double) i0);
    const int a = i0 % length, b = (i0 + 1) % length;
    return buf[(size_t) a] + (buf[(size_t) b] - buf[(size_t) a]) * frac;
}
} // namespace

void LooperEngine::prepare (double sampleRate, int maxBlockSize)
{
    sr = sampleRate;
    maxBlock = std::max (1, maxBlockSize);
    bufL.assign ((size_t) maxBlock, 0.0f);
    bufR.assign ((size_t) maxBlock, 0.0f);
    dryL.assign ((size_t) maxBlock, 0.0f);
    dryR.assign ((size_t) maxBlock, 0.0f);
    taps.assign ((size_t) (kLooperTracks * maxBlock), 0.0f);
    clickBuf.assign ((size_t) maxBlock, 0.0f);
    fx.prepare (sr);
    reset();
}

void LooperEngine::reset()
{
    fx.setParams (looper.masterFx, looper.bpm);
    fx.reset();
    step = 0;
    stepPhase = 0.0;
    barCount = 0;
    barStart = playing;
    origin.fill (0);
    rec = {};
    clickAge = -1;
    stepChanged = true;
    peaks.fill (0.0f);
}

void LooperEngine::setState (const LooperState& s)
{
    looper = s;
    stepChanged = true;
}

double LooperEngine::samplesPerStep() const
{
    return sr * 60.0 / (double) looper.bpm / 4.0;
}

void LooperEngine::apply (const Command& c)
{
    if (c.type == CommandType::RecordLoop)
    {
        if (c.loop == nullptr || c.index < 0 || c.index >= kNumPatterns || c.loop->bars < 1)
            return;
        rec = {};
        rec.status = Status::Waiting;
        rec.slot = c.index;
        rec.overdub = c.value > 0.5f && c.loop->length > 0;
        rec.buffer = c.loop;
        rec.latency = std::max (0, c.track);
        rec.countLeft = looper.countIn;
        return;
    }
    if (c.type == CommandType::CancelLoopRecord)
    {
        rec = {};
        return;
    }
    if (c.type == CommandType::SelectPattern && ! playing)
    {
        auto now = c;
        now.value = 0.0f;
        applyToLooper (looper, now);
        stepChanged = true;
        return;
    }
    applyToLooper (looper, c);
    stepChanged = true;
}

void LooperEngine::setPlaying (bool shouldPlay)
{
    if (shouldPlay && ! playing)
    {
        if (looper.queuedPattern >= 0)
        {
            looper.pattern = looper.queuedPattern;
            looper.queuedPattern = -1;
        }
        step = 0;
        stepPhase = 0.0;
        barCount = 0;
        origin.fill (0);
        barStart = true;
        stepChanged = true;
    }
    if (! shouldPlay && playing && (rec.status == Status::Recording || rec.status == Status::CountIn))
        rec = {}; // a recording cut short by Stop is dropped
    playing = shouldPlay;
}

void LooperEngine::joinClock (int stepIndex, double phaseInSamples)
{
    setPlaying (true);
    step = ((stepIndex % kNumSteps) + kNumSteps) % kNumSteps;
    stepPhase = std::max (0.0, phaseInSamples);
    barStart = step == 0 && stepPhase <= 0.0;
}

int LooperEngine::loopBar (int slot, int bars) const
{
    return (int) positiveMod (barCount - origin[(size_t) slot], bars);
}

int LooperEngine::sourceStep (const LoopSlot& slot, int bars, std::int64_t loopStep) const
{
    const int total = bars * kNumSteps;
    const auto k = positiveMod (loopStep, total);
    const int slice = slot.slices[(size_t) (k % kNumSteps)];
    return slice < 0 ? -1 : (int) (k - k % kNumSteps) + slice;
}

double LooperEngine::recordProgress() const
{
    if (rec.status != Status::Recording || rec.buffer == nullptr)
        return 0.0;
    const double bars = (double) rec.buffer->bars;
    const double done = (double) (barCount - rec.startBar) + ((double) step + stepPhase / samplesPerStep()) / kNumSteps;
    return std::clamp (done / bars, 0.0, 1.0);
}

int LooperEngine::countInBeats() const
{
    if (rec.status != Status::CountIn)
        return 0;
    const int stepsLeft = rec.countLeft * kNumSteps + (kNumSteps - step);
    return (stepsLeft + 3) / 4;
}

double LooperEngine::loopPosition() const
{
    const auto& slot = looper.slots[(size_t) looper.pattern];
    if (! playing || slot.audio == nullptr || slot.audio->length <= 0)
        return -1.0;
    const int bars = slot.audio->bars;
    const double steps = (double) (loopBar (looper.pattern, bars) * kNumSteps + step) + stepPhase / samplesPerStep();
    return steps / (double) (bars * kNumSteps);
}

void LooperEngine::onBarStart()
{
    if (looper.queuedPattern >= 0)
    {
        looper.pattern = looper.queuedPattern;
        looper.queuedPattern = -1;
    }

    if (rec.status == Status::Recording && rec.endCount < 0 && barCount - rec.startBar >= rec.buffer->bars)
    {
        rec.endCount = rec.count;
        if (rec.latency == 0)
            finishRecording();
    }

    if ((rec.status == Status::Waiting || rec.status == Status::CountIn) && rec.countLeft > 0)
    {
        rec.status = Status::CountIn;
        --rec.countLeft;
    }
    else if (rec.status == Status::Waiting || rec.status == Status::CountIn)
    {
        rec.status = Status::Recording;
        rec.startBar = barCount;
        rec.count = 0;
        rec.endCount = -1;
    }
    stepChanged = true;
}

void LooperEngine::finishRecording()
{
    auto* buffer = rec.buffer;
    auto& slot = looper.slots[(size_t) rec.slot];
    if (! rec.overdub)
    {
        if (live)
            buffer->length = (int) std::min<std::int64_t> (rec.endCount, (std::int64_t) buffer->left.size());
        origin[(size_t) rec.slot] = rec.startBar;
    }
    if (buffer->length > 0)
    {
        slot.audio = buffer;
        ++finished;
        finishedSlot = rec.slot;
        finishedAudio = buffer;
    }
    rec = {};
    stepChanged = true;
}

void LooperEngine::process (float* left, float* right, int numSamples)
{
    for (int offset = 0; offset < numSamples; offset += maxBlock)
    {
        const int n = std::min (maxBlock, numSamples - offset);
        float* outL = left + offset;
        float* outR = right + offset;
        const float* blockInL = inL != nullptr ? inL + offset : nullptr;
        const float* blockInR = inR != nullptr ? inR + offset : nullptr;

        const double stepLen = samplesPerStep();
        // short fades where a step jumps to another slice, so it doesn't click
        const double fadeIn = std::min (0.001 * sr, stepLen * 0.25), fadeOut = std::min (0.002 * sr, stepLen * 0.25);
        const double autoPosition = (double) step + stepPhase / stepLen;
        const double autoRate = playing ? 1.0 / stepLen : 0.0;
        const int autoPattern = looper.pattern;
        const float inGain = levelToGain (looper.inputGain);
        const float panL = balanceLeft (looper.pan), panR = balanceRight (looper.pan);
        float* inTap = taps.data() + kInputTrack * maxBlock;
        float inPeak = 0.0f;

        for (int i = 0; i < n; ++i)
        {
            if (playing && barStart)
            {
                barStart = false;
                onBarStart();
            }

            // the input, as the looper hears it
            float il = 0.0f, ir = 0.0f;
            if (blockInL != nullptr && blockInR != nullptr)
            {
                switch (looper.input)
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

            // the loop playing, through its slice steps
            float ll = 0.0f, lr = 0.0f;
            const auto& slot = looper.slots[(size_t) looper.pattern];
            const auto* audio = slot.audio;
            if (playing && audio != nullptr && audio->length > 1)
            {
                const int bars = audio->bars, total = bars * kNumSteps;
                const auto k = (std::int64_t) loopBar (looper.pattern, bars) * kNumSteps + step;
                const int src = sourceStep (slot, bars, k);
                if (stepChanged)
                {
                    stepChanged = false;
                    const int before = sourceStep (slot, bars, k - 1), after = sourceStep (slot, bars, k + 1);
                    cutIn = src < 0 || before < 0 || (before + 1) % total != src;
                    cutOut = src < 0 || after < 0 || (src + 1) % total != after;
                    // a step that jumps to another slice is a hit
                    if (src >= 0 && cutIn && stepPhase < 1.0)
                        sliceStarted = true;
                }
                if (src >= 0)
                {
                    const double pos = ((double) src + stepPhase / stepLen) / (double) total * (double) audio->length;
                    float gain = 1.0f;
                    if (cutIn && stepPhase < fadeIn)
                        gain *= (float) (stepPhase / fadeIn);
                    if (cutOut && stepLen - stepPhase < fadeOut)
                        gain *= (float) std::max (0.0, (stepLen - stepPhase) / fadeOut);
                    ll = readLoop (audio->left, audio->length, pos) * gain;
                    lr = readLoop (audio->right, audio->length, pos) * gain;
                }
            }

            // recording: new loops fill in from the start, overdubs add
            // on where the loop is; both shifted back by the latency
            if (rec.status == Status::Recording)
            {
                auto* buffer = rec.buffer;
                if (live)
                {
                    if (! rec.overdub)
                    {
                        const auto index = rec.count - rec.latency;
                        if (index >= 0 && index < (std::int64_t) buffer->left.size())
                        {
                            buffer->left[(size_t) index] = il;
                            buffer->right[(size_t) index] = ir;
                        }
                    }
                    else if (buffer->length > 0)
                    {
                        const int bars = buffer->bars;
                        const double steps = (double) (loopBar (rec.slot, bars) * kNumSteps + step) + stepPhase / stepLen;
                        const auto at = (std::int64_t) (steps / (double) (bars * kNumSteps) * (double) buffer->length);
                        const auto index = (size_t) positiveMod (at - rec.latency, buffer->length);
                        buffer->left[index] += il;
                        buffer->right[index] += ir;
                    }
                }
                ++rec.count;
                if (rec.endCount >= 0 && rec.count >= rec.endCount + rec.latency)
                    finishRecording();
            }

            // the input heard through the rack goes in here; heard dry, it
            // waits in the taps and joins after the rack
            const float throughRack = looper.monitor && ! looper.monitorDry ? 1.0f : 0.0f;
            bufL[(size_t) i] = (ll + il * throughRack) * panL;
            bufR[(size_t) i] = (lr + ir * throughRack) * panR;
            dryL[(size_t) i] = looper.monitor && looper.monitorDry ? il * panL : 0.0f;
            dryR[(size_t) i] = looper.monitor && looper.monitorDry ? ir * panR : 0.0f;

            // the count-in: a click on every beat, higher on the bar
            if (live && rec.status == Status::CountIn && step % 4 == 0 && stepPhase < 1.0)
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
                stepPhase += 1.0;
                if (stepPhase >= stepLen)
                {
                    stepPhase -= stepLen;
                    stepChanged = true;
                    if (++step >= kNumSteps)
                    {
                        step = 0;
                        ++barCount;
                        barStart = true;
                    }
                }
            }
        }

        fx.processAutomated (looper.masterFx, looper.masterFxAuto, autoPattern, autoPosition, autoRate, looper.bpm,
                             bufL.data(), bufR.data(), n);

        const float gain = levelToGain (looper.masterLevel);
        float peak = 0.0f;
        float* loopTap = taps.data() + kLoopTrack * maxBlock;
        for (int i = 0; i < n; ++i)
        {
            outL[i] = (bufL[(size_t) i] + dryL[(size_t) i]) * gain;
            outR[i] = (bufR[(size_t) i] + dryR[(size_t) i]) * gain;
            loopTap[i] = 0.5f * (outL[i] + outR[i]);
            peak = std::max ({ peak, std::abs (outL[i]), std::abs (outR[i]) });
            outL[i] += clickBuf[(size_t) i];
            outR[i] += clickBuf[(size_t) i];
        }
        peaks[kLoopTrack] = peak;
        peaks[kInputTrack] = inPeak;
    }
    inL = inR = nullptr;
}
} // namespace ms
