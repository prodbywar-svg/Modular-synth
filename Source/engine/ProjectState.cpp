#include "ProjectState.h"

#include <algorithm>

namespace ms
{
namespace
{
void setVoice (TrackState& t, VoiceType type, Character c = Character::Digital)
{
    t.voice = type;
    t.character = c;
    t.voiceParams = defaultVoiceParams (type, c);
}

void setSteps (Pattern& p, std::initializer_list<std::pair<int, float>> steps)
{
    for (auto [step, velocity] : steps)
        p[(size_t) step] = velocity;
}

float clamp01 (float v) { return std::clamp (v, 0.0f, 1.0f); }

#if ! MS_NO_JUCE
juce::var floatArray (const float* values, int count)
{
    juce::Array<juce::var> a;
    for (int i = 0; i < count; ++i)
        a.add (values[i]);
    return a;
}

void readFloatArray (const juce::var& v, float* values, int count)
{
    if (auto* a = v.getArray())
        for (int i = 0; i < juce::jmin (count, a->size()); ++i)
            values[i] = clamp01 ((float) (*a)[i]);
}

#endif
} // namespace

ProjectState makeDefaultProject()
{
    ProjectState p;
    auto& t = p.tracks;

    setVoice (t[0], VoiceType::Kick);
    setVoice (t[1], VoiceType::ClickHat);
    setVoice (t[2], VoiceType::NoiseSnare);
    setVoice (t[3], VoiceType::SineTone);
    t[3].level = 0.5f;
    setVoice (t[4], VoiceType::Glitch);
    t[4].level = 0.55f;
    setVoice (t[5], VoiceType::ClickHat); // open click
    t[5].voiceParams = { 0.55f, 0.4f, 0.8f, 0.0f };
    setVoice (t[6], VoiceType::SineTone); // low pulse
    t[6].voiceParams = { 0.25f, 0.5f, 0.5f, 0.0f };
    setVoice (t[7], VoiceType::Sample);

    // P1: the starter groove
    {
        setSteps (t[0].patterns[0], { { 0, 1.0f }, { 4, 1.0f }, { 8, 1.0f }, { 10, 0.7f }, { 12, 1.0f } });
        for (int s = 0; s < kNumSteps; ++s)
            t[1].patterns[0][(size_t) s] = (s % 2 == 0) ? 1.0f : 0.45f;
        setSteps (t[2].patterns[0], { { 4, 1.0f }, { 12, 1.0f }, { 15, 0.4f } });
        setSteps (t[3].patterns[0], { { 2, 0.8f }, { 11, 0.8f } });
        setSteps (t[4].patterns[0], { { 6, 0.8f }, { 14, 0.8f } });
        setSteps (t[5].patterns[0], { { 7, 0.7f } });
    }
    // P2: busier, broken kick
    {
        setSteps (t[0].patterns[1], { { 0, 1.0f }, { 3, 0.8f }, { 6, 1.0f }, { 8, 1.0f }, { 11, 0.8f }, { 14, 1.0f } });
        for (int s = 0; s < kNumSteps; ++s)
            t[1].patterns[1][(size_t) s] = 0.35f + 0.65f * (float) ((s * 7) % 3 == 0);
        setSteps (t[2].patterns[1], { { 4, 1.0f }, { 12, 1.0f }, { 13, 0.5f }, { 15, 0.7f } });
        setSteps (t[4].patterns[1], { { 2, 0.8f }, { 7, 0.6f }, { 10, 0.8f }, { 15, 0.6f } });
        setSteps (t[6].patterns[1], { { 0, 0.9f }, { 8, 0.9f } });
    }
    // P3: half-time, sparse
    {
        setSteps (t[0].patterns[2], { { 0, 1.0f }, { 10, 0.8f } });
        setSteps (t[1].patterns[2], { { 0, 0.6f }, { 4, 0.6f }, { 8, 0.6f }, { 12, 0.6f } });
        setSteps (t[2].patterns[2], { { 8, 1.0f } });
        setSteps (t[3].patterns[2], { { 3, 0.8f }, { 6, 0.6f }, { 13, 0.8f } });
        setSteps (t[5].patterns[2], { { 14, 0.8f } });
    }
    // P4 starts empty

    return p;
}

bool isTransport (CommandType t)
{
    return t == CommandType::Play || t == CommandType::Stop
        || t == CommandType::RecordStart || t == CommandType::RecordStop;
}

void applyToState (ProjectState& p, const Command& c)
{
    const bool validTrack = c.track >= 0 && c.track < kNumTracks;
    auto* track = validTrack ? &p.tracks[(size_t) c.track] : nullptr;

    switch (c.type)
    {
        case CommandType::SetVoiceType:
            if (track != nullptr && c.index >= 0 && c.index < kNumVoiceTypes)
                setVoice (*track, (VoiceType) c.index, track->character);
            break;
        case CommandType::SetCharacter:
            if (track != nullptr && c.index >= 0 && c.index < kNumCharacters)
                setVoice (*track, track->voice, (Character) c.index);
            break;
        case CommandType::SetVoiceParam:
            if (track != nullptr && c.index >= 0 && c.index < kNumVoiceParams)
                track->voiceParams[(size_t) c.index] = clamp01 (c.value);
            break;
        case CommandType::SetLevel:
            if (track != nullptr) track->level = clamp01 (c.value);
            break;
        case CommandType::SetPan:
            if (track != nullptr) track->pan = clamp01 (c.value);
            break;
        case CommandType::SetMute:
            if (track != nullptr) track->mute = c.value > 0.5f;
            break;
        case CommandType::SetSolo:
            if (track != nullptr) track->solo = c.value > 0.5f;
            break;
        case CommandType::SetFxParam:
            if (c.index >= 0 && c.index < kNumFxParams)
            {
                if (track != nullptr)
                    track->fx[(size_t) c.index] = clamp01 (c.value);
                else if (c.track == -1)
                    p.masterFx[(size_t) c.index] = clamp01 (c.value);
            }
            break;
        case CommandType::SetFxAuto:
        case CommandType::ClearFxAuto:
            if (track != nullptr)
                applyFxAuto (track->fxAuto, c);
            else if (c.track == -1)
                applyFxAuto (p.masterFxAuto, c);
            else if (c.track == kAllRacks)
            {
                applyFxAuto (p.masterFxAuto, c);
                for (auto& t : p.tracks)
                    applyFxAuto (t.fxAuto, c);
            }
            break;
        case CommandType::SetStep:
        {
            const int pattern = c.index / kNumSteps, step = c.index % kNumSteps;
            if (track != nullptr && c.index >= 0 && pattern < kNumPatterns)
                track->patterns[(size_t) pattern][(size_t) step] = clamp01 (c.value);
            break;
        }
        case CommandType::SelectPattern:
            if (c.index < 0 || c.index >= kNumPatterns)
                break;
            if (c.value > 0.5f)
                p.queuedPattern = c.index == p.pattern ? -1 : c.index;
            else
            {
                p.pattern = c.index;
                p.queuedPattern = -1;
            }
            break;
        case CommandType::SetBpm:
            p.bpm = std::clamp (c.value, kMinBpm, kMaxBpm);
            break;
        case CommandType::SetSwing:
            p.swing = clamp01 (c.value);
            break;
        case CommandType::SetMasterLevel:
            p.masterLevel = clamp01 (c.value);
            break;
        case CommandType::SetSample:
            if (track != nullptr) track->sample = c.sample;
            break;
        case CommandType::Trigger:
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
            break;
    }
}

#if ! MS_NO_JUCE
juce::var fxToVar (const FxParams& fx)
{
    auto o = std::make_unique<juce::DynamicObject>();
    for (int i = 0; i < kNumFxParams; ++i)
        o->setProperty (fxParamName (i), fx[(size_t) i]);
    return juce::var (o.release());
}

void fxFromVar (const juce::var& v, FxParams& fx)
{
    if (auto* o = v.getDynamicObject())
    {
        for (const auto& prop : o->getProperties())
        {
            const int i = fxParamFromName (prop.name.toString().toRawUTF8());
            if (i >= 0)
                fx[(size_t) i] = clamp01 ((float) prop.value);
        }
    }
    else if (auto* a = v.getArray())
    {
        // version 1 files: CUTOFF RESO BITS DOWNSMP DLY-TIME DLY-FDBK DLY-MIX REV-SIZE REV-MIX
        auto at = [a] (int i) { return i < a->size() ? clamp01 ((float) (*a)[i]) : 0.0f; };
        fx[fxCutoff] = at (0);
        fx[fxResonance] = at (1);
        fx[fxCrushBits] = at (2);
        fx[fxCrushRate] = at (3);
        const float oldDelayMs = 10.0f + at (4) * 990.0f;
        fx[fxDelayTime] = clamp01 (std::log (oldDelayMs / 5.0f) / std::log (400.0f));
        fx[fxDelaySync] = 0.0f;
        fx[fxDelayFeedback] = at (5);
        fx[fxDelayMix] = at (6);
        fx[fxReverbTime] = clamp01 (0.2f + at (7) * 0.5f);
        fx[fxReverbMix] = at (8);
    }
}

juce::var projectToVar (const ProjectState& p)
{
    auto root = std::make_unique<juce::DynamicObject>();
    root->setProperty ("format", "modular-synth");
    root->setProperty ("version", 2);
    root->setProperty ("bpm", p.bpm);
    root->setProperty ("swing", p.swing);
    root->setProperty ("pattern", p.pattern);
    root->setProperty ("masterLevel", p.masterLevel);
    root->setProperty ("masterFx", fxToVar (p.masterFx));
    root->setProperty ("masterFxAuto", p.masterFxAuto.toVar());

    juce::Array<juce::var> tracks;
    for (const auto& t : p.tracks)
    {
        auto o = std::make_unique<juce::DynamicObject>();
        o->setProperty ("voice", voiceTypeName (t.voice));
        o->setProperty ("character", characterName (t.character));
        o->setProperty ("voiceParams", floatArray (t.voiceParams.data(), kNumVoiceParams));
        o->setProperty ("level", t.level);
        o->setProperty ("pan", t.pan);
        o->setProperty ("mute", t.mute);
        o->setProperty ("solo", t.solo);
        o->setProperty ("fx", fxToVar (t.fx));
        o->setProperty ("fxAuto", t.fxAuto.toVar());
        juce::Array<juce::var> patterns;
        for (const auto& pat : t.patterns)
            patterns.add (floatArray (pat.data(), kNumSteps));
        o->setProperty ("patterns", patterns);
        o->setProperty ("sample", t.sample != nullptr ? t.sample->path : juce::String());
        tracks.add (juce::var (o.release()));
    }
    root->setProperty ("tracks", tracks);

    return juce::var (root.release());
}

bool projectFromVar (const juce::var& v, ProjectState& p, std::array<juce::String, kNumTracks>& samplePaths)
{
    if (v.getProperty ("format", {}).toString() != "modular-synth")
        return false;

    ProjectState result;
    result.bpm = juce::jlimit (kMinBpm, kMaxBpm, (float) v.getProperty ("bpm", result.bpm));
    result.swing = clamp01 ((float) v.getProperty ("swing", result.swing));
    result.pattern = juce::jlimit (0, kNumPatterns - 1, (int) v.getProperty ("pattern", 0));
    result.masterLevel = clamp01 ((float) v.getProperty ("masterLevel", result.masterLevel));
    fxFromVar (v.getProperty ("masterFx", {}), result.masterFx);
    result.masterFxAuto.fromVar (v.getProperty ("masterFxAuto", {}));

    if (auto* tracks = v.getProperty ("tracks", {}).getArray())
    {
        for (int i = 0; i < juce::jmin (kNumTracks, tracks->size()); ++i)
        {
            const auto& o = (*tracks)[i];
            auto& t = result.tracks[(size_t) i];
            setVoice (t, voiceTypeFromName (o.getProperty ("voice", "KICK").toString().toRawUTF8()),
                      characterFromName (o.getProperty ("character", "DIGITAL").toString().toRawUTF8()));
            readFloatArray (o.getProperty ("voiceParams", {}), t.voiceParams.data(), kNumVoiceParams);
            t.level = clamp01 ((float) o.getProperty ("level", t.level));
            t.pan = clamp01 ((float) o.getProperty ("pan", t.pan));
            t.mute = (bool) o.getProperty ("mute", false);
            t.solo = (bool) o.getProperty ("solo", false);
            fxFromVar (o.getProperty ("fx", {}), t.fx);
            t.fxAuto.fromVar (o.getProperty ("fxAuto", {}));

            if (auto* patterns = o.getProperty ("patterns", {}).getArray())
            {
                for (int pi = 0; pi < juce::jmin (kNumPatterns, patterns->size()); ++pi)
                    readFloatArray ((*patterns)[pi], t.patterns[(size_t) pi].data(), kNumSteps);
            }
            else // version 1: one pattern
            {
                readFloatArray (o.getProperty ("steps", {}), t.patterns[0].data(), kNumSteps);
            }
            samplePaths[(size_t) i] = o.getProperty ("sample", {}).toString();
        }
    }

    p = result;
    return true;
}
#endif
} // namespace ms
