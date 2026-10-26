#pragma once

#include "EngineCore.h"

#include <juce_audio_formats/juce_audio_formats.h>

#include <functional>

namespace ms
{
// A performance: the project as it was when Record was pressed, plus every
// change made while recording, stamped with its sample position.
struct Take
{
    ProjectState start;
    std::vector<TimedCommand> events;
    std::int64_t length = 0; // samples, not counting the effects tail
    double sampleRate = 48000.0;
};

// A take that simply plays one pattern a number of times (-1 = the pattern on
// screen).
Take makePatternTake (const ProjectState&, int loops, double sampleRate, int pattern = -1);

// Streams a render of the take to sink in blocks. soloTrack = -1 renders the
// master; a track index renders that track's stem. Rendering continues past
// take.length (sequencer stopped) until totalLength samples have been produced.
void renderTake (const Take&, int soloTrack, std::int64_t totalLength,
                 const std::function<void (const float* left, const float* right, int numSamples)>& sink);

struct ExportOptions
{
    juce::File folder;
    juce::String baseName = "take";
    int bitDepth = 24;
    double maxTailSeconds = 10.0;
    std::array<juce::String, kNumTracks> trackNames; // used in stem file names
};

struct ExportResult
{
    bool ok = false;
    juce::String error;
    juce::Array<juce::File> files; // master first, then stems
    std::int64_t lengthSamples = 0;
};

// Writes <base>_Master.wav plus one <base>_<NN>-<NAME>.wav per track that makes
// sound. All files have the same length and start, so they line up in a DAW.
// progress receives 0..1 and may return false to cancel.
ExportResult exportTake (const Take&, const ExportOptions&,
                         const std::function<bool (float progress)>& progress = {});
} // namespace ms
