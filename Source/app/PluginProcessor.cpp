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
    : AudioProcessor (BusesProperties()
                          .withInput ("Input", juce::AudioChannelSet::stereo(), true)
                          .withOutput ("Output", juce::AudioChannelSet::stereo(), true))
{
    loadHr16Bank();
    static_assert (kNumTracks <= kLanesPerTool && kBassRows <= kLanesPerTool && kLooperTracks <= kLanesPerTool);
    for (auto& q : queuedPatterns)
        q = -1;
    for (auto& p : looperPosition)
        p = -1.0;
    for (auto& t : takes)
        t.events.reserve (kMaxRecordedEvents);
    engine.setRecordsInput (true);
}

ModularSynthProcessor::~ModularSynthProcessor()
{
    // chunks the audio thread never took
    const auto scope = chunkFifo.read (chunkFifo.getNumReady());
    for (int i = 0; i < scope.blockSize1; ++i)
        delete[] chunkSlots[(size_t) (scope.startIndex1 + i)];
    for (int i = 0; i < scope.blockSize2; ++i)
        delete[] chunkSlots[(size_t) (scope.startIndex2 + i)];
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

    for (int b = 0; b < kMaxBassSynths; ++b)
    {
        const int tool = kFirstBassTool + b;
        if (! s.tools[(size_t) tool].active)
            continue;
        const auto& bass = s.bass[(size_t) b];
        // the scale first: it would snap the notes that follow
        cmd (CommandType::SetScale, tool, 0, 0, 0.0f);
        for (int r = 0; r < kBassRows; ++r)
        {
            const auto& row = bass.rows[(size_t) r];
            cmd (CommandType::SetVoiceType, tool, r, (int) row.sound, 0.0f);
            for (int i = 0; i < kNumBassParams; ++i)
                cmd (CommandType::SetVoiceParam, tool, r, i, row.params[(size_t) i]);
            cmd (CommandType::SetLevel, tool, r, 0, row.level);
            cmd (CommandType::SetPan, tool, r, 0, row.pan);
            cmd (CommandType::SetMute, tool, r, 0, row.mute ? 1.0f : 0.0f);
            cmd (CommandType::SetSolo, tool, r, 0, row.solo ? 1.0f : 0.0f);
            cmd (CommandType::SetCable, tool, r, row.cable, 0.0f);
            for (int i = 0; i < kNumFxParams; ++i)
                cmd (CommandType::SetFxParam, tool, r, i, row.fx[(size_t) i]);
            for (int pat = 0; pat < kNumPatterns; ++pat)
                for (int st = 0; st < kNumSteps; ++st)
                {
                    const int index = pat * kNumSteps + st;
                    cmd (CommandType::SetNote, tool, r, index, (float) row.notes[(size_t) pat][(size_t) st]);
                    cmd (CommandType::SetLength, tool, r, index, (float) row.lengths[(size_t) pat][(size_t) st]);
                    cmd (CommandType::SetStep, tool, r, index, row.velocity[(size_t) pat][(size_t) st]);
                }
        }
        cmd (CommandType::SetKey, tool, 0, bass.key, 0.0f);
        cmd (CommandType::SetScale, tool, 0, bass.scale, 0.0f);
        cmd (CommandType::SelectPattern, tool, 0, bass.pattern, 0.0f);
        cmd (CommandType::SetMasterLevel, tool, 0, 0, bass.masterLevel);
        for (int i = 0; i < kNumFxParams; ++i)
            cmd (CommandType::SetFxParam, tool, -1, i, bass.masterFx[(size_t) i]);
    }

    for (int l = 0; l < kMaxLoopers; ++l)
    {
        const int tool = kFirstLooperTool + l;
        if (! s.tools[(size_t) tool].active)
            continue;
        const auto& looper = s.loopers[(size_t) l];
        cmd (CommandType::CancelLoopRecord, tool, 0, 0, 0.0f);
        cmd (CommandType::SetLoopInput, tool, 0, (int) looper.input, 0.0f);
        cmd (CommandType::SetLoopMonitor, tool, 0, 0, looper.monitor ? 1.0f : 0.0f);
        cmd (CommandType::SetMonitorDry, tool, 0, 0, looper.monitorDry ? 1.0f : 0.0f);
        cmd (CommandType::SetCountIn, tool, 0, 0, (float) looper.countIn);
        cmd (CommandType::SetInputGain, tool, 0, 0, looper.inputGain);
        cmd (CommandType::SetMasterLevel, tool, 0, 0, looper.masterLevel);
        cmd (CommandType::SetPan, tool, 0, 0, looper.pan);
        for (int i = 0; i < kNumFxParams; ++i)
            cmd (CommandType::SetFxParam, tool, -1, i, looper.masterFx[(size_t) i]);
        for (int slot = 0; slot < kNumPatterns; ++slot)
        {
            const auto& ls = looper.slots[(size_t) slot];
            cmd (CommandType::SetLoopBars, tool, 0, slot, (float) ls.bars);
            for (int st = 0; st < kNumSteps; ++st)
                cmd (CommandType::SetSlice, tool, 0, slot * kNumSteps + st, (float) ls.slices[(size_t) st]);
            Command c;
            c.type = CommandType::SetLoopAudio;
            c.tool = tool;
            c.index = slot;
            c.loop = const_cast<LoopAudio*> (ls.audio); // never written to through this
            send (c);
            layers[(size_t) l][(size_t) slot].clear();
        }
        cmd (CommandType::SelectPattern, tool, 0, looper.pattern, 0.0f);
    }

    // effect automation: clear every rack, then send each recorded point
    auto automation = [&cmd] (int tool, int rack, const FxAutomation& a) {
        for (int pat = 0; pat < kNumPatterns; ++pat)
            for (int i = 0; i < kNumFxParams; ++i)
                for (int st = 0; st < kNumSteps; ++st)
                    if (const float v = a.point (pat, i, st); v >= 0.0f)
                        cmd (CommandType::SetFxAuto, tool, rack, fxAutoIndex (pat, i, st), v);
    };
    cmd (CommandType::ClearFxAuto, kStudio, 0, -1, -1.0f);
    automation (kStudio, 0, s.masterFxAuto);
    if (s.tools[(size_t) kDrumTool].active)
    {
        cmd (CommandType::ClearFxAuto, kDrumTool, kAllRacks, -1, -1.0f);
        automation (kDrumTool, -1, p.masterFxAuto);
        for (int t = 0; t < kNumTracks; ++t)
            automation (kDrumTool, t, p.tracks[(size_t) t].fxAuto);
    }
    for (int b = 0; b < kMaxBassSynths; ++b)
    {
        const int tool = kFirstBassTool + b;
        if (! s.tools[(size_t) tool].active)
            continue;
        cmd (CommandType::ClearFxAuto, tool, kAllRacks, -1, -1.0f);
        automation (tool, -1, s.bass[(size_t) b].masterFxAuto);
        for (int r = 0; r < kBassRows; ++r)
            automation (tool, r, s.bass[(size_t) b].rows[(size_t) r].fxAuto);
    }
    for (int l = 0; l < kMaxLoopers; ++l)
    {
        const int tool = kFirstLooperTool + l;
        if (! s.tools[(size_t) tool].active)
            continue;
        cmd (CommandType::ClearFxAuto, tool, -1, -1, -1.0f);
        automation (tool, -1, s.loopers[(size_t) l].masterFxAuto);
    }

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
    // the loops go in a folder beside the project, named after it
    auto v = studioVarWithLoops (file.getSiblingFile (file.getFileNameWithoutExtension() + " loops"), true);
    if (auto* o = v.getDynamicObject())
        o->setProperty ("windows", windowLayout);
    return file.replaceWithText (juce::JSON::toString (v));
}

