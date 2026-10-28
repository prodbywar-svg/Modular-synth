#include "StudioEngine.h"

#include <algorithm>
#include <cmath>

namespace ms
{
void StudioEngine::prepare (double sampleRate, int maxBlockSize)
{
    sr = sampleRate;
    maxBlock = std::max (1, maxBlockSize);
    toolL.assign ((size_t) maxBlock, 0.0f);
    toolR.assign ((size_t) maxBlock, 0.0f);
    drumEngine.prepare (sr, maxBlock);
    for (auto& b : bassEngines)
        b.prepare (sr, maxBlock);
    masterFx.prepare (sr);
    reset();
}

void StudioEngine::reset()
{
    drumEngine.reset();
    for (auto& b : bassEngines)
        b.reset();
    masterFx.setParams (studio.masterFx, studio.bpm);
    masterFx.reset();
    step = 0;
    stepPhase = 0.0;
}

StudioState StudioEngine::state() const
{
    auto s = studio;
    s.drums = drumEngine.state();
    for (int b = 0; b < kMaxBassSynths; ++b)
        s.bass[(size_t) b] = bassEngines[(size_t) b].state();
    return s;
}

void StudioEngine::setState (const StudioState& s)
{
    studio = s;
    auto drums = s.drums;
    drums.bpm = s.bpm;
    drums.swing = s.swing;
    drumEngine.setState (drums);
    for (int b = 0; b < kMaxBassSynths; ++b)
    {
        auto bass = s.bass[(size_t) b];
        bass.bpm = s.bpm;
        bass.swing = s.swing;
        bassEngines[(size_t) b].setState (bass);
    }
}

void StudioEngine::addTool (int tool)
{
    if (! validTool (tool) || studio.tools[(size_t) tool].active)
        return;

    auto fresh = state();
    applyToStudio (fresh, [tool] { Command c; c.type = CommandType::AddTool; c.tool = tool; return c; }());
    studio.tools[(size_t) tool] = fresh.tools[(size_t) tool];

    if (toolKind (tool) == ToolKind::Drums)
    {
        drumEngine.setState (fresh.drums);
        drumEngine.reset();
        if (playing)
            drumEngine.joinClock (step, stepPhase);
    }
    else if (const int b = bassIndex (tool); b >= 0)
    {
        auto& engine = bassEngines[(size_t) b];
        engine.setState (fresh.bass[(size_t) b]);
        engine.reset();
        if (playing)
            engine.joinClock (step, stepPhase);
    }
}

void StudioEngine::apply (const Command& c)
{
    if (c.type == CommandType::SetBpm || c.type == CommandType::SetSwing)
    {
        applyToStudio (studio, c);
        drumEngine.apply (c);
        for (auto& b : bassEngines)
            b.apply (c);
        return;
    }
    if (c.type == CommandType::AddTool)
    {
        addTool (c.tool);
        return;
    }
    if (c.type == CommandType::RemoveTool || c.type == CommandType::SetToolMute || c.type == CommandType::SetToolSolo)
    {
        applyToStudio (studio, c);
        return;
    }

    if (c.tool == kStudio)
        applyToStudio (studio, c);
    else if (c.tool == kDrumTool)
        drumEngine.apply (c);
    else if (const int b = bassIndex (c.tool); b >= 0)
        bassEngines[(size_t) b].apply (c);
}

void StudioEngine::setPlaying (bool shouldPlay)
{
    if (shouldPlay && ! playing)
    {
        step = 0;
        stepPhase = 0.0;
    }
    playing = shouldPlay;
    drumEngine.setPlaying (shouldPlay);
    for (auto& b : bassEngines)
        b.setPlaying (shouldPlay);
}

void StudioEngine::setSoloRender (int tool, int track)
{
    soloTool = tool;
    drumEngine.setSoloRender (tool == kDrumTool ? track : -1);
    for (int b = 0; b < kMaxBassSynths; ++b)
        bassEngines[(size_t) b].setSoloRender (bassIndex (tool) == b ? track : -1);
}

float StudioEngine::trackPeak (int tool, int track) const
{
    if (tool == kDrumTool)
        return drumEngine.trackPeak (track);
    const int b = bassIndex (tool);
    return b >= 0 ? bassEngines[(size_t) b].trackPeak (track) : 0.0f;
}

const float* StudioEngine::trackOutput (int tool, int track) const
{
    if (tool == kDrumTool)
        return drumEngine.trackOutput (track);
    const int b = bassIndex (tool);
    return b >= 0 ? bassEngines[(size_t) b].trackOutput (track) : nullptr;
}

bool StudioEngine::trackTriggered (int tool, int track) const
{
    if (tool == kDrumTool)
        return drumEngine.trackTriggered (track);
    const int b = bassIndex (tool);
    return b >= 0 && bassEngines[(size_t) b].trackTriggered (track);
}

float StudioEngine::trackNote (int tool, int track) const
{
    const int b = bassIndex (tool);
    return b >= 0 ? bassEngines[(size_t) b].trackNote (track) : 0.0f;
}

void StudioEngine::clearTriggered()
{
    drumEngine.clearTriggered();
    for (auto& b : bassEngines)
        b.clearTriggered();
}

float StudioEngine::busPeak (int tool) const
{
    if (tool == kDrumTool)
        return drumEngine.masterPeak();
    const int b = bassIndex (tool);
    return b >= 0 ? bassEngines[(size_t) b].masterPeak() : 0.0f;
}

int StudioEngine::toolPattern (int tool) const
{
    if (tool == kDrumTool)
        return drumEngine.currentPattern();
    const int b = bassIndex (tool);
    return b >= 0 ? bassEngines[(size_t) b].currentPattern() : 0;
}

int StudioEngine::toolQueuedPattern (int tool) const
{
    if (tool == kDrumTool)
        return drumEngine.queuedPattern();
    const int b = bassIndex (tool);
    return b >= 0 ? bassEngines[(size_t) b].queuedPattern() : -1;
}

void StudioEngine::processTool (int tool, float* left, float* right, int n)
{
    if (toolKind (tool) == ToolKind::Drums)
        drumEngine.process (left, right, n);
    else if (const int b = bassIndex (tool); b >= 0)
        bassEngines[(size_t) b].process (left, right, n);
}

void StudioEngine::process (float* left, float* right, int numSamples)
{
    for (int offset = 0; offset < numSamples; offset += maxBlock)
    {
        const int n = std::min (maxBlock, numSamples - offset);
        float* outL = left + offset;
        float* outR = right + offset;
        std::fill (outL, outL + n, 0.0f);
        std::fill (outR, outR + n, 0.0f);

        // where the block starts on the studio clock, for the master automation
        const double autoPosition = (double) step + stepPhase / samplesPerStep();
        const double autoRate = playing ? 1.0 / samplesPerStep() : 0.0;

        // 1. each tool, summed while it is audible
        for (int t = 0; t < kNumToolSlots; ++t)
        {
            toolPeaks[(size_t) t] = 0.0f;
            if (! studio.tools[(size_t) t].active || (soloTool >= 0 && soloTool != t))
                continue;

            processTool (t, toolL.data(), toolR.data(), n);
            if (! toolAudible (studio, t))
                continue;

            float peak = 0.0f;
            for (int i = 0; i < n; ++i)
            {
                outL[i] += toolL[(size_t) i];
                outR[i] += toolR[(size_t) i];
                peak = std::max ({ peak, std::abs (toolL[(size_t) i]), std::abs (toolR[(size_t) i]) });
            }
            toolPeaks[(size_t) t] = peak;
        }

        // 2. the shared clock, stepped the same way as each sequencer's own
        if (playing)
        {
            const double stepLen = samplesPerStep();
            for (int i = 0; i < n; ++i)
            {
                stepPhase += 1.0;
                if (stepPhase >= stepLen)
                {
                    stepPhase -= stepLen;
                    step = (step + 1) % kNumSteps;
                }
            }
        }

        // 3. studio master effects and level
        masterFx.processAutomated (studio.masterFx, studio.masterFxAuto, 0, autoPosition, autoRate, studio.bpm, outL, outR, n);

        const float gain = levelToGain (studio.masterLevel);
        float mp = 0.0f;
        for (int i = 0; i < n; ++i)
        {
            outL[i] *= gain;
            outR[i] *= gain;
            mp = std::max ({ mp, std::abs (outL[i]), std::abs (outR[i]) });
        }
        masterPeakValue = mp;
    }
}
} // namespace ms
