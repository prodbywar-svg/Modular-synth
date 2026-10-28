#include "StudioState.h"

#include <algorithm>

namespace ms
{
namespace
{
float clamp01 (float v) { return std::clamp (v, 0.0f, 1.0f); }

// A tool as it is when added: its starter sound, on the studio's tempo.
void resetTool (StudioState& s, int tool)
{
    if (toolKind (tool) == ToolKind::Drums)
    {
        s.drums = makeDefaultProject();
        s.drums.bpm = s.bpm;
        s.drums.swing = s.swing;
    }
    else if (const int b = bassIndex (tool); b >= 0)
    {
        s.bass[(size_t) b] = makeDefaultBass();
        s.bass[(size_t) b].bpm = s.bpm;
        s.bass[(size_t) b].swing = s.swing;
    }
}

const char* const kToolNames[kNumToolSlots] = { "DRUMS", "BASS-1", "BASS-2", "BASS-3", "BASS-4" };
const char* const kToolTitles[kNumToolSlots] = { "DRUM MACHINE", "BASS SYNTH 1", "BASS SYNTH 2", "BASS SYNTH 3", "BASS SYNTH 4" };
} // namespace

std::array<ToolMix, kNumToolSlots> StudioState::defaultToolMix()
{
    std::array<ToolMix, kNumToolSlots> t {};
    t[(size_t) kDrumTool].active = true;
    t[(size_t) kFirstBassTool].active = true;
    return t;
}

std::array<BassState, kMaxBassSynths> StudioState::defaultBassSynths()
{
    std::array<BassState, kMaxBassSynths> b;
    b.fill (makeDefaultBass());
    return b;
}

StudioState makeDefaultStudio()
{
    StudioState s;
    s.bpm = s.drums.bpm;
    s.swing = s.drums.swing;
    return s;
}

StudioState studioWithDrums (const ProjectState& drums)
{
    StudioState s;
    s.drums = drums;
    s.bpm = drums.bpm;
    s.swing = drums.swing;
    for (auto& b : s.bass)
    {
        b.bpm = s.bpm;
        b.swing = s.swing;
    }
    for (int t = 0; t < kNumToolSlots; ++t)
        s.tools[(size_t) t].active = t == kDrumTool;
    return s;
}

ToolKind toolKind (int tool)
{
    return bassIndex (tool) >= 0 ? ToolKind::Bass : ToolKind::Drums;
}

const char* toolName (int tool) { return kToolNames[std::clamp (tool, 0, kNumToolSlots - 1)]; }
const char* toolTitle (int tool) { return kToolTitles[std::clamp (tool, 0, kNumToolSlots - 1)]; }

bool validTool (int tool) { return tool >= 0 && tool < kNumToolSlots; }

bool toolAudible (const StudioState& s, int tool)
{
    if (! validTool (tool))
        return false;
    bool anySolo = false;
    for (const auto& t : s.tools)
        anySolo = anySolo || (t.active && t.solo);
    const auto& t = s.tools[(size_t) tool];
    return t.active && ! t.mute && (! anySolo || t.solo);
}

void applyToStudio (StudioState& s, const Command& c)
{
    auto* mix = validTool (c.tool) ? &s.tools[(size_t) c.tool] : nullptr;

    if (c.type == CommandType::SetBpm)
    {
        s.bpm = std::clamp (c.value, kMinBpm, kMaxBpm);
        applyToState (s.drums, c);
        for (auto& b : s.bass)
            applyToBass (b, c);
        return;
    }
    if (c.type == CommandType::SetSwing)
    {
        s.swing = clamp01 (c.value);
        applyToState (s.drums, c);
        for (auto& b : s.bass)
            applyToBass (b, c);
        return;
    }
    if (c.type == CommandType::AddTool)
    {
        if (mix != nullptr && ! mix->active)
        {
            *mix = ToolMix {};
            mix->active = true;
            resetTool (s, c.tool);
        }
        return;
    }
    if (c.type == CommandType::RemoveTool)
    {
        if (mix != nullptr)
        {
            *mix = ToolMix {};
        }
        return;
    }
    if (c.type == CommandType::SetToolMute || c.type == CommandType::SetToolSolo)
    {
        if (mix != nullptr)
            (c.type == CommandType::SetToolMute ? mix->mute : mix->solo) = c.value > 0.5f;
        return;
    }

    if (c.tool == kStudio)
    {
        if (c.type == CommandType::SetFxParam && c.index >= 0 && c.index < kNumFxParams)
            s.masterFx[(size_t) c.index] = clamp01 (c.value);
        else if (c.type == CommandType::SetMasterLevel)
            s.masterLevel = clamp01 (c.value);
        return;
    }

    if (c.tool == kDrumTool)
        applyToState (s.drums, c);
    else if (const int b = bassIndex (c.tool); b >= 0)
        applyToBass (s.bass[(size_t) b], c);
}

#if ! MS_NO_JUCE
juce::var studioToVar (const StudioState& s)
{
    auto root = std::make_unique<juce::DynamicObject>();
    root->setProperty ("format", "modular-synth");
    root->setProperty ("version", 3);
    root->setProperty ("bpm", s.bpm);
    root->setProperty ("swing", s.swing);
    root->setProperty ("masterLevel", s.masterLevel);
    root->setProperty ("masterFx", fxToVar (s.masterFx));

    juce::Array<juce::var> tools;
    for (int t = 0; t < kNumToolSlots; ++t)
    {
        const auto& mix = s.tools[(size_t) t];
        if (! mix.active)
            continue;
        auto o = std::make_unique<juce::DynamicObject>();
        o->setProperty ("tool", toolName (t));
        o->setProperty ("slot", t);
        o->setProperty ("mute", mix.mute);
        o->setProperty ("solo", mix.solo);
        if (toolKind (t) == ToolKind::Drums)
            o->setProperty ("state", projectToVar (s.drums));
        else if (const int b = bassIndex (t); b >= 0)
            o->setProperty ("state", bassToVar (s.bass[(size_t) b]));
        tools.add (juce::var (o.release()));
    }
    root->setProperty ("tools", tools);
    return juce::var (root.release());
}

bool studioFromVar (const juce::var& v, StudioState& out, std::array<juce::String, kNumTracks>& drumSamplePaths)
{
    if (v.getProperty ("format", {}).toString() != "modular-synth")
        return false;

    StudioState s = makeDefaultStudio();

    // Versions 1 and 2 hold just the drum machine.
    if (! v.hasProperty ("tools"))
    {
        ProjectState drums;
        if (! projectFromVar (v, drums, drumSamplePaths))
            return false;
        out = studioWithDrums (drums);
        return true;
    }

    s.bpm = juce::jlimit (kMinBpm, kMaxBpm, (float) v.getProperty ("bpm", s.bpm));
    s.swing = clamp01 ((float) v.getProperty ("swing", s.swing));
    s.masterLevel = clamp01 ((float) v.getProperty ("masterLevel", s.masterLevel));
    fxFromVar (v.getProperty ("masterFx", {}), s.masterFx);

    for (auto& mix : s.tools)
        mix.active = false;

    if (auto* tools = v.getProperty ("tools", {}).getArray())
    {
        for (const auto& o : *tools)
        {
            const int t = (int) o.getProperty ("slot", -1);
            if (! validTool (t) || o.getProperty ("tool", {}).toString() != toolName (t))
                continue;
            auto& mix = s.tools[(size_t) t];
            mix.active = true;
            mix.mute = (bool) o.getProperty ("mute", false);
            mix.solo = (bool) o.getProperty ("solo", false);
            if (toolKind (t) == ToolKind::Drums && ! projectFromVar (o.getProperty ("state", {}), s.drums, drumSamplePaths))
                s.drums = makeDefaultProject();
            if (const int b = bassIndex (t); b >= 0 && ! bassFromVar (o.getProperty ("state", {}), s.bass[(size_t) b]))
                s.bass[(size_t) b] = makeDefaultBass();
        }
    }

    s.drums.bpm = s.bpm;
    s.drums.swing = s.swing;
    for (auto& b : s.bass)
    {
        b.bpm = s.bpm;
        b.swing = s.swing;
    }
    out = s;
    return true;
}
#endif
} // namespace ms
