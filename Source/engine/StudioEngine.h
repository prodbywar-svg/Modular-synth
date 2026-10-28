#pragma once

#include "BassEngine.h"
#include "EngineCore.h"
#include "StudioState.h"

namespace ms
{
// Every tool in the studio, on one clock, summed through the studio master
// rack. Like EngineCore it knows nothing about devices, threads or files.
class StudioEngine
{
public:
    void prepare (double sampleRate, int maxBlockSize);

    // Silences every tool, clears effect tails and rewinds the clock.
    void reset();

    StudioState state() const;
    void setState (const StudioState&);

    void apply (const Command&);

    void setPlaying (bool shouldPlay);
    bool isPlaying() const { return playing; }

    // What process() renders: tool -1 is the full mix. A tool renders only its
    // share of the mix (through the studio master rack); with a track as well,
    // only that track of the tool. One stem each.
    void setSoloRender (int tool, int track = -1);

    // Overwrites left/right with numSamples of output.
    void process (float* left, float* right, int numSamples);

    // ---- read-outs ---------------------------------------------------------
    EngineCore& drums() { return drumEngine; }
    const EngineCore& drums() const { return drumEngine; }
    BassEngine& bass (int index) { return bassEngines[(size_t) index]; }
    // A track (drum or bass row) of a tool: peak, output, hit since clearTriggered().
    float trackPeak (int tool, int track) const;
    const float* trackOutput (int tool, int track) const;
    bool trackTriggered (int tool, int track) const;
    // A bass row's sounding (or last) MIDI note; 0 for drums.
    float trackNote (int tool, int track) const;
    bool toolActive (int tool) const { return validTool (tool) && studio.tools[(size_t) tool].active; }
    void clearTriggered();
    // The tool's own bus, before the studio master.
    float busPeak (int tool) const;
    int toolPattern (int tool) const;
    int toolQueuedPattern (int tool) const;
    int currentStep() const { return step; }
    float toolPeak (int tool) const { return validTool (tool) ? toolPeaks[(size_t) tool] : 0.0f; }
    float masterPeak() const { return masterPeakValue; }
    double getSampleRate() const { return sr; }
    double samplesPerStep() const { return sr * 60.0 / (double) studio.bpm / 4.0; }

private:
    void processTool (int tool, float* left, float* right, int numSamples);
    void addTool (int tool);

    double sr = 48000.0;
    int maxBlock = 512;
    StudioState studio = makeDefaultStudio(); // the tools' own state lives in their engines

    EngineCore drumEngine;
    std::array<BassEngine, kMaxBassSynths> bassEngines;
    FxChain masterFx;
    std::vector<float> toolL, toolR;

    bool playing = false;
    int step = 0;
    double stepPhase = 0.0;
    int soloTool = -1;

    std::array<float, kNumToolSlots> toolPeaks {};
    float masterPeakValue = 0.0f;
};
} // namespace ms
