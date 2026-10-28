// Engine tests: run headless (no audio device, no window) on every build.

#include "engine/Hr16Bank.h"
#include "engine/OfflineRenderer.h"
#include "engine/StudioState.h"

#include <cmath>
#include <cstdio>
#include <string>

using namespace ms;

namespace
{
int failures = 0;

#define CHECK(cond)                                                              \
    do                                                                           \
    {                                                                            \
        if (! (cond))                                                            \
        {                                                                        \
            std::printf ("  FAIL %s:%d  %s\n", __FILE__, __LINE__, #cond);       \
            ++failures;                                                          \
        }                                                                        \
    } while (false)

constexpr double kSr = 48000.0;

struct Rendered
{
    std::vector<float> l, r;
    float peak() const
    {
        float p = 0.0f;
        for (size_t i = 0; i < l.size(); ++i)
            p = std::max ({ p, std::abs (l[i]), std::abs (r[i]) });
        return p;
    }
    bool finite() const
    {
        for (size_t i = 0; i < l.size(); ++i)
            if (! std::isfinite (l[i]) || ! std::isfinite (r[i]))
                return false;
        return true;
    }
    // first sample index at or after from whose level exceeds threshold
    std::int64_t onset (std::int64_t from = 0, float threshold = 0.001f) const
    {
        for (size_t i = (size_t) from; i < l.size(); ++i)
            if (std::abs (l[i]) > threshold || std::abs (r[i]) > threshold)
                return (std::int64_t) i;
        return -1;
    }
};

Rendered render (const Take& take, int solo, std::int64_t length)
{
    Rendered out;
    renderTake (take, drumStem (solo), length, [&] (const float* l, const float* r, int n) {
        out.l.insert (out.l.end(), l, l + n);
        out.r.insert (out.r.end(), r, r + n);
    });
    return out;
}

float maxDifferenceOfStemSum (const Take& take, std::int64_t length)
{
    const auto master = render (take, -1, length);
    std::vector<float> sumL (master.l.size(), 0.0f), sumR (master.r.size(), 0.0f);
    for (int t = 0; t < kNumTracks; ++t)
    {
        const auto stem = render (take, t, length);
        for (size_t i = 0; i < sumL.size(); ++i)
        {
            sumL[i] += stem.l[i];
            sumR[i] += stem.r[i];
        }
    }
    float diff = 0.0f;
    for (size_t i = 0; i < sumL.size(); ++i)
        diff = std::max ({ diff, std::abs (sumL[i] - master.l[i]), std::abs (sumR[i] - master.r[i]) });
    return diff;
}

void testDefaultKitMakesSound()
{
    std::printf ("default kit makes sound\n");
    const auto take = makePatternTake (makeDefaultProject(), 2, kSr);
    const auto out = render (take, -1, take.length);
    CHECK (out.finite());
    CHECK (out.peak() > 0.1f);
    CHECK (out.peak() < 8.0f);

    for (int t : { 0, 1, 2, 3, 4, 5 })
        CHECK (render (take, t, take.length).peak() > 0.01f);
    CHECK (render (take, 6, take.length).peak() == 0.0f); // no steps
    CHECK (render (take, 7, take.length).peak() == 0.0f); // no sample loaded
}

void testEachVoiceType()
{
    std::printf ("every voice type renders\n");
    SampleData sample;
    sample.sampleRate = 44100.0;
    for (int i = 0; i < 4410; ++i)
        sample.samples.push_back (std::sin ((float) i * 0.05f));

    for (int v = 0; v < kNumVoiceTypes; ++v)
    {
        ProjectState p;
        p.tracks[0].voice = (VoiceType) v;
        p.tracks[0].voiceParams = defaultVoiceParams ((VoiceType) v, Character::Digital);
        p.tracks[0].sample = &sample;
        p.tracks[0].patterns[0][0] = 1.0f;
        const auto take = makePatternTake (p, 1, kSr);
        const auto out = render (take, -1, take.length);
        CHECK (out.finite());
        CHECK (out.peak() > 0.01f);
        CHECK (out.onset() >= 0 && out.onset() < 50);
    }
}

void testDeterministic()
{
    std::printf ("renders are identical every time\n");
    const auto take = makePatternTake (makeDefaultProject(), 1, kSr);
    const auto a = render (take, -1, take.length);
    const auto b = render (take, -1, take.length);
    CHECK (a.l == b.l);
    CHECK (a.r == b.r);
}

void testStemsSumToMaster()
{
    std::printf ("stems add up to the master (linear master effects)\n");
    auto p = makeDefaultProject();
    p.tracks[0].fx[fxReverbMix] = 0.3f;
    p.tracks[2].fx[fxCutoff] = 0.6f;
    p.masterFx[fxDelayMix] = 0.4f;
    p.masterFx[fxReverbMix] = 0.25f;
    p.masterFx[fxCutoff] = 0.8f;
    const auto take = makePatternTake (p, 1, kSr);
    const auto diff = maxDifferenceOfStemSum (take, take.length + (std::int64_t) kSr);
    std::printf ("  max difference %g\n", (double) diff);
    CHECK (diff < 1.0e-4f);
}

void testStemsCarryMasterEffects()
{
    std::printf ("a stem carries the master reverb\n");
    ProjectState p;
    p.tracks[0].patterns[0][0] = 1.0f;
    auto take = makePatternTake (p, 1, kSr);
    const auto len = take.length + (std::int64_t) kSr * 2;
    const auto dry = render (take, 0, len);

    take.start.masterFx[fxReverbMix] = 0.6f;
    take.start.masterFx[fxReverbSync] = 0.0f;
    take.start.masterFx[fxReverbTime] = 0.85f; // a long tail, about 9 s
    const auto wet = render (take, 0, len);

    // long after the kick has died, only the master reverb tail remains
    const auto late = (size_t) (take.length + (std::int64_t) (kSr * 0.5));
    float dryLate = 0.0f, wetLate = 0.0f;
    for (size_t i = late; i < late + 2000; ++i)
    {
        dryLate = std::max (dryLate, std::abs (dry.l[i]));
        wetLate = std::max (wetLate, std::abs (wet.l[i]));
    }
    CHECK (dryLate < 1.0e-4f);
    CHECK (wetLate > 1.0e-3f);
}

void testSwingTiming()
{
    std::printf ("swing delays odd steps\n");
    ProjectState p;
    p.bpm = 120.0f;
    p.swing = 0.5f;
    p.tracks[0].voice = VoiceType::ClickHat;
    p.tracks[0].voiceParams = defaultVoiceParams (VoiceType::ClickHat, Character::Digital);
    p.tracks[0].patterns[0][1] = 1.0f;
    const auto take = makePatternTake (p, 1, kSr);
    const auto out = render (take, -1, take.length);
    const double stepLen = kSr * 60.0 / 120.0 / 4.0; // 6000
    const auto expected = (std::int64_t) std::ceil (stepLen + 0.5 * 0.5 * stepLen);
    std::printf ("  onset %lld expected %lld\n", (long long) out.onset(), (long long) expected);
    CHECK (std::llabs (out.onset() - expected) <= 1);
}

void testRecordedEventsReplay()
{
    std::printf ("recorded pad hits and mutes replay at the right sample\n");
    ProjectState p;
    p.tracks[0].patterns[0] = {}; // silent pattern
    p.tracks[1].voice = VoiceType::NoiseSnare;
    p.tracks[1].voiceParams = defaultVoiceParams (VoiceType::NoiseSnare, Character::Digital);
    p.tracks[2].voice = VoiceType::Kick;
    p.tracks[2].voiceParams = defaultVoiceParams (VoiceType::Kick, Character::Digital);
    for (int s = 0; s < kNumSteps; s += 4)
        p.tracks[2].patterns[0][(size_t) s] = 1.0f;

    Take take;
    take.start = studioWithDrums (p);
    take.sampleRate = kSr;
    take.length = (std::int64_t) kSr * 4;

    Command hit;
    hit.type = CommandType::Trigger;
    hit.track = 1;
    hit.value = 1.0f;
    take.events.push_back ({ 12345, hit });

    Command mute;
    mute.type = CommandType::SetMute;
    mute.track = 2;
    mute.value = 1.0f;
    take.events.push_back ({ (std::int64_t) kSr * 2, mute });

    const auto snare = render (take, 1, take.length);
    CHECK (snare.onset() >= 12345 && snare.onset() < 12345 + 50);

    const auto kick = render (take, 2, take.length);
    CHECK (kick.onset() == 0);
    CHECK (kick.onset ((std::int64_t) kSr * 2) == -1); // silent once muted
}

void testParameterChangeIsRecorded()
{
    std::printf ("recorded knob moves change the sound\n");
    ProjectState p;
    p.tracks[0].patterns[0][0] = 1.0f;
    Take take = makePatternTake (p, 2, kSr);
    const auto before = render (take, 0, take.length);

    Command level;
    level.type = CommandType::SetLevel;
    level.track = 0;
    level.value = 0.0f;
    take.events.push_back ({ take.length / 2 - 10, level }); // just before loop 2
    const auto after = render (take, 0, take.length);

    CHECK (after.onset() == 0);
    CHECK (after.onset (take.length / 2) == -1);
    CHECK (before.onset (take.length / 2) >= take.length / 2);
}

void testExportFiles()
{
    std::printf ("export writes a master and one stem per sounding track\n");
    auto folder = juce::File::getSpecialLocation (juce::File::tempDirectory).getChildFile ("ms-export-test");
    folder.deleteRecursively();

    auto p = makeDefaultProject();
    p.tracks[4].mute = true; // muted tracks are skipped
    p.masterFx[fxReverbMix] = 0.3f;
    const auto take = makePatternTake (p, 2, kSr);

    ExportOptions opt;
    opt.folder = folder;
    opt.baseName = "test";
    opt.trackNames[0] = "Kick";

    float lastProgress = 0.0f;
    bool monotonic = true;
    const auto result = exportTake (take, opt, [&] (float f) {
        monotonic = monotonic && f >= lastProgress - 1.0e-6f;
        lastProgress = f;
        return true;
    });

    CHECK (result.ok);
    CHECK (monotonic);
    CHECK (lastProgress > 0.99f);
    // master, the drum machine, tracks 1,2,3,4,6 (5 is muted, 7 has no steps,
    // 8 has no sample)
    CHECK (result.files.size() == 7);
    CHECK (folder.getChildFile ("test_Master.wav").existsAsFile());
    CHECK (folder.getChildFile ("test_DRUMS.wav").existsAsFile());
    CHECK (folder.getChildFile ("test_DRUMS-01-Kick.wav").existsAsFile());
    CHECK (! folder.getChildFile ("test_DRUMS-05-GLITCH.wav").existsAsFile());
    CHECK (result.lengthSamples > take.length); // reverb tail kept
    CHECK (result.lengthSamples < take.length + (std::int64_t) (kSr * 10.5));

    juce::AudioFormatManager formats;
    formats.registerBasicFormats();
    for (const auto& f : result.files)
    {
        std::unique_ptr<juce::AudioFormatReader> reader (formats.createReaderFor (f));
        CHECK (reader != nullptr);
        if (reader != nullptr)
        {
            CHECK (reader->bitsPerSample == 24);
            CHECK (reader->numChannels == 2);
            CHECK ((std::int64_t) reader->lengthInSamples == result.lengthSamples);
            CHECK (reader->sampleRate == kSr);
        }
    }

    folder.deleteRecursively();
}

void testSaveAndOpen()
{
    std::printf ("projects survive save and open\n");
    auto p = makeDefaultProject();
    p.bpm = 141.5f;
    p.swing = 0.25f;
    p.tracks[3].mute = true;
    p.tracks[5].fx[fxDelayMix] = 0.4f;
    p.masterFx[fxCrushBits] = 0.5f;
    p.masterFx[fxPsyMix] = 0.3f;
    p.tracks[2].character = Character::Hr16;
    p.tracks[1].patterns[3][5] = 0.75f;
    p.pattern = 2;

    const auto json = juce::JSON::toString (projectToVar (p));
    ProjectState q;
    std::array<juce::String, kNumTracks> paths;
    CHECK (projectFromVar (juce::JSON::parse (json), q, paths));
    CHECK (q.bpm == p.bpm);
    CHECK (q.pattern == 2);
    CHECK (q.swing == p.swing);
    CHECK (q.tracks[3].mute);
    CHECK (q.tracks[5].fx == p.tracks[5].fx);
    CHECK (q.masterFx == p.masterFx);
    for (int t = 0; t < kNumTracks; ++t)
    {
        CHECK (q.tracks[(size_t) t].voice == p.tracks[(size_t) t].voice);
        CHECK (q.tracks[(size_t) t].patterns == p.tracks[(size_t) t].patterns);
        CHECK (q.tracks[(size_t) t].character == p.tracks[(size_t) t].character);
        CHECK (q.tracks[(size_t) t].voiceParams == p.tracks[(size_t) t].voiceParams);
    }

    CHECK (! projectFromVar (juce::JSON::parse ("{\"format\":\"other\"}"), q, paths));
}

void testEveryCharacterRenders()
{
    std::printf ("every drum renders in LIVE, DIGITAL and HR-16\n");
    for (int c = 0; c < kNumCharacters; ++c)
    {
        for (int v = 0; v < (int) VoiceType::Sample; ++v)
        {
            ProjectState p;
            auto& t = p.tracks[0];
            t.voice = (VoiceType) v;
            t.character = (Character) c;
            t.voiceParams = defaultVoiceParams (t.voice, t.character);
            t.patterns[0][0] = 1.0f;
            const auto take = makePatternTake (p, 1, kSr);
            const auto out = render (take, -1, take.length);
            const bool ok = out.finite() && out.peak() > 0.01f && out.peak() < 2.0f;
            if (! ok)
                std::printf ("  %s / %s peak %g\n", characterName ((Character) c), voiceTypeName ((VoiceType) v), (double) out.peak());
            CHECK (ok);
        }
    }

    for (int v = 0; v < (int) VoiceType::Sample; ++v)
        for (int i = 0; i < hr16SoundCount ((VoiceType) v); ++i)
            CHECK (hr16Sample ((VoiceType) v, i) != nullptr);
}

void testPatternQueue()
{
    std::printf ("a queued pattern starts only when the current one ends\n");
    ProjectState p;
    p.bpm = 120.0f;
    p.tracks[0].patterns[0][0] = 1.0f; // P1: kick on step 1
    p.tracks[0].patterns[1][8] = 1.0f; // P2: kick on step 9
    Take take = makePatternTake (p, 3, kSr, 0);
    const auto loop = take.length / 3;
    const auto step = loop / kNumSteps;

    Command select;
    select.type = CommandType::SelectPattern;
    select.index = 1;
    select.value = 1.0f; // queue
    take.events.push_back ({ step * 3, select }); // pressed during step 4 of loop 1
    const auto out = render (take, -1, take.length);

    CHECK (out.onset() == 0);                                 // loop 1 still P1
    CHECK (out.onset (loop - 100) >= loop + step * 8 - 1);    // loop 2 is P2: nothing on step 1
    CHECK (out.onset (loop - 100) <= loop + step * 8 + 1);

    std::printf ("selecting while stopped switches straight away\n");
    EngineCore engine;
    engine.prepare (kSr, 256);
    engine.setState (p);
    engine.apply (select);
    CHECK (engine.currentPattern() == 1);
    CHECK (engine.queuedPattern() == -1);
}

void testNoteSyncedDelay()
{
    std::printf ("a delay synced to 1/4 repeats one beat later\n");
    ProjectState p;
    p.bpm = 120.0f;
    p.tracks[0].voice = VoiceType::ClickHat;
    p.tracks[0].voiceParams = defaultVoiceParams (VoiceType::ClickHat, Character::Digital);
    p.tracks[0].voiceParams[2] = 0.0f; // pure single-sample click
    p.tracks[0].patterns[0][0] = 1.0f;
    p.tracks[0].fx[fxDelaySync] = 1.0f;
    p.tracks[0].fx[fxDelayTime] = 10.0f / 18.0f; // division 10 = 1/4
    p.tracks[0].fx[fxDelayFeedback] = 0.0f;
    p.tracks[0].fx[fxDelayMix] = 1.0f;
    const auto take = makePatternTake (p, 1, kSr);
    const auto out = render (take, -1, take.length);
    const auto echo = out.onset (200, 0.01f);
    std::printf ("  echo at %lld (expected 24000)\n", (long long) echo);
    CHECK (std::llabs (echo - 24000) <= 2);

    char buf[32];
    formatFxParam (p.tracks[0].fx, fxDelayTime, buf, (int) sizeof (buf));
    CHECK (std::string (buf) == "1/4");
}

void testNewEffectsAreSane()
{
    std::printf ("grain delay, distortion, reverb and Psychosis stay finite and audible\n");
    for (int fx : { fxGrainMix, fxDistMix, fxReverbMix, fxPsyMix })
    {
        auto p = makeDefaultProject();
        p.masterFx[(size_t) fx] = 1.0f;
        p.masterFx[fxDistDrive] = 0.8f;
        p.masterFx[fxGrainPitch] = 1.0f; // +12 st
        const auto take = makePatternTake (p, 1, kSr);
        const auto out = render (take, -1, take.length + (std::int64_t) kSr);
        const bool ok = out.finite() && out.peak() > 0.05f && out.peak() < 4.0f;
        if (! ok)
            std::printf ("  %s peak %g\n", fxParamName (fx), (double) out.peak());
        CHECK (ok);
    }

    for (int type = 0; type < 4; ++type)
    {
        auto p = makeDefaultProject();
        p.masterFx[fxDistMix] = 1.0f;
        p.masterFx[fxDistType] = (float) type / 3.0f;
        p.masterFx[fxDistDrive] = 1.0f;
        const auto take = makePatternTake (p, 1, kSr);
        CHECK (render (take, -1, take.length).finite());
    }
}

void testPitchNames()
{
    std::printf ("pitches read as note names\n");
    char buf[32];
    noteNameForFrequency (41.2034f, buf, (int) sizeof (buf));
    CHECK (std::string (buf) == "E1");
    noteNameForFrequency (440.0f, buf, (int) sizeof (buf));
    CHECK (std::string (buf) == "A4");
    noteNameForFrequency (446.0f, buf, (int) sizeof (buf));
    CHECK (std::string (buf) == "A4+23c");

    // snapping lands exactly on a note
    const float v = snapVoiceParamToNote (VoiceType::Kick, Character::Digital, 0, 0.37f);
    formatVoiceParam (VoiceType::Kick, Character::Digital, 0, v, buf, (int) sizeof (buf));
    std::printf ("  kick pitch snapped: %s\n", buf);
    CHECK (std::string (buf).find ('c') == std::string::npos); // no cents
}

Rendered renderStem (const Take& take, Stem stem, std::int64_t length)
{
    Rendered out;
    renderTake (take, stem, length, [&] (const float* l, const float* r, int n) {
        out.l.insert (out.l.end(), l, l + n);
        out.r.insert (out.r.end(), r, r + n);
    });
    return out;
}

Command toolCommand (CommandType type, int tool, float value = 0.0f)
{
    Command c;
    c.type = type;
    c.tool = tool;
    c.value = value;
    return c;
}

void testStudioToolMixing()
{
    std::printf ("studio: tool mute, solo, remove and the master rack\n");
    const auto studio = studioWithDrums (makeDefaultProject());
    const auto take = makePatternTake (studio, 1, kSr);
    const auto master = renderStem (take, {}, take.length);
    const auto drums = renderStem (take, { kDrumTool, -1 }, take.length);
    CHECK (master.peak() > 0.1f);
    // with one tool, the tool's stem is the whole mix
    float diff = 0.0f;
    for (size_t i = 0; i < master.l.size(); ++i)
        diff = std::max (diff, std::abs (master.l[i] - drums.l[i]));
    CHECK (diff < 1.0e-6f);

    auto muted = studio;
    muted.tools[kDrumTool].mute = true;
    CHECK (renderStem (makePatternTake (muted, 1, kSr), {}, take.length).peak() <= 0.0f);

    auto removed = studio;
    applyToStudio (removed, toolCommand (CommandType::RemoveTool, kDrumTool));
    CHECK (! removed.tools[kDrumTool].active);
    CHECK (renderStem (makePatternTake (removed, 1, kSr), {}, take.length).peak() <= 0.0f);

    auto quiet = studio;
    quiet.masterLevel = 0.0f;
    CHECK (renderStem (makePatternTake (quiet, 1, kSr), {}, take.length).peak() <= 0.0f);

    // the studio master rack reaches every stem: a long reverb tail on the kick
    auto wet = studio;
    wet.masterFx[fxReverbMix] = 0.5f;
    wet.masterFx[fxReverbTime] = 0.8f;
    const auto wetTake = makePatternTake (wet, 1, kSr);
    const auto dryKick = renderStem (take, { kDrumTool, 0 }, take.length * 2);
    const auto wetKick = renderStem (wetTake, { kDrumTool, 0 }, take.length * 2);
    CHECK (dryKick.onset (take.length + (std::int64_t) kSr / 2, 0.0005f) == -1);
    CHECK (wetKick.onset (take.length + (std::int64_t) kSr / 2, 0.0005f) >= 0);
}

void testAddedToolJoinsTheBeat()
{
    std::printf ("studio: a drum machine added while playing lands on the beat\n");
    auto studio = makeDefaultStudio();
    applyToStudio (studio, toolCommand (CommandType::RemoveTool, kDrumTool));
    auto take = makePatternTake (studio, 2, kSr);
    const double stepLen = kSr * 60.0 / (double) studio.bpm / 4.0;
    const auto addAt = (std::int64_t) (stepLen * 2.5); // halfway through step 3
    take.events.push_back ({ addAt, toolCommand (CommandType::AddTool, kDrumTool) });

    // the default kick plays steps 1, 5, 9, 11 and 13: the first one heard is step 5
    const auto kick = renderStem (take, { kDrumTool, 0 }, take.length);
    const auto expected = (std::int64_t) std::ceil (stepLen * 4.0);
    std::printf ("  kick onset %lld expected %lld\n", (long long) kick.onset(), (long long) expected);
    CHECK (std::llabs (kick.onset() - expected) <= 1);
}

void testStudioSaveAndOpen()
{
    std::printf ("studio projects save and open; drum machine projects still open\n");
    auto s = makeDefaultStudio();
    s.bpm = 141.0f;
    s.masterLevel = 0.6f;
    s.masterFx[fxDelayMix] = 0.4f;
    s.tools[kDrumTool].solo = true;
    s.drums.tracks[2].level = 0.3f;
    s.drums.masterFx[fxReverbMix] = 0.2f;

    StudioState q;
    std::array<juce::String, kNumTracks> paths;
    CHECK (studioFromVar (juce::JSON::parse (juce::JSON::toString (studioToVar (s))), q, paths));
    CHECK (juce::exactlyEqual (q.bpm, 141.0f) && juce::exactlyEqual (q.drums.bpm, 141.0f));
    CHECK (std::abs (q.masterLevel - 0.6f) < 1.0e-6f);
    CHECK (std::abs (q.masterFx[fxDelayMix] - 0.4f) < 1.0e-6f);
    CHECK (q.tools[kDrumTool].active && q.tools[kDrumTool].solo);
    CHECK (std::abs (q.drums.tracks[2].level - 0.3f) < 1.0e-6f);
    CHECK (std::abs (q.drums.masterFx[fxReverbMix] - 0.2f) < 1.0e-6f);

    auto none = s;
    applyToStudio (none, toolCommand (CommandType::RemoveTool, kDrumTool));
    CHECK (studioFromVar (juce::JSON::parse (juce::JSON::toString (studioToVar (none))), q, paths));
    CHECK (! q.tools[kDrumTool].active);

    // a version 2 file is the drum machine, its master rack becomes the drum bus
    auto old = makeDefaultProject();
    old.bpm = 120.0f;
    old.masterFx[fxCutoff] = 0.5f;
    CHECK (studioFromVar (juce::JSON::parse (juce::JSON::toString (projectToVar (old))), q, paths));
    CHECK (juce::exactlyEqual (q.bpm, 120.0f) && q.tools[kDrumTool].active);
    CHECK (std::abs (q.drums.masterFx[fxCutoff] - 0.5f) < 1.0e-6f);
    CHECK (q.masterFx == defaultFxParams());
}

// A studio with just one bass synth (slot 1) and nothing on its patterns.
StudioState bassOnlyStudio()
{
    auto s = makeDefaultStudio();
    for (int t = 0; t < kNumToolSlots; ++t)
        s.tools[(size_t) t].active = t == kFirstBassTool;
    for (auto& row : s.bass[0].rows)
    {
        row.cable = -1;
        for (auto& p : row.velocity)
            p.fill (0.0f);
    }
    return s;
}

void setBassNote (StudioState& s, int row, int step, int note, int length, float velocity = 1.0f)
{
    auto& r = s.bass[0].rows[(size_t) row];
    r.velocity[0][(size_t) step] = velocity;
    r.notes[0][(size_t) step] = (std::int8_t) note;
    r.lengths[0][(size_t) step] = (std::uint8_t) length;
}

void testBassSoundsRender()
{
    std::printf ("bass: every sound renders, the default bass line plays\n");
    const auto def = makeDefaultStudio();
    auto take = makePatternTake (def, 1, kSr);
    const auto bassMix = renderStem (take, { kFirstBassTool, -1 }, take.length);
    CHECK (bassMix.finite());
    CHECK (bassMix.peak() > 0.05f);
    CHECK (bassMix.peak() < 4.0f);

    for (int sound = 0; sound < kNumBassSounds; ++sound)
    {
        auto s = bassOnlyStudio();
        Command c;
        c.type = CommandType::SetVoiceType;
        c.tool = kFirstBassTool;
        c.track = 0;
        c.index = sound;
        applyToStudio (s, c);
        setBassNote (s, 0, 0, 36, 4);
        const auto t = makePatternTake (s, 1, kSr);
        const auto out = renderStem (t, { kFirstBassTool, 0 }, t.length);
        std::printf ("  %-6s peak %.3f\n", bassSoundName ((BassSound) sound), (double) out.peak());
        CHECK (out.finite());
        CHECK (out.peak() > 0.02f);
        CHECK (out.peak() < 2.0f);
    }
}

void testBassPitch()
{
    std::printf ("bass: a SUB note sounds at its pitch\n");
    auto s = bassOnlyStudio();
    s.bass[0].rows[0].params[bpDrive] = 0.0f; // clean sine
    setBassNote (s, 0, 0, 45, 8); // A2 = 110 Hz
    const auto t = makePatternTake (s, 1, kSr);
    const auto out = renderStem (t, { kFirstBassTool, 0 }, t.length);
    // count rising zero crossings over 0.2 s from 0.1 s in
    int crossings = 0;
    const size_t from = (size_t) (kSr * 0.1), to = (size_t) (kSr * 0.3);
    for (size_t i = from + 1; i < to; ++i)
        if (out.l[i - 1] <= 0.0f && out.l[i] > 0.0f)
            ++crossings;
    const double hz = crossings / 0.2;
    std::printf ("  measured %.1f Hz (expected 110)\n", hz);
    CHECK (std::abs (hz - 110.0) < 6.0);
}

void testBassCutGroups()
{
    std::printf ("bass: rows joined by a cable cut each other, others ring on\n");
    auto s = bassOnlyStudio();
    // row 0 holds a long note, row 1 plays on step 5
    setBassNote (s, 0, 0, 36, 16);
    setBassNote (s, 1, 4, 43, 1);
    const double stepLen = kSr * 60.0 / (double) s.bpm / 4.0;
    const auto probe = (std::int64_t) (stepLen * 6.0); // two steps after row 1 played

    auto free = makePatternTake (s, 1, kSr);
    const auto ringing = renderStem (free, { kFirstBassTool, 0 }, free.length);
    CHECK (ringing.onset (probe) >= 0 && ringing.onset (probe) < probe + 200);

    Command cable;
    cable.type = CommandType::SetCable;
    cable.tool = kFirstBassTool;
    cable.track = 1;
    cable.index = 0;
    applyToStudio (s, cable);
    CHECK (cutGroups (s.bass[0])[0] == cutGroups (s.bass[0])[1]);
    CHECK (cutGroups (s.bass[0])[2] != cutGroups (s.bass[0])[0]);
    auto cut = makePatternTake (s, 1, kSr);
    const auto silenced = renderStem (cut, { kFirstBassTool, 0 }, cut.length);
    CHECK (silenced.onset (probe) == -1);
}

void testScaleLock()
{
    std::printf ("bass: notes snap into the key and scale\n");
    // C minor: C D Eb F G Ab Bb
    CHECK (snapToScale (40, 0, 2) == 39);     // E -> Eb
    CHECK (snapToScale (37, 0, 2) == 36);     // C# -> C (lower on a tie)
    CHECK (snapToScale (45, 0, 2) == 44);     // A -> Ab
    CHECK (stepInScale (36, 0, 2, 1) == 38);  // C -> D
    CHECK (stepInScale (36, 0, 2, 2) == 39);  // C -> Eb
    CHECK (stepInScale (36, 0, 2, -1) == 34); // C -> Bb
    CHECK (snapToScale (41, 0, 0) == 41);     // chromatic leaves notes alone
    CHECK (snapToScale (10, 0, 2) == kBassLowNote);

    auto b = makeDefaultBass();
    b.rows[0].notes[0][0] = 40;
    Command scale;
    scale.type = CommandType::SetScale;
    scale.index = 1; // C major: E stays, Eb would go
    applyToBass (b, scale);
    CHECK (b.rows[0].notes[0][0] == 40);
    Command key;
    key.type = CommandType::SetKey;
    key.index = 2; // D major: E is in, C becomes C# or B
    applyToBass (b, key);
    CHECK (b.rows[0].notes[0][0] == 40);
    for (const auto& row : b.rows)
        for (const auto& p : row.notes)
            for (auto n : p)
                CHECK (noteInScale (n, 2, 1));
}

void testBassSaveAndOpen()
{
    std::printf ("bass: saved and opened with the studio\n");
    auto s = makeDefaultStudio();
    s.tools[kFirstBassTool + 2].active = true;
    auto& b = s.bass[2];
    b.key = 7;
    b.scale = 4;
    b.rows[3].cable = 5;
    b.rows[3].sound = BassSound::Reese;
    b.rows[3].params[bpRelease] = 0.25f;
    b.rows[3].velocity[1][9] = 0.5f;
    b.rows[3].notes[1][9] = 50;
    b.rows[3].lengths[1][9] = 3;
    b.masterFx[fxChorusMix] = 0.4f;

    StudioState q;
    std::array<juce::String, kNumTracks> paths;
    CHECK (studioFromVar (juce::JSON::parse (juce::JSON::toString (studioToVar (s))), q, paths));
    CHECK (q.tools[kFirstBassTool].active && q.tools[kFirstBassTool + 2].active && ! q.tools[kFirstBassTool + 1].active);
    const auto& r = q.bass[2];
    CHECK (r.key == 7 && r.scale == 4);
    CHECK (r.rows[3].cable == 5 && r.rows[3].sound == BassSound::Reese);
    CHECK (std::abs (r.rows[3].params[bpRelease] - 0.25f) < 1.0e-6f);
    CHECK (std::abs (r.rows[3].velocity[1][9] - 0.5f) < 1.0e-6f);
    CHECK (r.rows[3].notes[1][9] == 50 && r.rows[3].lengths[1][9] == 3);
    CHECK (std::abs (r.masterFx[fxChorusMix] - 0.4f) < 1.0e-6f);
}

void testChorus()
{
    std::printf ("chorus: ensemble and doubler stay finite and widen a mono sound\n");
    for (int mode = 0; mode < 2; ++mode)
    {
        auto s = bassOnlyStudio();
        setBassNote (s, 0, 0, 36, 8);
        s.bass[0].rows[0].pan = 0.5f;
        auto& fx = s.bass[0].rows[0].fx;
        fx[fxChorusMode] = (float) mode;
        fx[fxChorusMix] = 0.8f;
        fx[fxChorusDepth] = 0.8f;
        const auto t = makePatternTake (s, 1, kSr);
        const auto out = renderStem (t, { kFirstBassTool, 0 }, t.length);
        float side = 0.0f;
        for (size_t i = 0; i < out.l.size(); ++i)
            side = std::max (side, std::abs (out.l[i] - out.r[i]));
        std::printf ("  %s: peak %.3f, left-right difference %.3f\n", mode == 0 ? "ENSEMBLE" : "DOUBLER", (double) out.peak(), (double) side);
        CHECK (out.finite());
        CHECK (out.peak() > 0.02f && out.peak() < 2.0f);
        CHECK (side > 0.01f); // no longer mono
    }
}

void testBassLiveNotes()
{
    std::printf ("bass: a live note holds until it is let go\n");
    StudioEngine engine;
    engine.prepare (kSr, 512);
    engine.setState (bassOnlyStudio());
    Command on;
    on.type = CommandType::Trigger;
    on.tool = kFirstBassTool;
    on.track = 0;
    on.index = 40; // E2: not in C minor, plays Eb2
    on.value = 1.0f;
    engine.apply (on);
    std::vector<float> l ((size_t) kSr), r ((size_t) kSr);
    engine.process (l.data(), r.data(), (int) kSr);
    float held = 0.0f;
    for (size_t i = l.size() / 2; i < l.size(); ++i)
        held = std::max (held, std::abs (l[i]));
    CHECK (held > 0.02f);
    CHECK (std::abs (engine.trackNote (kFirstBassTool, 0) - 39.0f) < 0.01f);

    Command off = on;
    off.type = CommandType::NoteOff;
    engine.apply (off);
    engine.process (l.data(), r.data(), (int) kSr);
    float after = 0.0f;
    for (size_t i = l.size() / 2; i < l.size(); ++i)
        after = std::max (after, std::abs (l[i]));
    std::printf ("  held %.3f, after release %.5f\n", (double) held, (double) after);
    CHECK (after < 0.001f);
}

void testBassExportFiles()
{
    std::printf ("export writes each bass row as a stem\n");
    auto folder = juce::File::getSpecialLocation (juce::File::tempDirectory).getChildFile ("ms-bass-export-test");
    folder.deleteRecursively();
    const auto take = makePatternTake (makeDefaultStudio(), 1, kSr);
    ExportOptions opt;
    opt.folder = folder;
    opt.baseName = "t";
    const auto result = exportTake (take, opt);
    CHECK (result.ok);
    CHECK (folder.getChildFile ("t_BASS-1.wav").existsAsFile());
    CHECK (folder.getChildFile ("t_BASS-1-01-SUB.wav").existsAsFile());
    CHECK (folder.getChildFile ("t_BASS-1-04-ACID.wav").existsAsFile());
    CHECK (! folder.getChildFile ("t_BASS-1-02-PULSE.wav").existsAsFile()); // nothing on P1
    folder.deleteRecursively();
}

void testAutomationCurve()
{
    std::printf ("automation: controls glide between recorded steps, round the bar\n");
    FxAutomation a;
    CHECK (! a.any (0));
    a.set (0, fxCutoff, 0, 0.0f);
    CHECK (std::abs (a.valueAt (0, fxCutoff, 9.5) - 0.0f) < 1.0e-4f); // one point holds
    a.set (0, fxCutoff, 8, 1.0f);
    CHECK (std::abs (a.valueAt (0, fxCutoff, 4.0) - 0.5f) < 1.0e-3f);
    CHECK (std::abs (a.valueAt (0, fxCutoff, 12.0) - 0.5f) < 1.0e-3f); // back down towards step 1
    CHECK (std::abs (a.valueAt (0, fxCutoff, 8.0) - 1.0f) < 1.0e-3f);
    CHECK (a.has (0, fxCutoff) && ! a.has (1, fxCutoff) && ! a.has (0, fxDelayMix));

    FxParams base = defaultFxParams();
    const auto applied = a.apply (base, 0, 4.0);
    CHECK (std::abs (applied[fxCutoff] - 0.5f) < 1.0e-3f);
    CHECK (juce::exactlyEqual (applied[fxDelayMix], base[fxDelayMix]));

    Command clear;
    clear.type = CommandType::ClearFxAuto;
    clear.index = 0;
    clear.value = -1.0f;
    applyFxAuto (a, clear);
    CHECK (! a.anyAtAll());
}

// Energy of the sample-to-sample differences (the highs) in [from, to).
double highs (const std::vector<float>& x, size_t from, size_t to)
{
    double e = 0.0;
    for (size_t i = from + 1; i < to; ++i)
        e += (double) (x[i] - x[i - 1]) * (double) (x[i] - x[i - 1]);
    return e;
}

void testAutomationPlays()
{
    std::printf ("automation: a recorded filter sweep on the drum bus plays back, the same every time\n");
    auto studio = studioWithDrums (makeDefaultProject());
    // open for the first half of the bar, nearly shut for the second
    auto record = [&studio] (int step, float value) {
        Command c;
        c.type = CommandType::SetFxAuto;
        c.tool = kDrumTool;
        c.track = -1;
        c.index = fxAutoIndex (0, fxCutoff, step);
        c.value = value;
        applyToStudio (studio, c);
    };
    const auto plain = makePatternTake (studio, 1, kSr);
    record (0, 1.0f);
    record (7, 1.0f);
    record (8, 0.08f);
    record (15, 0.08f);
    CHECK (studio.drums.masterFxAuto.has (0, fxCutoff));
    const auto swept = makePatternTake (studio, 1, kSr);

    const auto a = renderStem (plain, {}, plain.length);
    const auto b = renderStem (swept, {}, swept.length);
    const auto b2 = renderStem (swept, {}, swept.length);
    CHECK (a.l == a.l && b.l == b2.l); // deterministic
    const double stepLen = kSr * 60.0 / (double) studio.bpm / 4.0;
    const auto half = (size_t) (stepLen * 8.0), end = (size_t) (stepLen * 15.5);
    const double plainRatio = highs (a.l, half + (size_t) stepLen, end) / highs (a.l, 0, half);
    const double sweptRatio = highs (b.l, half + (size_t) stepLen, end) / highs (b.l, 0, half);
    std::printf ("  second half / first half highs: %.3f plain, %.4f swept\n", plainRatio, sweptRatio);
    CHECK (sweptRatio < plainRatio * 0.2);
    CHECK (b.finite());

    // every rack automated at once, several bars: nothing blows up
    auto busy = makeDefaultStudio();
    auto point = [&busy] (int tool, int rack, int param, int st, float v) {
        Command c;
        c.type = CommandType::SetFxAuto;
        c.tool = tool;
        c.track = rack;
        c.index = fxAutoIndex (0, param, st);
        c.value = v;
        applyToStudio (busy, c);
    };
    for (int st = 0; st < kNumSteps; ++st)
    {
        point (kDrumTool, 0, fxCutoff, st, 0.35f + 0.6f * std::abs (std::sin ((float) st * 0.4f)));
        point (kFirstBassTool, 0, fxCutoff, st, 0.35f + 0.6f * std::abs (std::sin ((float) st * 0.4f)));
        if (st % 4 == 0)
        {
            point (kDrumTool, 0, fxDelayMix, st, st == 8 ? 0.6f : 0.1f);
            point (kFirstBassTool, 0, fxDelayMix, st, st == 8 ? 0.6f : 0.1f);
        }
    }
    point (kStudio, 0, fxReverbMix, 0, 0.1f);
    point (kStudio, 0, fxReverbMix, 12, 0.5f);
    const auto busyTake = makePatternTake (busy, 4, kSr);
    const auto out = renderStem (busyTake, {}, busyTake.length);
    std::printf ("  everything automated: peak %.3f\n", (double) out.peak());
    CHECK (out.finite());
}

void testAutomationSaved()
{
    std::printf ("automation: saved and opened on every kind of rack\n");
    auto s = makeDefaultStudio();
    s.masterFxAuto.set (0, fxReverbMix, 3, 0.25f);
    s.drums.tracks[2].fxAuto.set (1, fxDelayMix, 5, 0.5f);
    s.drums.masterFxAuto.set (3, fxCutoff, 15, 0.75f);
    s.bass[0].rows[4].fxAuto.set (2, fxChorusMix, 0, 1.0f);
    s.bass[0].masterFxAuto.set (0, fxGrainMix, 9, 0.125f);

    StudioState q;
    std::array<juce::String, kNumTracks> paths;
    CHECK (studioFromVar (juce::JSON::parse (juce::JSON::toString (studioToVar (s))), q, paths));
    auto near = [] (float a, float b) { return std::abs (a - b) < 3.0e-3f; };
    CHECK (near (q.masterFxAuto.point (0, fxReverbMix, 3), 0.25f));
    CHECK (near (q.drums.tracks[2].fxAuto.point (1, fxDelayMix, 5), 0.5f));
    CHECK (near (q.drums.masterFxAuto.point (3, fxCutoff, 15), 0.75f));
    CHECK (near (q.bass[0].rows[4].fxAuto.point (2, fxChorusMix, 0), 1.0f));
    CHECK (near (q.bass[0].masterFxAuto.point (0, fxGrainMix, 9), 0.125f));
    CHECK (q.drums.tracks[2].fxAuto.point (1, fxDelayMix, 4) < 0.0f);
}
// ---- looper -------------------------------------------------------------------

// A studio with just looper 1, at 120 BPM: a step is 6000 samples, a bar 96000.
StudioState looperOnlyStudio()
{
    auto s = makeDefaultStudio();
    s.bpm = 120.0f;
    for (int t = 0; t < kNumToolSlots; ++t)
        s.tools[(size_t) t].active = t == kFirstLooperTool;
    for (auto& l : s.loopers)
        l.bpm = s.bpm;
    return s;
}

constexpr std::int64_t kBar = 96000;

Command loopCommand (CommandType type, int index, float value = 0.0f, LoopAudio* loop = nullptr, int track = 0)
{
    Command c;
    c.type = type;
    c.tool = kFirstLooperTool;
    c.index = index;
    c.value = value;
    c.loop = loop;
    c.track = track;
    return c;
}

std::unique_ptr<LoopAudio> emptyLoop (int bars)
{
    auto a = std::make_unique<LoopAudio>();
    a->bars = bars;
    a->bpm = 120.0f;
    a->sampleRate = kSr;
    a->left.assign ((size_t) (kBar * bars + kBar / 4), 0.0f);
    a->right = a->left;
    return a;
}

// Plays the studio live from the start, the input as given, applying
// commands at their sample (on block boundaries, like the processor).
Rendered playLive (StudioEngine& e, const std::vector<float>& input, std::int64_t length,
                   std::vector<TimedCommand> commands = {}, bool startPlaying = true)
{
    Rendered out;
    out.l.resize ((size_t) length);
    out.r.resize ((size_t) length);
    std::vector<float> in (256);
    size_t next = 0;
    if (startPlaying)
        e.setPlaying (true);
    for (std::int64_t pos = 0; pos < length;)
    {
        while (next < commands.size() && commands[next].time <= pos)
            e.apply (commands[next++].command);
        int n = (int) std::min<std::int64_t> (256, length - pos);
        if (next < commands.size())
            n = (int) std::min<std::int64_t> (n, commands[next].time - pos);
        for (int i = 0; i < n; ++i)
            in[(size_t) i] = pos + i < (std::int64_t) input.size() ? input[(size_t) (pos + i)] : 0.0f;
        e.setInput (in.data(), in.data());
        e.process (out.l.data() + pos, out.r.data() + pos, n);
        pos += n;
    }
    return out;
}

std::unique_ptr<StudioEngine> liveLooper (const StudioState& s)
{
    auto e = std::make_unique<StudioEngine>();
    e->prepare (kSr, 256);
    e->setState (s);
    e->reset();
    e->setRecordsInput (true);
    return e;
}

void testLooperRecordsAndPlays()
{
    std::printf ("looper: records a bar from the input and plays it on the clock\n");
    const auto s = looperOnlyStudio();
    auto e = liveLooper (s);
    auto audio = emptyLoop (1);
    std::vector<float> input ((size_t) kBar, 0.0f);
    input[24100] = 0.5f; // just after beat 2 of the first bar
    const auto out = playLive (*e, input, kBar * 3, { { 0, loopCommand (CommandType::RecordLoop, 0, 0.0f, audio.get()) } });

    CHECK (audio->length == kBar);
    CHECK (std::abs (audio->left[24100] - 0.5f) < 1.0e-4f);
    CHECK (e->looper (0).state().slots[0].audio == audio.get());
    CHECK (e->looper (0).finishedCount() == 1);
    // nothing while it records (no monitoring), then the loop every bar
    float firstBar = 0.0f;
    for (std::int64_t i = 0; i < kBar; ++i)
        firstBar = std::max (firstBar, std::abs (out.l[(size_t) i]));
    CHECK (firstBar <= 0.0f);
    std::printf ("  bar 2 %.4f bar 3 %.4f\n", (double) out.l[(size_t) (kBar + 24100)], (double) out.l[(size_t) (2 * kBar + 24100)]);
    CHECK (std::abs (out.l[(size_t) (kBar + 24100)] - 0.5f) < 1.0e-4f);
    CHECK (std::abs (out.r[(size_t) (2 * kBar + 24100)] - 0.5f) < 1.0e-4f);
    CHECK (out.onset (kBar) == kBar + 24100);

    // slices: step 5 muted, step 9 plays slice 5 again
    e->apply (loopCommand (CommandType::SetSlice, 4, (float) kMuteSlice));
    e->apply (loopCommand (CommandType::SetSlice, 8, 4.0f));
    const auto sliced = playLive (*e, {}, kBar, {}, false);
    const auto at = sliced.onset (0, 0.01f);
    std::printf ("  sliced: first hit at %lld (bar position %lld)\n", (long long) at, (long long) ((at + 0) % kBar));
    CHECK (at >= 0 && (at + 3 * kBar) % kBar == 48100);
}

void testLooperLatencyAndOverdub()
{
    std::printf ("looper: the input latency is taken off; an overdub adds a layer\n");
    const auto s = looperOnlyStudio();
    auto e = liveLooper (s);
    auto audio = emptyLoop (1);
    std::vector<float> input ((size_t) (kBar * 3), 0.0f);
    input[24100 + 480] = 0.5f; // arrives 10 ms late
    // the overdub goes on in bar 3, on beat 4
    input[(size_t) (2 * kBar + 72100 + 480)] = 0.25f;
    auto layer = std::make_unique<LoopAudio>();
    std::vector<TimedCommand> commands = { { 0, loopCommand (CommandType::RecordLoop, 0, 0.0f, audio.get(), 480) } };
    const auto first = playLive (*e, input, kBar + 1000, commands);
    CHECK (audio->length == kBar);
    CHECK (std::abs (audio->left[24100] - 0.5f) < 1.0e-4f);

    *layer = *audio; // what the UI does: a copy to record on top of
    e->apply (loopCommand (CommandType::RecordLoop, 0, 1.0f, layer.get(), 480));
    std::vector<float> rest (input.begin() + kBar + 1000, input.end());
    playLive (*e, rest, kBar * 2 + 2000, {}, false);
    CHECK (e->looper (0).state().slots[0].audio == layer.get());
    CHECK (std::abs (layer->left[24100] - 0.5f) < 1.0e-4f);
    std::printf ("  overdub layer at beat 4: %.5f\n", (double) layer->left[72100]);
    CHECK (std::abs (layer->left[72100] - 0.25f) < 1.0e-4f);
    CHECK (juce::exactlyEqual (audio->left[72100], 0.0f)); // the first layer stays as it was, for undo
}

void testLooperTakeReplays()
{
    std::printf ("looper: a recorded take, input and all, exports the same as it played\n");
    auto s = looperOnlyStudio();
    s.loopers[0].monitor = true; // the export hears the input too
    s.loopers[0].masterFx[fxDelayMix] = 0.3f;
    auto e = liveLooper (s);
    auto audio = emptyLoop (2);
    std::vector<float> input ((size_t) (kBar * 4), 0.0f);
    for (int i = 0; i < 6; ++i)
        input[(size_t) (10000 + i * 31000)] = 0.4f;

    Take take;
    take.start = e->state();
    take.sampleRate = kSr;
    take.length = kBar * 4;
    take.events = { { 0, loopCommand (CommandType::RecordLoop, 0, 0.0f, audio.get()) } };
    InputRecording rec;
    rec.append (input.data(), input.data(), (int) input.size(), [] { return InputRecording::makeChunk(); });
    take.input = &rec;
    CHECK (rec.length == kBar * 4);

    const auto live = playLive (*e, input, take.length, take.events);
    const auto again = render (take, -1, take.length);
    float diff = 0.0f;
    for (size_t i = 0; i < live.l.size(); ++i)
        diff = std::max ({ diff, std::abs (live.l[i] - again.l[i]), std::abs (live.r[i] - again.r[i]) });
    std::printf ("  live peak %.3f, largest difference %g\n", (double) live.peak(), (double) diff);
    CHECK (live.peak() > 0.3f);
    CHECK (diff < 1.0e-6f);

    // and the export writes the looper as its own stem
    auto folder = juce::File::getSpecialLocation (juce::File::tempDirectory).getChildFile ("ms-looper-export-test");
    folder.deleteRecursively();
    ExportOptions opt;
    opt.folder = folder;
    opt.baseName = "t";
    CHECK (exportTake (take, opt).ok);
    CHECK (folder.getChildFile ("t_LOOP-1.wav").existsAsFile());
    folder.deleteRecursively();
}

void testLooperSaveAndOpen()
{
    std::printf ("looper settings save and open\n");
    auto s = looperOnlyStudio();
    LoopAudio audio;
    audio.length = 100;
    audio.bars = 4;
    audio.bpm = 97.0f;
    auto& l = s.loopers[0];
    l.input = LoopInput::In2;
    l.monitor = true;
    l.inputGain = 0.4f;
    l.pan = 0.2f;
    l.pattern = 2;
    l.slots[1].bars = 8;
    l.slots[1].slices[3] = kMuteSlice;
    l.slots[1].slices[5] = 0;
    l.slots[2].audio = &audio;
    l.masterFx[fxChorusMix] = 0.5f;
    std::array<LoopFiles, kMaxLoopers> files;
    files[0][2].file = "x loops/LOOP-1-P3.wav";

    StudioState q;
    std::array<juce::String, kNumTracks> paths;
    std::array<LoopFiles, kMaxLoopers> opened;
    CHECK (studioFromVar (juce::JSON::parse (juce::JSON::toString (studioToVar (s, files))), q, paths, &opened));
    const auto& m = q.loopers[0];
    CHECK (q.tools[kFirstLooperTool].active);
    CHECK (m.input == LoopInput::In2 && m.monitor && m.pattern == 2);
    CHECK (std::abs (m.inputGain - 0.4f) < 1.0e-6f && std::abs (m.pan - 0.2f) < 1.0e-6f);
    CHECK (m.slots[1].bars == 8 && m.slots[1].slices[3] == kMuteSlice && m.slots[1].slices[5] == 0 && m.slots[1].slices[6] == 6);
    CHECK (m.slots[2].audio == nullptr); // the caller loads it
    CHECK (opened[0][2].file == "x loops/LOOP-1-P3.wav" && opened[0][2].bars == 4 && std::abs (opened[0][2].bpm - 97.0f) < 1.0e-3f);
    CHECK (opened[0][1].file.isEmpty());
    CHECK (std::abs (m.masterFx[fxChorusMix] - 0.5f) < 1.0e-6f);
}

} // namespace

// EngineTests --demo <folder>: renders the starter patterns (master + stems)
// with each drum character, plus one through the new effects, so the kit can
// be heard without opening the app.
int renderDemo (const char* folder)
{
    const auto root = juce::File::getCurrentWorkingDirectory().getChildFile (folder);
    bool ok = true;

    auto renderOne = [&] (ProjectState p, int pattern, const juce::String& name) {
        ExportOptions opt;
        opt.folder = root.getChildFile (name);
        opt.baseName = name;
        for (int t = 0; t < kNumTracks; ++t)
            opt.trackNames[(size_t) t] = drumName (p.tracks[(size_t) t].voice, p.tracks[(size_t) t].character);
        const auto result = exportTake (makePatternTake (p, 4, kSr, pattern), opt);
        for (const auto& f : result.files)
            std::printf ("%s\n", f.getFullPathName().toRawUTF8());
        ok = ok && result.ok;
    };

    const char* names[] = { "live", "digital", "hr16" };
    for (int c = 0; c < kNumCharacters; ++c)
    {
        auto p = makeDefaultProject();
        if ((Character) c != Character::Digital) // the default kit is already digital
            for (int t = 0; t < kNumTracks - 1; ++t)
                applyToState (p, { CommandType::SetCharacter, t, c, 0.0f });
        p.masterFx[fxReverbMix] = 0.12f;
        p.masterFx[fxReverbTime] = 0.4f;
        renderOne (p, 0, juce::String ("p1-") + names[c]);
    }

    // P2 through distortion, a 1/16 grain delay, a 1/8D delay and Psychosis
    auto p = makeDefaultProject();
    p.masterFx[fxDistDrive] = 0.35f;
    p.masterFx[fxDistMix] = 0.5f;
    p.masterFx[fxDelayMix] = 0.25f;
    p.masterFx[fxGrainMix] = 0.3f;
    p.masterFx[fxGrainPitch] = 1.0f; // +12 st
    p.masterFx[fxPsyMix] = 0.7f;
    renderOne (p, 1, "p2-effects");

    return ok ? 0 : 1;
}

int main (int argc, char** argv)
{
    loadHr16Bank();
    if (argc == 3 && std::string (argv[1]) == "--demo")
        return renderDemo (argv[2]);

    testDefaultKitMakesSound();
    testEachVoiceType();
    testDeterministic();
    testStemsSumToMaster();
    testStemsCarryMasterEffects();
    testSwingTiming();
    testRecordedEventsReplay();
    testParameterChangeIsRecorded();
    testExportFiles();
    testSaveAndOpen();
    testEveryCharacterRenders();
    testPatternQueue();
    testNoteSyncedDelay();
    testNewEffectsAreSane();
    testPitchNames();
    testStudioToolMixing();
    testAddedToolJoinsTheBeat();
    testStudioSaveAndOpen();
    testBassSoundsRender();
    testBassPitch();
    testBassCutGroups();
    testScaleLock();
    testBassSaveAndOpen();
    testChorus();
    testBassLiveNotes();
    testBassExportFiles();
    testAutomationCurve();
    testAutomationPlays();
    testAutomationSaved();
    testLooperRecordsAndPlays();
    testLooperLatencyAndOverdub();
    testLooperTakeReplays();
    testLooperSaveAndOpen();

    if (failures == 0)
        std::printf ("\nall engine tests passed\n");
    else
        std::printf ("\n%d check(s) failed\n", failures);
    return failures == 0 ? 0 : 1;
}
