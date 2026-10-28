#include "PluginProcessor.h"

#include "PluginEditor.h"

#include "engine/Hr16Bank.h"

using namespace ms;

namespace
{
constexpr size_t kMaxRecordedEvents = 1 << 19;

// MIDI notes that play the pads: GM drum notes 36-43 and C4-G4 (60-67).
int padForNote (int note)
{
    if (note >= 36 && note < 36 + kNumTracks) return note - 36;
    if (note >= 60 && note < 60 + kNumTracks) return note - 60;
    return -1;
}
} // namespace

ModularSynthProcessor::ModularSynthProcessor()
    : AudioProcessor (BusesProperties().withOutput ("Output", juce::AudioChannelSet::stereo(), true))
{
    loadHr16Bank();
    for (auto& t : takes)
        t.events.reserve (kMaxRecordedEvents);
}

// ---- message thread ---------------------------------------------------------

void ModularSynthProcessor::send (const Command& c)
{
    applyToStudio (uiState, c);

    const auto scope = fifo.write (1);
    if (scope.blockSize1 > 0)
        fifoBuffer[(size_t) scope.startIndex1] = c;
    else if (scope.blockSize2 > 0)
        fifoBuffer[(size_t) scope.startIndex2] = c;
    else
        jassertfalse; // queue full: the audio thread has stalled
}

void ModularSynthProcessor::replaceStudio (const StudioState& s)
{
    auto cmd = [this] (CommandType type, int tool, int track, int index, float value, const SampleData* sample = nullptr) {
        Command c;
        c.type = type;
        c.tool = tool;
        c.track = track;
        c.index = index;
        c.value = value;
        c.sample = sample;
        send (c);
    };

    // tools come and go first: adding one starts it from its default sound
    for (int t = 0; t < kNumToolSlots; ++t)
    {
        const bool want = s.tools[(size_t) t].active;
        if (want && ! uiState.tools[(size_t) t].active)
            cmd (CommandType::AddTool, t, 0, 0, 0.0f);
        else if (! want)
            cmd (CommandType::RemoveTool, t, 0, 0, 0.0f);
    }

    const auto& p = s.drums;
    for (int t = 0; t < kNumTracks; ++t)
    {
        const auto& tr = p.tracks[(size_t) t];
        cmd (CommandType::SetVoiceType, kDrumTool, t, (int) tr.voice, 0.0f);
        cmd (CommandType::SetCharacter, kDrumTool, t, (int) tr.character, 0.0f);
        for (int i = 0; i < kNumVoiceParams; ++i)
            cmd (CommandType::SetVoiceParam, kDrumTool, t, i, tr.voiceParams[(size_t) i]);
        cmd (CommandType::SetLevel, kDrumTool, t, 0, tr.level);
        cmd (CommandType::SetPan, kDrumTool, t, 0, tr.pan);
        cmd (CommandType::SetMute, kDrumTool, t, 0, tr.mute ? 1.0f : 0.0f);
        cmd (CommandType::SetSolo, kDrumTool, t, 0, tr.solo ? 1.0f : 0.0f);
        for (int i = 0; i < kNumFxParams; ++i)
            cmd (CommandType::SetFxParam, kDrumTool, t, i, tr.fx[(size_t) i]);
        for (int pat = 0; pat < kNumPatterns; ++pat)
            for (int st = 0; st < kNumSteps; ++st)
                cmd (CommandType::SetStep, kDrumTool, t, pat * kNumSteps + st, tr.patterns[(size_t) pat][(size_t) st]);
        cmd (CommandType::SetSample, kDrumTool, t, 0, 0.0f, tr.sample);
    }
    cmd (CommandType::SelectPattern, kDrumTool, 0, p.pattern, 0.0f);
    cmd (CommandType::SetMasterLevel, kDrumTool, 0, 0, p.masterLevel);
    for (int i = 0; i < kNumFxParams; ++i)
        cmd (CommandType::SetFxParam, kDrumTool, -1, i, p.masterFx[(size_t) i]);

    cmd (CommandType::SetBpm, kStudio, 0, 0, s.bpm);
    cmd (CommandType::SetSwing, kStudio, 0, 0, s.swing);
    cmd (CommandType::SetMasterLevel, kStudio, 0, 0, s.masterLevel);
    for (int i = 0; i < kNumFxParams; ++i)
        cmd (CommandType::SetFxParam, kStudio, 0, i, s.masterFx[(size_t) i]);
    for (int t = 0; t < kNumToolSlots; ++t)
    {
        cmd (CommandType::SetToolMute, t, 0, 0, s.tools[(size_t) t].mute ? 1.0f : 0.0f);
        cmd (CommandType::SetToolSolo, t, 0, 0, s.tools[(size_t) t].solo ? 1.0f : 0.0f);
    }
}