namespace
{
bool writeLoop (const LoopAudio& audio, const juce::File& file)
{
    file.deleteFile();
    std::unique_ptr<juce::OutputStream> stream = std::make_unique<juce::FileOutputStream> (file);
    if (static_cast<juce::FileOutputStream*> (stream.get())->failedToOpen())
        return false;
    juce::WavAudioFormat wav;
    // 32-bit float: overdubs can go past full scale without clipping
    auto writer = wav.createWriterFor (stream, juce::AudioFormatWriterOptions {}
                                                   .withSampleRate (audio.sampleRate)
                                                   .withNumChannels (2)
                                                   .withBitsPerSample (32)
                                                   .withSampleFormat (juce::AudioFormatWriterOptions::SampleFormat::floatingPoint));
    if (writer == nullptr)
        return false;
    const float* chans[] = { audio.left.data(), audio.right.data() };
    return writer->writeFromFloatArrays (chans, 2, audio.length);
}
} // namespace

juce::var ModularSynthProcessor::studioVarWithLoops (const juce::File& folder, bool relative)
{
    std::array<LoopFiles, kMaxLoopers> files;
    std::vector<const LoopAudio*> written;
    for (int l = 0; l < kMaxLoopers; ++l)
    {
        const int tool = kFirstLooperTool + l;
        if (! uiState.tools[(size_t) tool].active)
            continue;
        for (int slot = 0; slot < kNumPatterns; ++slot)
        {
            const auto* audio = uiState.loopers[(size_t) l].slots[(size_t) slot].audio;
            if (audio == nullptr || audio->length <= 0)
                continue;
            auto* held = const_cast<LoopAudio*> (audio); // only its file name changes
            auto file = folder.getChildFile (juce::String (toolName (tool)) + "-P" + juce::String (slot + 1) + ".wav");
            // a session save keeps a loop's file once it has one
            if (! relative && held->file.isNotEmpty() && juce::File (held->file).existsAsFile())
                file = juce::File (held->file);
            else if (std::find (written.begin(), written.end(), audio) == written.end())
            {
                folder.createDirectory();
                if (! writeLoop (*audio, file))
                    continue;
                held->file = file.getFullPathName();
                written.push_back (audio);
            }
            else
                file = juce::File (held->file);
            files[(size_t) l][(size_t) slot].file = relative ? folder.getFileName() + "/" + file.getFileName() : file.getFullPathName();
        }
    }
    return studioToVar (uiState, files);
}

