#include "LooperState.h"

#include <algorithm>
#include <cmath>
#include <cstring>

namespace ms
{
namespace
{
float clamp01 (float v) { return std::clamp (v, 0.0f, 1.0f); }

int nearestLoopLength (int bars)
{
    int best = kLoopLengths[0];
    for (int l : kLoopLengths)
        if (std::abs (l - bars) < std::abs (best - bars))
            best = l;
    return best;
}

int nearestCountIn (int bars)
{
    int best = kCountIns[0];
    for (int c : kCountIns)
        if (std::abs (c - bars) < std::abs (best - bars))
            best = c;
    return best;
}
} // namespace

bool InputRecording::append (const float* l, const float* r, int n, const std::function<float*()>& next)
{
    for (int done = 0; done < n;)
    {
        const auto within = (int) (length % kChunkSamples);
        if (within == 0 && (std::int64_t) chunks.size() * kChunkSamples <= length)
        {
            float* chunk = chunks.size() < (size_t) kMaxChunks ? next() : nullptr;
            if (chunk == nullptr)
                return false;
            chunks.push_back (chunk); // reserved: never allocates
        }
        float* chunk = chunks.back();
        const int count = std::min (n - done, kChunkSamples - within);
        std::memcpy (chunk + within, l + done, sizeof (float) * (size_t) count);
        std::memcpy (chunk + kChunkSamples + within, r + done, sizeof (float) * (size_t) count);
        done += count;
        length += count;
    }
    return true;
}

void InputRecording::read (std::int64_t position, float* l, float* r, int n) const
{
    for (int i = 0; i < n;)
    {
        const auto pos = position + i;
        if (pos < 0 || pos >= length)
        {
            l[i] = r[i] = 0.0f;
            ++i;
            continue;
        }
        const auto* chunk = chunks[(size_t) (pos / kChunkSamples)];
        const auto within = (int) (pos % kChunkSamples);
        const int count = (int) std::min<std::int64_t> ({ (std::int64_t) (n - i), (std::int64_t) (kChunkSamples - within), length - pos });
        std::memcpy (l + i, chunk + within, sizeof (float) * (size_t) count);
        std::memcpy (r + i, chunk + kChunkSamples + within, sizeof (float) * (size_t) count);
        i += count;
    }
}

LooperState makeDefaultLooper()
{
    return LooperState {};
}

const char* loopInputName (LoopInput i)
{
    switch (i)
    {
        case LoopInput::In1: return "IN 1";
        case LoopInput::In2: return "IN 2";
        case LoopInput::Stereo:
        case LoopInput::Count: break;
    }
    return "IN 1+2";
}

bool straightSlices (const SlicePattern& s)
{
    return s == LoopSlot::straightSlices();
}

void applyToLooper (LooperState& l, const Command& c)
{
    const int slot = c.index / kNumSteps, step = c.index % kNumSteps;
    const bool validSlot = c.index >= 0 && c.index < kNumPatterns;

    switch (c.type)
    {
        case CommandType::SetLoopInput:
            if (c.index >= 0 && c.index < (int) LoopInput::Count)
                l.input = (LoopInput) c.index;
            break;
        case CommandType::SetLoopMonitor:
            l.monitor = c.value > 0.5f;
            break;
        case CommandType::SetMonitorDry:
            l.monitorDry = c.value > 0.5f;
            break;
        case CommandType::SetCountIn:
            l.countIn = nearestCountIn ((int) std::lround (c.value));
            break;
        case CommandType::SetInputGain:
            l.inputGain = clamp01 (c.value);
            break;
        case CommandType::SetLoopBars:
            if (validSlot)
                l.slots[(size_t) c.index].bars = nearestLoopLength ((int) std::lround (c.value));
            break;
        case CommandType::SetSlice:
            if (c.index >= 0 && slot < kNumPatterns)
                l.slots[(size_t) slot].slices[(size_t) step] = (std::int8_t) std::clamp ((int) std::lround (c.value), kMuteSlice, kNumSteps - 1);
            break;
        case CommandType::SetLoopAudio:
            if (validSlot)
                l.slots[(size_t) c.index].audio = c.loop;
            break;
        case CommandType::SetMasterLevel:
            l.masterLevel = clamp01 (c.value);
            break;
        case CommandType::SetPan:
            l.pan = clamp01 (c.value);
            break;
        case CommandType::SetFxParam:
            if (c.index >= 0 && c.index < kNumFxParams)
                l.masterFx[(size_t) c.index] = clamp01 (c.value);
            break;
        case CommandType::SetFxAuto:
        case CommandType::ClearFxAuto:
            applyFxAuto (l.masterFxAuto, c);
            break;
        case CommandType::SelectPattern:
            if (c.index < 0 || c.index >= kNumPatterns)
                break;
            if (c.value > 0.5f)
                l.queuedPattern = c.index == l.pattern ? -1 : c.index;
            else
            {
                l.pattern = c.index;
                l.queuedPattern = -1;
            }
            break;
        case CommandType::SetBpm:
            l.bpm = std::clamp (c.value, kMinBpm, kMaxBpm);
            break;
        case CommandType::SetSwing:
            l.swing = clamp01 (c.value);
            break;
        case CommandType::Trigger:
        case CommandType::SetVoiceType:
        case CommandType::SetCharacter:
        case CommandType::SetVoiceParam:
        case CommandType::SetLevel:
        case CommandType::SetMute:
        case CommandType::SetSolo:
        case CommandType::SetStep:
        case CommandType::SetSample:
        case CommandType::Play:
        case CommandType::Stop:
        case CommandType::RecordStart:
        case CommandType::RecordStop:
        case CommandType::AddTool:
        case CommandType::RemoveTool:
        case CommandType::SetToolMute:
        case CommandType::SetToolSolo:
        case CommandType::NoteOff:
        case CommandType::SetNote:
        case CommandType::SetLength:
        case CommandType::SetCable:
        case CommandType::SetKey:
        case CommandType::SetScale:
        case CommandType::RecordLoop:
        case CommandType::CancelLoopRecord:
            break;
    }
}

#if ! MS_NO_JUCE
juce::var looperToVar (const LooperState& l, const LoopFiles& files)
{
    auto root = std::make_unique<juce::DynamicObject>();
    root->setProperty ("pattern", l.pattern);
    root->setProperty ("input", loopInputName (l.input));
    root->setProperty ("monitor", l.monitor);
    root->setProperty ("monitorDry", l.monitorDry);
    root->setProperty ("countIn", l.countIn);
    root->setProperty ("inputGain", l.inputGain);
    root->setProperty ("masterLevel", l.masterLevel);
    root->setProperty ("pan", l.pan);
    root->setProperty ("masterFx", fxToVar (l.masterFx));
    root->setProperty ("masterFxAuto", l.masterFxAuto.toVar());

    juce::Array<juce::var> slots;
    for (int p = 0; p < kNumPatterns; ++p)
    {
        const auto& s = l.slots[(size_t) p];
        auto o = std::make_unique<juce::DynamicObject>();
        o->setProperty ("bars", s.bars);
        juce::Array<juce::var> slices;
        for (auto v : s.slices)
            slices.add ((int) v);
        o->setProperty ("slices", slices);
        if (s.audio != nullptr && files[(size_t) p].file.isNotEmpty())
        {
            o->setProperty ("file", files[(size_t) p].file);
            o->setProperty ("loopBars", s.audio->bars);
            o->setProperty ("loopBpm", s.audio->bpm);
        }
        slots.add (juce::var (o.release()));
    }
    root->setProperty ("slots", slots);
    return juce::var (root.release());
}

bool looperFromVar (const juce::var& v, LooperState& out, LoopFiles& files)
{
    if (! v.isObject())
        return false;

    LooperState l = makeDefaultLooper();
    l.pattern = juce::jlimit (0, kNumPatterns - 1, (int) v.getProperty ("pattern", 0));
    const auto input = v.getProperty ("input", {}).toString();
    for (int i = 0; i < (int) LoopInput::Count; ++i)
        if (input == loopInputName ((LoopInput) i))
            l.input = (LoopInput) i;
    l.monitor = (bool) v.getProperty ("monitor", false);
    l.monitorDry = (bool) v.getProperty ("monitorDry", false);
    l.countIn = nearestCountIn ((int) v.getProperty ("countIn", l.countIn));
    l.inputGain = clamp01 ((float) v.getProperty ("inputGain", l.inputGain));
    l.masterLevel = clamp01 ((float) v.getProperty ("masterLevel", l.masterLevel));
    l.pan = clamp01 ((float) v.getProperty ("pan", l.pan));
    fxFromVar (v.getProperty ("masterFx", {}), l.masterFx);
    l.masterFxAuto.fromVar (v.getProperty ("masterFxAuto", {}));

    files = {};
    if (auto* slots = v.getProperty ("slots", {}).getArray())
    {
        for (int p = 0; p < juce::jmin (kNumPatterns, slots->size()); ++p)
        {
            const auto& o = (*slots)[p];
            auto& s = l.slots[(size_t) p];
            s.bars = nearestLoopLength ((int) o.getProperty ("bars", s.bars));
            if (auto* slices = o.getProperty ("slices", {}).getArray())
                for (int i = 0; i < juce::jmin (kNumSteps, slices->size()); ++i)
                    s.slices[(size_t) i] = (std::int8_t) juce::jlimit (kMuteSlice, kNumSteps - 1, (int) (*slices)[i]);
            auto& f = files[(size_t) p];
            f.file = o.getProperty ("file", {}).toString();
            f.bars = nearestLoopLength ((int) o.getProperty ("loopBars", s.bars));
            f.bpm = juce::jlimit (kMinBpm, kMaxBpm, (float) o.getProperty ("loopBpm", 132.0f));
        }
    }
    out = l;
    return true;
}
#endif
} // namespace ms