const SampleData* ModularSynthProcessor::loadSample (const juce::File& file, juce::String& error)
{
    for (const auto& s : samples)
        if (s->path == file.getFullPathName())
            return s.get();

    juce::AudioFormatManager formats;
    formats.registerBasicFormats();
    std::unique_ptr<juce::AudioFormatReader> reader (formats.createReaderFor (file));
    if (reader == nullptr)
    {
        error = "Can't read " + file.getFileName();
        return nullptr;
    }

    constexpr juce::int64 maxSamples = 48000 * 60; // one minute is plenty for a drum hit
    const auto length = (int) juce::jmin (reader->lengthInSamples, maxSamples);
    juce::AudioBuffer<float> buffer ((int) reader->numChannels, length);
    reader->read (&buffer, 0, length, 0, true, true);

    auto data = std::make_unique<SampleData>();
    data->sampleRate = reader->sampleRate;
    data->path = file.getFullPathName();
    data->samples.resize ((size_t) length);
    const float scale = 1.0f / (float) juce::jmax (1, buffer.getNumChannels());
    for (int ch = 0; ch < buffer.getNumChannels(); ++ch)
        for (int i = 0; i < length; ++i)
            data->samples[(size_t) i] += buffer.getSample (ch, i) * scale;

    samples.push_back (std::move (data));
    return samples.back().get();
}

bool ModularSynthProcessor::saveProject (const juce::File& file)
{
    auto v = studioToVar (uiState);
    if (auto* o = v.getDynamicObject())
        o->setProperty ("windows", windowLayout);
    return file.replaceWithText (juce::JSON::toString (v));
}

void ModularSynthProcessor::loadSamplePaths (StudioState& s, const std::array<juce::String, kNumTracks>& paths, juce::String& error)
{
    for (int t = 0; t < kNumTracks; ++t)
    {
        const auto& path = paths[(size_t) t];
        if (path.isEmpty())
            continue;
        juce::String sampleError;
        s.drums.tracks[(size_t) t].sample = loadSample (juce::File (path), sampleError);
        if (s.drums.tracks[(size_t) t].sample == nullptr)
            error = "Missing sample: " + path;
    }
}

bool ModularSynthProcessor::openProject (const juce::File& file, juce::String& error)
{
    const auto v = juce::JSON::parse (file);
    StudioState s;
    std::array<juce::String, kNumTracks> paths;
    if (! studioFromVar (v, s, paths))
    {
        error = file.getFileName() + " is not a Modular-synth project";
        return false;
    }

    loadSamplePaths (s, paths, error);
    replaceStudio (s);
    windowLayout = v.getProperty ("windows", {});
    ++windowLayoutVersion;
    return true;
}

juce::String ModularSynthProcessor::trackName (int track) const
{
    const auto& tr = uiState.drums.tracks[(size_t) track];
    if (tr.voice == VoiceType::Sample && tr.sample != nullptr)
        return juce::File (tr.sample->path).getFileNameWithoutExtension().toUpperCase();
    return drumName (tr.voice, tr.character);
}

std::optional<Take> ModularSynthProcessor::takeFinishedRecording()
{
    const int index = finishedTake.exchange (-1);
    if (index < 0)
        return std::nullopt;

    lastTake = takes[(size_t) index];
    return lastTake;
}

void ModularSynthProcessor::syncPatternFromEngine()
{
    // The engine switches to a queued pattern on its own at the end of a bar;
    // mirror that once it has happened.
    const int queued = uiState.drums.queuedPattern;
    if (queued >= 0 && queuedPatternIndex.load() < 0 && currentPatternIndex.load() == queued)
    {
        uiState.drums.pattern = queued;
        uiState.drums.queuedPattern = -1;
    }
}

