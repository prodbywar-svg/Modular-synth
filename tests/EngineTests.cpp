// Engine tests: run headless (no audio device, no window) on every build.

#include "engine/OfflineRenderer.h"

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
    renderTake (take, solo, length, [&] (const float* l, const float* r, int n) {
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
        p.tracks[0].voiceParams = defaultVoiceParams ((VoiceType) v);
        p.tracks[0].sample = &sample;
        p.tracks[0].steps[0] = 1.0f;
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
    p.tracks[0].steps[0] = 1.0f;
    auto take = makePatternTake (p, 1, kSr);
    const auto len = take.length + (std::int64_t) kSr * 2;
    const auto dry = render (take, 0, len);

    take.start.masterFx[fxReverbMix] = 0.6f;
    take.start.masterFx[fxReverbSize] = 0.9f;
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
    p.tracks[0].voiceParams = defaultVoiceParams (VoiceType::ClickHat);
    p.tracks[0].steps[1] = 1.0f;
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
    p.tracks[0].steps = {}; // silent pattern
    p.tracks[1].voice = VoiceType::NoiseSnare;
    p.tracks[1].voiceParams = defaultVoiceParams (VoiceType::NoiseSnare);
    p.tracks[2].voice = VoiceType::Kick;
    p.tracks[2].voiceParams = defaultVoiceParams (VoiceType::Kick);
    for (int s = 0; s < kNumSteps; s += 4)
        p.tracks[2].steps[(size_t) s] = 1.0f;

    Take take;
    take.start = p;
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
    p.tracks[0].steps[0] = 1.0f;
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
    // master + tracks 1,2,3,4,6 (5 is muted, 7 has no steps, 8 has no sample)
    CHECK (result.files.size() == 6);
    CHECK (folder.getChildFile ("test_Master.wav").existsAsFile());
    CHECK (folder.getChildFile ("test_01-Kick.wav").existsAsFile());
    CHECK (! folder.getChildFile ("test_05-GLITCH.wav").existsAsFile());
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

    const auto json = juce::JSON::toString (projectToVar (p));
    ProjectState q;
    std::array<juce::String, kNumTracks> paths;
    CHECK (projectFromVar (juce::JSON::parse (json), q, paths));
    CHECK (q.bpm == p.bpm);
    CHECK (q.swing == p.swing);
    CHECK (q.tracks[3].mute);
    CHECK (q.tracks[5].fx == p.tracks[5].fx);
    CHECK (q.masterFx == p.masterFx);
    for (int t = 0; t < kNumTracks; ++t)
    {
        CHECK (q.tracks[(size_t) t].voice == p.tracks[(size_t) t].voice);
        CHECK (q.tracks[(size_t) t].steps == p.tracks[(size_t) t].steps);
        CHECK (q.tracks[(size_t) t].voiceParams == p.tracks[(size_t) t].voiceParams);
    }

    CHECK (! projectFromVar (juce::JSON::parse ("{\"format\":\"other\"}"), q, paths));
}
} // namespace

// EngineTests --demo <folder>: renders the starter pattern (master + stems)
// so the kit can be heard without opening the app.
int renderDemo (const char* folder)
{
    ExportOptions opt;
    opt.folder = juce::File::getCurrentWorkingDirectory().getChildFile (folder);
    opt.baseName = "starter-kit";
    auto p = makeDefaultProject();
    p.masterFx[fxReverbMix] = 0.12f;
    p.masterFx[fxReverbSize] = 0.7f;
    const auto result = exportTake (makePatternTake (p, 8, kSr), opt);
    for (const auto& f : result.files)
        std::printf ("%s\n", f.getFullPathName().toRawUTF8());
    return result.ok ? 0 : 1;
}

int main (int argc, char** argv)
{
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

    if (failures == 0)
        std::printf ("\nall engine tests passed\n");
    else
        std::printf ("\n%d check(s) failed\n", failures);
    return failures == 0 ? 0 : 1;
}
