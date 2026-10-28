#pragma once

#include "StudioEngine.h"

#include <juce_audio_formats/juce_audio_formats.h>

#include <functional>

namespace ms
{
// A performance: the studio as it was when Record was pressed, plus every
// change made while recording, stamped with its sample position.
struct Take
{
    StudioState start;
    std::vector<TimedCommand> events;
    std::int64_t length = 0; // samples, not counting the effects tail
    double sampleRate = 48000.0;
};

// A take that simply plays one pattern a number of times (-1 = the pattern on
// screen).
Take makePatternTake (const StudioState&, int loops, double sampleRate, int pattern = -1);
// The same for a drum machine on its own.
Take makePatternTake (const ProjectState&, int loops, double sampleRate, int pattern = -1);

// What to render: the master (both -1), one tool's share of it, or one track
// of a tool.
struct Stem
{
    int tool = -1;
    int track = -1;
};

// A drum track's stem.
inline Stem drumStem (int track) { return track < 0 ? Stem {} : Stem { kDrumTool, track }; }

// Streams a render of the take to sink in blocks. Rendering continues past
// take.length (sequencer stopped) until totalLength samples have been produced.
void renderTake (const Take&, Stem, std::int64_t totalLength,
                 const std::function<void (const float* left, const float* right, int numSamples)>& sink);

struct ExportOptions
{
    juce::File folder;
    juce::String baseName = "take";
    int bitDepth = 24;
    double maxTailSeconds = 10.0;
    std::array<juce::String, kNumTracks> trackNames; // drum names used in stem file names
};

struct ExportResult
{
    bool ok = false;
    juce::String error;
    juce::Array<juce::File> files; // master first, then each tool followed by its tracks
    std::int64_t lengthSamples = 0;
};

// Writes <base>_Master.wav, one <base>_<TOOL>.wav per tool (e.g. DRUMS) and one
// <base>_<TOOL>-<NN>-<NAME>.wav per track of a tool, skipping any that make no
// sound. All files have the same length and start, so they line up in a DAW.
// progress receives 0..1 and may return false to cancel.
ExportResult exportTake (const Take&, const ExportOptions&,
                         const std::function<bool (float progress)>& progress = {});
} // namespace ms