float ModularSynthProcessor::toolHistory (int tool, int age) const
{
    const int pos = (historyPos.load() - 1 - age + kHistoryLength * 4) % kHistoryLength;
    return toolHistoryValues[(size_t) (tool * kHistoryLength + pos)].load (std::memory_order_relaxed);
}

float ModularSynthProcessor::trackHistory (int t, int age) const
{
    const int pos = (historyPos.load() - 1 - age + kHistoryLength * 4) % kHistoryLength;
    return history[(size_t) (t * kHistoryLength + pos)].load (std::memory_order_relaxed);
}

void ModularSynthProcessor::copyTrackScope (int t, float* dest, int count) const
{
    const int end = scopePos.load();
    for (int i = 0; i < count; ++i)
    {
        const int pos = (end - count + i + kScopeLength * 4) % kScopeLength;
        dest[i] = scopeSamples[(size_t) (t * kScopeLength + pos)].load (std::memory_order_relaxed);
    }
}

// ---- audio thread -----------------------------------------------------------

void ModularSynthProcessor::prepareToPlay (double sampleRate, int)
{
    engine.prepare (sampleRate, kHistoryWindow);
    sampleRateForUi = sampleRate;
}

bool ModularSynthProcessor::isBusesLayoutSupported (const BusesLayout& layouts) const
{
    return layouts.getMainOutputChannelSet() == juce::AudioChannelSet::stereo();
}

void ModularSynthProcessor::recordEvent (const Command& c)
{
    if (! recording)
        return;

    auto& events = takes[(size_t) writeTake].events;
    if (events.size() >= kMaxRecordedEvents)
    {
        finishRecording(); // out of room: keep what we have
        return;
    }

    events.push_back ({ recordPosition, c });
    recordedEventCount = (int) events.size();
}

void ModularSynthProcessor::finishRecording()
{
    if (! recording)
        return;

    recording = false;
    takes[(size_t) writeTake].length = recordPosition;
    finishedTake = writeTake;
    writeTake = 1 - writeTake;
}

void ModularSynthProcessor::handleOnAudioThread (const Command& c)
{
    if (c.type == CommandType::Play)
    {
        if (! engine.isPlaying())
            playedSampleCount = 0;
        engine.setPlaying (true);
    }
    else if (c.type == CommandType::Stop || c.type == CommandType::RecordStop)
    {
        finishRecording();
        engine.setPlaying (false);
    }
    else if (c.type == CommandType::RecordStart)
    {
        finishRecording();
        // Start from silence at step 1, exactly like the offline export will.
        engine.setPlaying (false);
        engine.reset();
        auto& take = takes[(size_t) writeTake];
        take.start = engine.state();
        take.events.clear(); // keeps its reserved memory
        take.sampleRate = engine.getSampleRate();
        take.length = 0;
        recordPosition = 0;
        recordedEventCount = 0;
        playedSampleCount = 0;
        recording = true;
        engine.setPlaying (true);
    }
    else
    {
        engine.apply (c);
        recordEvent (c);
    }
}

