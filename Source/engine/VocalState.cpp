#include "VocalState.h"

#include <algorithm>
#include <cmath>

namespace ms
{
namespace
{
float clamp01 (float v) { return std::clamp (v, 0.0f, 1.0f); }

int nearestCountIn (int bars)
{
    int best = kCountIns[0];
    for (int c : kCountIns)
        if (std::abs (c - bars) < std::abs (best - bars))
            best = c;
    return best;
}

constexpr int kEdgeFade = 128; // samples, where a piece starts or ends (under 3 ms)
} // namespace

void VocalRecording::makePeaks()
{
    const auto windows = (size_t) ((audio.length + kPeakWindow - 1) / kPeakWindow);
    peaks.assign (windows, 0.0f);
    std::vector<float> l (kPeakWindow), r (kPeakWindow);
    for (size_t w = 0; w < windows; ++w)
    {
        audio.read ((std::int64_t) w * kPeakWindow, l.data(), r.data(), kPeakWindow);
        float p = 0.0f;
        for (int i = 0; i < kPeakWindow; ++i)
            p = std::max ({ p, std::abs (l[(size_t) i]), std::abs (r[(size_t) i]) });
        peaks[w] = p;
    }
}

void VocalClip::read (std::int64_t position, float* l, float* r, int n) const
{
    std::fill (l, l + n, 0.0f);
    std::fill (r, r + n, 0.0f);
    const auto to = position + n;
    for (const auto& s : segments)
    {
        if (s.end() <= position)
            continue;
        if (s.start >= to)
            break;
        const auto a = std::max (position, s.start), b = std::min (to, s.end());
        const int offset = (int) (a - position), count = (int) (b - a);
        s.source->audio.read (s.from + (a - s.start), l + offset, r + offset, count);
        // fade the edges of the piece, so a punch-in never clicks
        for (int i = 0; i < count; ++i)
        {
            const auto t = a + i;
            const auto edge = std::min (t - s.start, s.end() - 1 - t);
            if (edge >= kEdgeFade)
            {
                i = (int) std::max<std::int64_t> (i, s.end() - kEdgeFade - a - 1);
                continue;
            }
            const float g = (float) (edge + 1) / (float) (kEdgeFade + 1);
            l[offset + i] *= g;
            r[offset + i] *= g;
        }
    }
}

float VocalClip::peak (std::int64_t from, std::int64_t to) const
{
    float p = 0.0f;
    for (const auto& s : segments)
    {
        if (s.end() <= from)
            continue;
        if (s.start >= to)
            break;
        const auto& peaks = s.source->peaks;
        if (peaks.empty())
            continue;
        const auto a = std::max (from, s.start) - s.start + s.from, b = std::min (to, s.end()) - s.start + s.from;
        const auto w0 = (size_t) (a / VocalRecording::kPeakWindow);
        const auto w1 = std::min (peaks.size(), (size_t) ((b + VocalRecording::kPeakWindow - 1) / VocalRecording::kPeakWindow));
        for (auto w = w0; w < w1; ++w)
            p = std::max (p, peaks[w]);
    }
    return p;
}

VocalClip withSegment (const VocalClip* base, const VocalSegment& piece, std::shared_ptr<const VocalRecording> source)
{
    VocalClip out;
    if (base != nullptr)
    {
        // what lies outside the new piece stays, cut where it meets it
        for (const auto& s : base->segments)
        {
            if (s.end() <= piece.start || s.start >= piece.end())
            {
                out.segments.push_back (s);
                continue;
            }
            if (s.start < piece.start)
            {
                auto before = s;
                before.length = piece.start - s.start;
                out.segments.push_back (before);
            }
            if (s.end() > piece.end())
            {
                auto after = s;
                after.start = piece.end();
                after.from = s.from + (piece.end() - s.start);
                after.length = s.end() - piece.end();
                out.segments.push_back (after);
            }
        }
        // only the recordings still played are kept
        for (const auto& held : base->sources)
            for (const auto& s : out.segments)
                if (s.source == held.get())
                {
                    out.sources.push_back (held);
                    break;
                }
    }
    if (piece.length > 0 && piece.source != nullptr)
    {
        out.segments.push_back (piece);
        if (source != nullptr)
            out.sources.push_back (std::move (source));
    }
    std::sort (out.segments.begin(), out.segments.end(), [] (const VocalSegment& a, const VocalSegment& b) { return a.start < b.start; });
    return out;
}

VocalState makeDefaultVocal()
{
    return VocalState {};
}

void applyToVocal (VocalState& v, const Command& c)
{
    switch (c.type)
    {
        case CommandType::SetLoopInput:
            if (c.index >= 0 && c.index < (int) LoopInput::Count)
                v.input = (LoopInput) c.index;
            break;
        case CommandType::SetLoopMonitor:
            v.monitor = c.value > 0.5f;
            break;
        case CommandType::SetMonitorDry:
            v.monitorDry = c.value > 0.5f;
            break;
        case CommandType::SetMetronome:
            if (c.index >= 0 && c.index < kNumClickModes)
                v.metronome = c.index;
            break;
        case CommandType::SetCountIn:
            v.countIn = nearestCountIn ((int) std::lround (c.value));
            break;
        case CommandType::SetInputGain:
            v.inputGain = clamp01 (c.value);
            break;
        case CommandType::SetVocalClip:
            v.clip = c.clip;
            break;
        case CommandType::SetMasterLevel:
            v.masterLevel = clamp01 (c.value);
            break;
        case CommandType::SetPan:
            v.pan = clamp01 (c.value);
            break;
        case CommandType::SetFxParam:
            if (c.index >= 0 && c.index < kNumFxParams)
                v.masterFx[(size_t) c.index] = clamp01 (c.value);
            break;
        case CommandType::SetFxAuto:
        case CommandType::ClearFxAuto:
            applyFxAuto (v.masterFxAuto, c);
            break;
        case CommandType::SetBpm:
            v.bpm = std::clamp (c.value, kMinBpm, kMaxBpm);
            break;
        case CommandType::SetSwing:
            v.swing = clamp01 (c.value);
            break;
        case CommandType::Trigger:
        case CommandType::SetVoiceType:
        case CommandType::SetCharacter:
        case CommandType::SetVoiceParam:
        case CommandType::SetLevel:
        case CommandType::SetMute:
        case CommandType::SetSolo:
        case CommandType::SetStep:
        case CommandType::SelectPattern:
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
        case CommandType::SetLoopBars:
        case CommandType::SetSlice:
        case CommandType::SetLoopAudio:
        case CommandType::RecordLoop:
        case CommandType::CancelLoopRecord:
        case CommandType::RecordVocal:
        case CommandType::StopVocalRecord:
            break;
    }
}

#if ! MS_NO_JUCE
juce::var vocalToVar (const VocalState& v, const VocalFile& file)
{
    auto root = std::make_unique<juce::DynamicObject>();
    root->setProperty ("input", loopInputName (v.input));
    root->setProperty ("monitor", v.monitor);
    root->setProperty ("monitorDry", v.monitorDry);
    root->setProperty ("countIn", v.countIn);
    root->setProperty ("metronome", v.metronome);
    root->setProperty ("inputGain", v.inputGain);
    root->setProperty ("masterLevel", v.masterLevel);
    root->setProperty ("pan", v.pan);
    root->setProperty ("masterFx", fxToVar (v.masterFx));
    root->setProperty ("masterFxAuto", v.masterFxAuto.toVar());
    if (v.clip != nullptr && ! v.clip->empty() && file.file.isNotEmpty())
    {
        root->setProperty ("file", file.file);
        root->setProperty ("start", (juce::int64) file.start);
    }
    return juce::var (root.release());
}

bool vocalFromVar (const juce::var& v, VocalState& out, VocalFile& file)
{
    if (! v.isObject())
        return false;

    VocalState s = makeDefaultVocal();
    const auto input = v.getProperty ("input", {}).toString();
    for (int i = 0; i < (int) LoopInput::Count; ++i)
        if (input == loopInputName ((LoopInput) i))
            s.input = (LoopInput) i;
    s.monitor = (bool) v.getProperty ("monitor", false);
    s.monitorDry = (bool) v.getProperty ("monitorDry", false);
    s.countIn = nearestCountIn ((int) v.getProperty ("countIn", s.countIn));
    s.metronome = juce::jlimit (0, kNumClickModes - 1, (int) v.getProperty ("metronome", s.metronome));
    s.inputGain = clamp01 ((float) v.getProperty ("inputGain", s.inputGain));
    s.masterLevel = clamp01 ((float) v.getProperty ("masterLevel", s.masterLevel));
    s.pan = clamp01 ((float) v.getProperty ("pan", s.pan));
    fxFromVar (v.getProperty ("masterFx", {}), s.masterFx);
    s.masterFxAuto.fromVar (v.getProperty ("masterFxAuto", {}));

    file = {};
    file.file = v.getProperty ("file", {}).toString();
    file.start = std::max<std::int64_t> (0, (juce::int64) v.getProperty ("start", 0));
    out = s;
    return true;
}
#endif
} // namespace ms
