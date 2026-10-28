#pragma once

#include "BassState.h"

namespace ms
{
// The studio: one clock and one master rack shared by every tool, and the
// tools themselves. Each tool has a fixed slot: slot 0 is the drum machine,
// slots 1-4 the bass synths. The loopers get their own slots as they arrive.
constexpr int kMaxBassSynths = 4;
constexpr int kFirstBassTool = 1;
constexpr int kNumToolSlots = kFirstBassTool + kMaxBassSynths;

enum class ToolKind : std::uint8_t
{
    Drums,
    Bass,
    Count
};

struct ToolMix
{
    bool active = false;
    bool mute = false;
    bool solo = false;
};

// Plain data like ProjectState, so a recording can copy it as its start.
struct StudioState
{
    float bpm = 132.0f;
    float swing = 0.0f;
    float masterLevel = 0.7071f; // unity: each tool already has its own level
    FxParams masterFx = defaultFxParams();
    std::array<ToolMix, kNumToolSlots> tools = defaultToolMix(); // the drum machine and one bass synth
    ProjectState drums = makeDefaultProject(); // slot 0; its master rack is the drum bus
    std::array<BassState, kMaxBassSynths> bass = defaultBassSynths(); // slots 1-4

    static std::array<ToolMix, kNumToolSlots> defaultToolMix();
    static std::array<BassState, kMaxBassSynths> defaultBassSynths();
};

// The bass synth in a tool slot (slots 1-4 -> 0-3), or -1.
inline int bassIndex (int tool) { return tool >= kFirstBassTool && tool < kFirstBassTool + kMaxBassSynths ? tool - kFirstBassTool : -1; }

StudioState makeDefaultStudio();
// A studio holding just this drum machine, on its tempo (how drum machine
// projects from before the studio open).
StudioState studioWithDrums (const ProjectState&);

ToolKind toolKind (int tool);
// Used in exported file names and project files, e.g. "DRUMS", "BASS-2".
const char* toolName (int tool);
// Used on screen, e.g. "DRUM MACHINE", "BASS SYNTH 2".
const char* toolTitle (int tool);
bool validTool (int tool);
// Active, not muted, and not silenced by another tool's solo.
bool toolAudible (const StudioState&, int tool);

// Applies a state-changing command to the studio or the tool it is for.
void applyToStudio (StudioState&, const Command&);

#if ! MS_NO_JUCE
// ---- save / open ------------------------------------------------------------
juce::var studioToVar (const StudioState&);
// Opens studio files (version 3 and later) and drum machine files (versions 1 and 2).
// Sample paths are returned for the caller to load, as in projectFromVar.
bool studioFromVar (const juce::var&, StudioState&, std::array<juce::String, kNumTracks>& drumSamplePaths);
#endif
} // namespace ms