void ModularSynthProcessor::processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midi)
{
    juce::ScopedNoDenormals noDenormals;

    // 1. commands from the UI
    const auto scope = fifo.read (fifo.getNumReady());
    for (int i = 0; i < scope.blockSize1; ++i)
        handleOnAudioThread (fifoBuffer[(size_t) (scope.startIndex1 + i)]);
    for (int i = 0; i < scope.blockSize2; ++i)
        handleOnAudioThread (fifoBuffer[(size_t) (scope.startIndex2 + i)]);

    // 2. MIDI pads (played at the start of the block, like UI hits)
    for (const auto metadata : midi)
    {
        const auto msg = metadata.getMessage();
        if (msg.isNoteOn())
        {
            const int pad = padForNote (msg.getNoteNumber());
            if (pad >= 0)
            {
                Command c;
                c.type = CommandType::Trigger;
                c.tool = kDrumTool;
                c.track = pad;
                c.value = msg.getFloatVelocity();
                handleOnAudioThread (c);
            }
        }
    }
    midi.clear();

    // 3. audio, in history-sized chunks so the barcodes have a steady clock
    const int numSamples = buffer.getNumSamples();
    float* left = buffer.getWritePointer (0);
    float* right = buffer.getNumChannels() > 1 ? buffer.getWritePointer (1) : nullptr;
    std::array<float, kHistoryWindow> scratch {};
    float blockMasterPeak = 0.0f, blockDrumPeak = 0.0f;
    auto& drums = engine.drums();

    for (int offset = 0; offset < numSamples;)
    {
        const int n = juce::jmin (kHistoryWindow - windowFill, numSamples - offset);
        engine.process (left + offset, right != nullptr ? right + offset : scratch.data(), n);
        offset += n;
        windowFill += n;
        blockMasterPeak = juce::jmax (blockMasterPeak, engine.masterPeak());
        blockDrumPeak = juce::jmax (blockDrumPeak, drums.masterPeak());

        for (int tool = 0; tool < kNumToolSlots; ++tool)
        {
            toolWindowPeak[(size_t) tool] = juce::jmax (toolWindowPeak[(size_t) tool], engine.toolPeak (tool));
            toolPeaks[(size_t) tool].store (engine.toolPeak (tool), std::memory_order_relaxed);
        }

        for (int t = 0; t < kNumTracks; ++t)
        {
            windowPeak[(size_t) t] = juce::jmax (windowPeak[(size_t) t], drums.trackPeak (t));
            trackPeaks[(size_t) t].store (drums.trackPeak (t), std::memory_order_relaxed);
            if (drums.trackTriggered (t))
                hitCounts[(size_t) t].fetch_add (1, std::memory_order_relaxed);
        }
        drums.clearTriggered();

        const int scopeStart = scopePos.load (std::memory_order_relaxed);
        for (int t = 0; t < kNumTracks; ++t)
        {
            const float* out = drums.trackOutput (t);
            for (int i = 0; i < n; ++i)
                scopeSamples[(size_t) (t * kScopeLength + (scopeStart + i) % kScopeLength)].store (out[i], std::memory_order_relaxed);
        }
        scopePos.store ((scopeStart + n) % kScopeLength);

        if (windowFill >= kHistoryWindow)
        {
            const int pos = historyPos.load (std::memory_order_relaxed);
            for (int t = 0; t < kNumTracks; ++t)
            {
                history[(size_t) (t * kHistoryLength + pos)].store (windowPeak[(size_t) t], std::memory_order_relaxed);
                windowPeak[(size_t) t] = 0.0f;
            }
            for (int tool = 0; tool < kNumToolSlots; ++tool)
            {
                toolHistoryValues[(size_t) (tool * kHistoryLength + pos)].store (toolWindowPeak[(size_t) tool], std::memory_order_relaxed);
                toolWindowPeak[(size_t) tool] = 0.0f;
            }
            historyPos.store ((pos + 1) % kHistoryLength);
            windowFill = 0;
        }
    }

    for (int ch = 2; ch < buffer.getNumChannels(); ++ch)
        buffer.clear (ch, 0, numSamples);

    if (recording)
        recordPosition += numSamples;
    if (engine.isPlaying())
        playedSampleCount += numSamples;

    masterPeakValue.store (blockMasterPeak, std::memory_order_relaxed);
    drumBusPeakValue.store (blockDrumPeak, std::memory_order_relaxed);
    playingFlag = engine.isPlaying();
    recordingFlag = recording;
    stepIndex = engine.currentStep();
    currentPatternIndex = drums.currentPattern();
    queuedPatternIndex = drums.queuedPattern();
    recordedSampleCount = recordPosition;
}

// ---- plugin plumbing --------------------------------------------------------

juce::AudioProcessorEditor* ModularSynthProcessor::createEditor()
{
    return new ModularSynthEditor (*this);
}

void ModularSynthProcessor::getStateInformation (juce::MemoryBlock& dest)
{
    auto v = studioToVar (uiState);
    if (auto* o = v.getDynamicObject())
        o->setProperty ("windows", windowLayout);
    const auto json = juce::JSON::toString (v);
    dest.replaceAll (json.toRawUTF8(), json.getNumBytesAsUTF8());
}

void ModularSynthProcessor::setStateInformation (const void* data, int size)
{
    const auto v = juce::JSON::parse (juce::String::fromUTF8 ((const char*) data, size));
    StudioState s;
    std::array<juce::String, kNumTracks> paths;
    if (! studioFromVar (v, s, paths))
        return;

    juce::String error;
    loadSamplePaths (s, paths, error);
    replaceStudio (s);
    windowLayout = v.getProperty ("windows", {});
    ++windowLayoutVersion;
}

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new ModularSynthProcessor();
}
