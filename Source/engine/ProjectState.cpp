#include "ProjectState.h"

namespace ms
{
namespace
{
void setVoice (TrackState& t, VoiceType type)
{
    t.voice = type;
    t.voiceParams = defaultVoiceParams (type);
}

void setSteps (TrackState& t, std::initializer_list<std::pair<int, float>> steps)
{
    for (auto [step, velocity] : steps)
        t.steps[(size_t) step] = velocity;
}

float clamp01 (float v) { return juce::jlimit (0.0f, 1.0f, v); }

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
} // namespace

ProjectState makeDefaultProject()
{
    ProjectState p;
    auto& t = p.tracks;

    setVoice (t[0], VoiceType::Kick);
    setSteps (t[0], { { 0, 1.0f }, { 4, 1.0f }, { 8, 1.0f }, { 10, 0.7f }, { 12, 1.0f } });

    setVoice (t[1], VoiceType::ClickHat);
    for (int s = 0; s < kNumSteps; ++s)
        t[1].steps[(size_t) s] = (s % 2 == 0) ? 1.0f : 0.45f;

    setVoice (t[2], VoiceType::NoiseSnare);
    setSteps (t[2], { { 4, 1.0f }, { 12, 1.0f }, { 15, 0.4f } });

    setVoice (t[3], VoiceType::SineTone);
    setSteps (t[3], { { 2, 0.8f }, { 11, 0.8f } });
    t[3].level = 0.5f;

    setVoice (t[4], VoiceType::Glitch);
    setSteps (t[4], { { 6, 0.8f }, { 14, 0.8f } });
    t[4].level = 0.55f;

    setVoice (t[5], VoiceType::ClickHat); // open click
    t[5].voiceParams = { 0.55f, 0.4f, 0.8f, 0.0f };
    setSteps (t[5], { { 7, 0.7f } });

    setVoice (t[6], VoiceType::SineTone); // low pulse
    t[6].voiceParams = { 0.25f, 0.5f, 0.5f, 0.0f };

    setVoice (t[7], VoiceType::Sample);

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
                setVoice (*track, (VoiceType) c.index);
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
        case CommandType::SetStep:
            if (track != nullptr && c.index >= 0 && c.index < kNumSteps)
                track->steps[(size_t) c.index] = clamp01 (c.value);
            break;
        case CommandType::SetBpm:
            p.bpm = juce::jlimit (kMinBpm, kMaxBpm, c.value);
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
            break;
    }
}

juce::var projectToVar (const ProjectState& p)
{
    auto root = std::make_unique<juce::DynamicObject>();
    root->setProperty ("format", "modular-synth");
    root->setProperty ("version", 1);
    root->setProperty ("bpm", p.bpm);
    root->setProperty ("swing", p.swing);
    root->setProperty ("masterLevel", p.masterLevel);
    root->setProperty ("masterFx", floatArray (p.masterFx.data(), kNumFxParams));

    juce::Array<juce::var> tracks;
    for (const auto& t : p.tracks)
    {
        auto o = std::make_unique<juce::DynamicObject>();
        o->setProperty ("voice", voiceTypeName (t.voice));
        o->setProperty ("voiceParams", floatArray (t.voiceParams.data(), kNumVoiceParams));
        o->setProperty ("level", t.level);
        o->setProperty ("pan", t.pan);
        o->setProperty ("mute", t.mute);
        o->setProperty ("solo", t.solo);
        o->setProperty ("fx", floatArray (t.fx.data(), kNumFxParams));
        o->setProperty ("steps", floatArray (t.steps.data(), kNumSteps));
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
    result.masterLevel = clamp01 ((float) v.getProperty ("masterLevel", result.masterLevel));
    readFloatArray (v.getProperty ("masterFx", {}), result.masterFx.data(), kNumFxParams);

    if (auto* tracks = v.getProperty ("tracks", {}).getArray())
    {
        for (int i = 0; i < juce::jmin (kNumTracks, tracks->size()); ++i)
        {
            const auto& o = (*tracks)[i];
            auto& t = result.tracks[(size_t) i];
            setVoice (t, voiceTypeFromName (o.getProperty ("voice", "KICK").toString().toRawUTF8()));
            readFloatArray (o.getProperty ("voiceParams", {}), t.voiceParams.data(), kNumVoiceParams);
            t.level = clamp01 ((float) o.getProperty ("level", t.level));
            t.pan = clamp01 ((float) o.getProperty ("pan", t.pan));
            t.mute = (bool) o.getProperty ("mute", false);
            t.solo = (bool) o.getProperty ("solo", false);
            readFloatArray (o.getProperty ("fx", {}), t.fx.data(), kNumFxParams);
            readFloatArray (o.getProperty ("steps", {}), t.steps.data(), kNumSteps);
            samplePaths[(size_t) i] = o.getProperty ("sample", {}).toString();
        }
    }

    p = result;
    return true;
}
} // namespace ms