void ModularSynthProcessor::loadLoops (StudioState& s, const std::array<LoopFiles, kMaxLoopers>& files, const juce::File& base,
                                       juce::String& error)
{
    juce::AudioFormatManager formats;
    formats.registerBasicFormats();
    for (int l = 0; l < kMaxLoopers; ++l)
    {
        for (int slot = 0; slot < kNumPatterns; ++slot)
        {
            const auto& f = files[(size_t) l][(size_t) slot];
            if (f.file.isEmpty())
                continue;
            const auto file = juce::File::isAbsolutePath (f.file) ? juce::File (f.file) : base.getChildFile (f.file);
            std::unique_ptr<juce::AudioFormatReader> reader (formats.createReaderFor (file));
            if (reader == nullptr || reader->lengthInSamples <= 1)
            {
                error = "Missing loop: " + file.getFullPathName();
                continue;
            }
            constexpr juce::int64 maxSamples = 48000 * 60 * 4;
            const auto length = (int) juce::jmin (reader->lengthInSamples, maxSamples);
            auto* audio = newLoop (f.bars, nullptr);
            audio->left.assign ((size_t) length, 0.0f);
            audio->right.assign ((size_t) length, 0.0f);
            float* chans[] = { audio->left.data(), audio->right.data() };
            reader->read (chans, reader->numChannels > 1 ? 2 : 1, 0, length);
            if (reader->numChannels < 2)
                audio->right = audio->left;
            audio->length = length;
            audio->bpm = f.bpm;
            audio->sampleRate = reader->sampleRate;
            audio->file = file.getFullPathName();
            s.loopers[(size_t) l].slots[(size_t) slot].audio = audio;
        }
    }
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
    std::array<LoopFiles, kMaxLoopers> loopFiles;
    if (! studioFromVar (v, s, paths, &loopFiles))
    {
        error = file.getFileName() + " is not a Modular-synth project";
        return false;
    }

    loadSamplePaths (s, paths, error);
    loadLoops (s, loopFiles, file.getParentDirectory(), error);
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

juce::String ModularSynthProcessor::trackName (int tool, int track) const
{
    if (const int b = bassIndex (tool); b >= 0)
        return bassSoundName (uiState.bass[(size_t) b].rows[(size_t) track].sound);
    if (looperIndex (tool) >= 0)
        return track == kInputTrack ? "INPUT" : "LOOP";
    return trackName (track);
}

// ---- loopers --------------------------------------------------------------------

LoopAudio* ModularSynthProcessor::newLoop (int bars, const LoopAudio* copyOf)
{
    auto audio = std::make_shared<LoopAudio>();
    if (copyOf != nullptr)
    {
        audio->left = copyOf->left;
        audio->right = copyOf->right;
        audio->length = copyOf->length;
        audio->bars = copyOf->bars;
        audio->bpm = copyOf->bpm;
        audio->sampleRate = copyOf->sampleRate;
    }
    else
    {
        audio->bars = bars;
        audio->bpm = uiState.bpm;
        audio->sampleRate = getEngineSampleRate();
    }
    loops.push_back ({ audio, juce::Time::getMillisecondCounter() });
    return audio.get();
}

std::shared_ptr<const LoopAudio> ModularSynthProcessor::holdLoop (const LoopAudio* audio) const
{
    for (const auto& h : loops)
        if (h.audio.get() == audio)
            return h.audio;
    return {};
}

void ModularSynthProcessor::holdTake (Take& take) const
{
    take.held.clear();
    auto hold = [this, &take] (const LoopAudio* audio) {
        if (audio != nullptr)
            if (auto h = holdLoop (audio))
                take.held.push_back (h);
    };
    for (const auto& l : take.start.loopers)
        for (const auto& slot : l.slots)
            hold (slot.audio);
    for (const auto& e : take.events)
        hold (e.command.loop);
    if (take.input != nullptr)
        for (const auto& in : inputs)
            if (in.get() == take.input)
                take.held.push_back (in);
}

bool ModularSynthProcessor::anyLooper() const
{
    for (int l = 0; l < kMaxLoopers; ++l)
        if (uiState.tools[(size_t) (kFirstLooperTool + l)].active)
            return true;
    return false;
}

void ModularSynthProcessor::recordLoop (int tool, int slot, bool overdub)
{
    const int l = looperIndex (tool);
    if (l < 0 || slot < 0 || slot >= kNumPatterns)
        return;
    const auto& ls = uiState.loopers[(size_t) l].slots[(size_t) slot];
    const bool onTop = overdub && ls.audio != nullptr && ls.audio->length > 0;
    LoopAudio* audio = nullptr;
    if (onTop)
    {
        audio = newLoop (0, ls.audio);
    }
    else
    {
        // room for the bars at this tempo, and some to spare in case it slows
        audio = newLoop (ls.bars, nullptr);
        const double stepLen = audio->sampleRate * 60.0 / (double) uiState.bpm / 4.0;
        const auto room = (size_t) (stepLen * kNumSteps * ls.bars * 1.25) + (size_t) inputLatency.load() + 1024;
        audio->left.assign (room, 0.0f);
        audio->right.assign (room, 0.0f);
    }
    Command c;
    c.type = CommandType::RecordLoop;
    c.tool = tool;
    c.index = slot;
    c.value = onTop ? 1.0f : 0.0f;
    c.loop = audio;
    send (c);
    recordingLoops[(size_t) l] = audio; // until the audio thread says otherwise
}

void ModularSynthProcessor::cancelLoopRecord (int tool)
{
    Command c;
    c.type = CommandType::CancelLoopRecord;
    c.tool = tool;
    send (c);
}

void ModularSynthProcessor::pushLayer (int looper, int slot, const LoopAudio* previous)
{
    auto& stack = layers[(size_t) looper][(size_t) slot];
    stack.push_back (holdLoop (previous)); // null = it was empty
    if (stack.size() > kMaxLayers)
        stack.erase (stack.begin());
}

void ModularSynthProcessor::setLoopAudio (int tool, int slot, const LoopAudio* audio)
{
    const int l = looperIndex (tool);
    if (l < 0 || slot < 0 || slot >= kNumPatterns)
        return;
    const auto* was = uiState.loopers[(size_t) l].slots[(size_t) slot].audio;
    if (was == audio)
        return;
    pushLayer (l, slot, was);
    Command c;
    c.type = CommandType::SetLoopAudio;
    c.tool = tool;
    c.index = slot;
    c.loop = const_cast<LoopAudio*> (audio);
    send (c);
}

bool ModularSynthProcessor::undoLoopLayer (int tool, int slot)
{
    const int l = looperIndex (tool);
    if (l < 0 || slot < 0 || slot >= kNumPatterns || layers[(size_t) l][(size_t) slot].empty())
        return false;
    auto& stack = layers[(size_t) l][(size_t) slot];
    const auto previous = stack.back();
    stack.pop_back();
    Command c;
    c.type = CommandType::SetLoopAudio;
    c.tool = tool;
    c.index = slot;
    c.loop = const_cast<LoopAudio*> (previous.get());
    send (c);
    return true;
}

int ModularSynthProcessor::loopLayers (int tool, int slot) const
{
    const int l = looperIndex (tool);
    return l < 0 || slot < 0 || slot >= kNumPatterns ? 0 : (int) layers[(size_t) l][(size_t) slot].size();
}

ModularSynthProcessor::LooperReadout ModularSynthProcessor::looperReadout (int tool) const
{
    LooperReadout r;
    const int l = looperIndex (tool);
    if (l < 0)
        return r;
    r.status = (LooperEngine::Status) looperStatus[(size_t) l].load();
    r.slot = looperSlot[(size_t) l].load();
    r.overdub = looperOverdub[(size_t) l].load();
    r.progress = looperProgress[(size_t) l].load();
    r.countBeats = looperCount[(size_t) l].load();
    r.position = looperPosition[(size_t) l].load();
    return r;
}

void ModularSynthProcessor::collectLoops()
{
    // A loop nothing refers to any more (the studio, undo, a take, a
    // recording) is let go once the audio thread has surely moved on from it.
    const auto now = juce::Time::getMillisecondCounter();
    for (auto& h : loops)
    {
        bool used = h.audio.use_count() > 1;
        for (const auto& l : uiState.loopers)
            for (const auto& slot : l.slots)
                used = used || slot.audio == h.audio.get();
        for (const auto& r : recordingLoops)
            used = used || r.load() == h.audio.get();
        if (used)
            h.lastUsed = now;
    }
    loops.erase (std::remove_if (loops.begin(), loops.end(), [now] (const HeldLoop& h) { return now - h.lastUsed > 10000; }),
                 loops.end());

    // the newest two may belong to a take not collected yet
    for (size_t i = 0; i + 2 < inputs.size();)
    {
        if (inputs[i].use_count() == 1 && inputs[i].get() != recordingInput.load())
            inputs.erase (inputs.begin() + (std::ptrdiff_t) i);
        else
            ++i;
    }
}

void ModularSynthProcessor::topUpInputChunks()
{
    if (nextInput.load() == nullptr && pendingInput != nullptr)
        inputs.push_back (std::move (pendingInput)); // the audio thread took it
    if (! anyLooper())
        return;
    // a take's input recording waiting for the next REC
    if (pendingInput == nullptr)
    {
        pendingInput = std::make_shared<InputRecording>();
        nextInput = pendingInput.get();
    }
    // and chunks for it to fill: well over ten seconds ahead
    const int free = chunkFifo.getFreeSpace();
    if (free > 16)
    {
        const auto scope = chunkFifo.write (free - 16);
        for (int i = 0; i < scope.blockSize1; ++i)
            chunkSlots[(size_t) (scope.startIndex1 + i)] = InputRecording::makeChunk();
        for (int i = 0; i < scope.blockSize2; ++i)
            chunkSlots[(size_t) (scope.startIndex2 + i)] = InputRecording::makeChunk();
    }
}

float* ModularSynthProcessor::takeChunk()
{
    const auto scope = chunkFifo.read (1);
    if (scope.blockSize1 > 0)
        return chunkSlots[(size_t) scope.startIndex1];
    if (scope.blockSize2 > 0)
        return chunkSlots[(size_t) scope.startIndex2];
    return nullptr;
}

std::optional<Take> ModularSynthProcessor::takeFinishedRecording()
{
    const int index = finishedTake.exchange (-1);
    if (index < 0)
        return std::nullopt;

    lastTake = takes[(size_t) index];
    holdTake (*lastTake);
    return lastTake;
}

void ModularSynthProcessor::syncPatternFromEngine()
{
    // A tool switches to a queued pattern on its own at the end of a bar;
    // mirror that once it has happened.
    auto sync = [this] (int tool, int& pattern, int& queuedPattern) {
        if (queuedPattern >= 0 && queuedPatterns[(size_t) tool].load() < 0 && currentPatterns[(size_t) tool].load() == queuedPattern)
        {
            pattern = queuedPattern;
            queuedPattern = -1;
        }
    };
    sync (kDrumTool, uiState.drums.pattern, uiState.drums.queuedPattern);
    for (int b = 0; b < kMaxBassSynths; ++b)
        sync (kFirstBassTool + b, uiState.bass[(size_t) b].pattern, uiState.bass[(size_t) b].queuedPattern);
    for (int l = 0; l < kMaxLoopers; ++l)
        sync (kFirstLooperTool + l, uiState.loopers[(size_t) l].pattern, uiState.loopers[(size_t) l].queuedPattern);

    // loops whose recording finished: the slot now plays them
    const auto scope = finishedFifo.read (finishedFifo.getNumReady());
    auto take = [this] (const FinishedLoop& f) {
        auto& slot = uiState.loopers[(size_t) f.looper].slots[(size_t) f.slot];
        if (slot.audio != f.audio)
        {
            pushLayer (f.looper, f.slot, slot.audio);
            slot.audio = f.audio;
        }
    };
    for (int i = 0; i < scope.blockSize1; ++i)
        take (finishedLoops[(size_t) (scope.startIndex1 + i)]);
    for (int i = 0; i < scope.blockSize2; ++i)
        take (finishedLoops[(size_t) (scope.startIndex2 + i)]);

    topUpInputChunks();
    const auto now = juce::Time::getMillisecondCounter();
    if (now - lastCollect > 1000)
    {
        lastCollect = now;
        collectLoops();
    }
}

float ModularSynthProcessor::toolHistory (int tool, int age) const
{
    const int pos = (historyPos.load() - 1 - age + kHistoryLength * 4) % kHistoryLength;
    return toolHistoryValues[(size_t) (tool * kHistoryLength + pos)].load (std::memory_order_relaxed);
}

float ModularSynthProcessor::trackHistory (int tool, int t, int age) const
{
    const int pos = (historyPos.load() - 1 - age + kHistoryLength * 4) % kHistoryLength;
    return history[(size_t) (lane (tool, t) * kHistoryLength + pos)].load (std::memory_order_relaxed);
}

void ModularSynthProcessor::copyTrackScope (int tool, int t, float* dest, int count) const
{
    const int end = scopePos.load();
    const auto base = (size_t) (lane (tool, t) * kScopeLength);
    for (int i = 0; i < count; ++i)
    {
        const int pos = (end - count + i + kScopeLength * 4) % kScopeLength;
        dest[i] = scopeSamples[base + (size_t) pos].load (std::memory_order_relaxed);
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
    const auto in = layouts.getMainInputChannelSet();
    return layouts.getMainOutputChannelSet() == juce::AudioChannelSet::stereo()
        && (in.isDisabled() || in == juce::AudioChannelSet::mono() || in == juce::AudioChannelSet::stereo());
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
    takeInput = nullptr;
    recordingInput = nullptr;
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
        // loopers waiting for the bar (or counting in) to record start over
        // in the take, count-in and all
        std::array<Command, kMaxLoopers> waiting;
        int numWaiting = 0;
        for (int l = 0; l < kMaxLoopers; ++l)
        {
            const auto& looper = engine.looper (l);
            if (looper.status() != LooperEngine::Status::Waiting && looper.status() != LooperEngine::Status::CountIn)
                continue;
            auto& w = waiting[(size_t) numWaiting++];
            w.type = CommandType::RecordLoop;
            w.tool = kFirstLooperTool + l;
            w.index = looper.recordSlot();
            w.value = looper.recordOverdub() ? 1.0f : 0.0f;
            w.loop = looper.recordBuffer();
            w.track = looper.recordLatency();
        }

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
        // the input goes with the take when there is somewhere to keep it
        takeInput = nextInput.exchange (nullptr);
        take.input = takeInput;
        recordingInput = takeInput;
        recording = true;
        engine.setPlaying (true);
        for (int i = 0; i < numWaiting; ++i)
        {
            engine.apply (waiting[(size_t) i]);
            recordEvent (waiting[(size_t) i]);
        }
    }
    else if (c.type == CommandType::RecordLoop)
    {
        auto withLatency = c;
        withLatency.track = inputLatency.load();
        engine.apply (withLatency);
        recordEvent (withLatency);
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
    const int numInputs = getTotalNumInputChannels();
    float* left = buffer.getWritePointer (0);
    float* right = buffer.getNumChannels() > 1 ? buffer.getWritePointer (1) : nullptr;
    std::array<float, kHistoryWindow> scratch {}, inLeft {}, inRight {};
    float blockMasterPeak = 0.0f;
    busBlockPeak.fill (0.0f);

    for (int offset = 0; offset < numSamples;)
    {
        const int n = juce::jmin (kHistoryWindow - windowFill, numSamples - offset);

        // the input shares the buffer with the output, so it is read first
        if (numInputs > 0)
        {
            std::copy (buffer.getReadPointer (0) + offset, buffer.getReadPointer (0) + offset + n, inLeft.data());
            const int second = numInputs > 1 ? 1 : 0;
            std::copy (buffer.getReadPointer (second) + offset, buffer.getReadPointer (second) + offset + n, inRight.data());
            engine.setInput (inLeft.data(), inRight.data());
            if (recording && takeInput != nullptr)
                takeInput->append (inLeft.data(), inRight.data(), n, [this] { return takeChunk(); });
        }
        engine.process (left + offset, right != nullptr ? right + offset : scratch.data(), n);
        offset += n;
        windowFill += n;
        blockMasterPeak = juce::jmax (blockMasterPeak, engine.masterPeak());

        const int scopeStart = scopePos.load (std::memory_order_relaxed);
        for (int tool = 0; tool < kNumToolSlots; ++tool)
        {
            toolWindowPeak[(size_t) tool] = juce::jmax (toolWindowPeak[(size_t) tool], engine.toolPeak (tool));
            toolPeaks[(size_t) tool].store (engine.toolPeak (tool), std::memory_order_relaxed);
            if (! engine.toolActive (tool))
                continue;
            busBlockPeak[(size_t) tool] = juce::jmax (busBlockPeak[(size_t) tool], engine.busPeak (tool));

            const int tracks = toolTracks (tool);
            for (int t = 0; t < tracks; ++t)
            {
                const auto l = (size_t) lane (tool, t);
                const float peak = engine.trackPeak (tool, t);
                windowPeak[l] = juce::jmax (windowPeak[l], peak);
                trackPeaks[l].store (peak, std::memory_order_relaxed);
                trackNotes[l].store (engine.trackNote (tool, t), std::memory_order_relaxed);
                if (engine.trackTriggered (tool, t))
                    hitCounts[l].fetch_add (1, std::memory_order_relaxed);

                const float* out = engine.trackOutput (tool, t);
                for (int i = 0; i < n; ++i)
                    scopeSamples[l * kScopeLength + (size_t) ((scopeStart + i) % kScopeLength)].store (out[i], std::memory_order_relaxed);
            }
        }
        engine.clearTriggered();
        scopePos.store ((scopeStart + n) % kScopeLength);

        if (windowFill >= kHistoryWindow)
        {
            const int pos = historyPos.load (std::memory_order_relaxed);
            for (int l = 0; l < kLanes; ++l)
            {
                history[(size_t) (l * kHistoryLength + pos)].store (windowPeak[(size_t) l], std::memory_order_relaxed);
                windowPeak[(size_t) l] = 0.0f;
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
    for (int tool = 0; tool < kNumToolSlots; ++tool)
    {
        busPeaks[(size_t) tool].store (busBlockPeak[(size_t) tool], std::memory_order_relaxed);
        currentPatterns[(size_t) tool] = engine.toolPattern (tool);
        queuedPatterns[(size_t) tool] = engine.toolQueuedPattern (tool);
    }
    for (int l = 0; l < kMaxLoopers; ++l)
    {
        const auto& looper = engine.looper (l);
        looperStatus[(size_t) l] = (int) looper.status();
        looperSlot[(size_t) l] = looper.recordSlot();
        looperOverdub[(size_t) l] = looper.recordOverdub();
        looperProgress[(size_t) l] = looper.recordProgress();
        looperCount[(size_t) l] = looper.countInBeats();
        looperPosition[(size_t) l] = looper.loopPosition();
        recordingLoops[(size_t) l] = looper.recordBuffer();
        if (looper.finishedCount() != finishedSeen[(size_t) l])
        {
            finishedSeen[(size_t) l] = looper.finishedCount();
            const auto w = finishedFifo.write (1);
            const FinishedLoop f { l, looper.lastFinishedSlot(), looper.lastFinishedAudio() };
            if (w.blockSize1 > 0)
                finishedLoops[(size_t) w.startIndex1] = f;
            else if (w.blockSize2 > 0)
                finishedLoops[(size_t) w.startIndex2] = f;
        }
    }
    playingFlag = engine.isPlaying();
    recordingFlag = recording;
    stepIndex = engine.currentStep();
    stepPositionValue = engine.position();
    recordedSampleCount = recordPosition;
}

// ---- plugin plumbing --------------------------------------------------------

juce::AudioProcessorEditor* ModularSynthProcessor::createEditor()
{
    return new ModularSynthEditor (*this);
}

void ModularSynthProcessor::getStateInformation (juce::MemoryBlock& dest)
{
    // loops wait for the next session in the app's own folder
    const auto folder = juce::File::getSpecialLocation (juce::File::userApplicationDataDirectory)
                            .getChildFile ("Modular-synth")
                            .getChildFile ("Session loops");
    auto v = studioVarWithLoops (folder, false);
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
    std::array<LoopFiles, kMaxLoopers> loopFiles;
    if (! studioFromVar (v, s, paths, &loopFiles))
        return;

    juce::String error;
    loadSamplePaths (s, paths, error);
    loadLoops (s, loopFiles, {}, error);
    replaceStudio (s);
    windowLayout = v.getProperty ("windows", {});
    ++windowLayoutVersion;
}

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new ModularSynthProcessor();
}
