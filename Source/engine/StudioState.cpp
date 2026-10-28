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
}
} // namespace

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
    return s;
}

ToolKind toolKind (int tool)
{
    (void) tool;
    return ToolKind::Drums;
}

const char* toolName (int tool)
{
    switch (toolKind (tool))
    {
        case ToolKind::Drums: return "DRUMS";
        case ToolKind::Count: break;
    }
    return "TOOL";
}

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
        return;
    }
    if (c.type == CommandType::SetSwing)
    {
        s.swing = clamp01 (c.value);
        applyToState (s.drums, c);
        return;
    }
    if (c.type == CommandType::AddTool)
    {
        if (mix != nullptr && ! mix->active)
        {
            *mix = ToolMix {};
            resetTool (s, c.tool);
        }
        return;
    }
    if (c.type == CommandType::RemoveTool)
    {
        if (mix != nullptr)
        {
            *mix = ToolMix {};
            mix->active = false;
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
        }
    }

    s.drums.bpm = s.bpm;
    s.drums.swing = s.swing;
    out = s;
    return true;
}
#endif
} // namespace ms
