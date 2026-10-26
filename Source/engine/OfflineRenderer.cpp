#include "OfflineRenderer.h"

#include <algorithm>

namespace ms
{
namespace
{
constexpr int kBlock = 512;
constexpr float kTailSilence = 0.00003f; // about -90 dB
constexpr double kSilentTailSeconds = 0.3;

// Renders a take block by block, applying recorded events at their exact sample.
class TakePlayer
{
public:
    TakePlayer (const Take& t, int soloTrack) : take (t)
    {
        engine.prepare (take.sampleRate, kBlock);
        engine.setState (take.start);
        engine.reset();
        engine.setSoloRender (soloTrack);
        engine.setPlaying (take.length > 0);
    }

    // Renders up to maxSamples (stops early at the next event or the end of
    // the take) and returns the number rendered.
    int renderNext (float* left, float* right, int maxSamples)
    {
        while (nextEvent < take.events.size() && take.events[nextEvent].time <= position)
            engine.apply (take.events[nextEvent++].command);

        if (position >= take.length)
            engine.setPlaying (false);

        std::int64_t limit = maxSamples;
        if (nextEvent < take.events.size())
            limit = std::min (limit, take.events[nextEvent].time - position);
        if (position < take.length)
            limit = std::min (limit, take.length - position);

        const int n = (int) std::max<std::int64_t> (1, limit);
        engine.process (left, right, n);
        position += n;
        return n;
    }

private:
    const Take& take;
    EngineCore engine;
    size_t nextEvent = 0;
    std::int64_t position = 0;
};

std::unique_ptr<juce::AudioFormatWriter> makeWriter (const juce::File& file, double sr, int bits)
{
    file.deleteFile();
    std::unique_ptr<juce::OutputStream> stream = std::make_unique<juce::FileOutputStream> (file);
    if (static_cast<juce::FileOutputStream*> (stream.get())->failedToOpen())
        return {};

    juce::WavAudioFormat wav;
    const auto options = juce::AudioFormatWriterOptions {}
                             .withSampleRate (sr)
                             .withNumChannels (2)
                             .withBitsPerSample (bits);
    return wav.createWriterFor (stream, options);
}

juce::String safeName (const juce::String& s)
{
    auto r = s.retainCharacters ("ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789-_ ")
                 .trim()
                 .replaceCharacter (' ', '-');
    return r.isEmpty() ? juce::String ("track") : r;
}
} // namespace

Take makePatternTake (const ProjectState& state, int loops, double sampleRate, int pattern)
{
    Take t;
    t.start = state;
    t.start.pattern = pattern >= 0 ? pattern : viewedPattern (state);
    t.start.queuedPattern = -1;
    t.sampleRate = sampleRate;
    const double stepLen = sampleRate * 60.0 / (double) state.bpm / 4.0;
    t.length = (std::int64_t) std::llround (stepLen * kNumSteps * std::max (1, loops));
    return t;
}

void renderTake (const Take& take, int soloTrack, std::int64_t totalLength,
                 const std::function<void (const float*, const float*, int)>& sink)
{
    TakePlayer player (take, soloTrack);
    std::vector<float> l (kBlock), r (kBlock);

    for (std::int64_t done = 0; done < totalLength;)
    {
        const int want = (int) std::min<std::int64_t> (kBlock, totalLength - done);
        const int n = player.renderNext (l.data(), r.data(), want);
        sink (l.data(), r.data(), n);
        done += n;
    }
}

ExportResult exportTake (const Take& take, const ExportOptions& opt,
                         const std::function<bool (float)>& progress)
{
    ExportResult result;
    const double sr = take.sampleRate;

    if (! opt.folder.isDirectory() && ! opt.folder.createDirectory())
    {
        result.error = "Could not create folder " + opt.folder.getFullPathName();
        return result;
    }

    const int passes = 1 + kNumTracks;
    auto report = [&] (int pass, double fraction) {
        return ! progress || progress ((float) ((pass + juce::jlimit (0.0, 1.0, fraction)) / passes));
    };

    // ---- pass 1: master, which also decides where the tail ends -----------
    const auto masterFile = opt.folder.getChildFile (opt.baseName + "_Master.wav");
    auto writer = makeWriter (masterFile, sr, opt.bitDepth);
    if (writer == nullptr)
    {
        result.error = "Could not write " + masterFile.getFullPathName();
        return result;
    }

    const auto maxLength = take.length + (std::int64_t) (opt.maxTailSeconds * sr);
    const auto silentNeeded = (std::int64_t) (kSilentTailSeconds * sr);
    std::int64_t written = 0, silentRun = 0;

    {
        TakePlayer player (take, -1);
        std::vector<float> l (kBlock), r (kBlock);

        while (written < maxLength)
        {
            const int n = player.renderNext (l.data(), r.data(), kBlock);
            const float* chans[] = { l.data(), r.data() };
            writer->writeFromFloatArrays (chans, 2, n);
            written += n;

            if (written > take.length)
            {
                float peak = 0.0f;
                for (int i = 0; i < n; ++i)
                    peak = std::max ({ peak, std::abs (l[(size_t) i]), std::abs (r[(size_t) i]) });
                silentRun = peak < kTailSilence ? silentRun + n : 0;
                if (silentRun >= silentNeeded)
                    break;
            }

            if (! report (0, (double) written / (double) std::max<std::int64_t> (1, take.length)))
            {
                result.error = "Cancelled";
                return result;
            }
        }
    }
    writer.reset();
    result.files.add (masterFile);
    result.lengthSamples = written;

    // ---- passes 2..9: one stem per track, same length as the master -------
    for (int t = 0; t < kNumTracks; ++t)
    {
        const auto& tr = take.start.tracks[(size_t) t];
        const auto name = opt.trackNames[(size_t) t].isNotEmpty() ? opt.trackNames[(size_t) t]
                                                                  : juce::String (drumName (tr.voice, tr.character));
        const auto stemFile = opt.folder.getChildFile (opt.baseName + "_" + juce::String (t + 1).paddedLeft ('0', 2)
                                                       + "-" + safeName (name) + ".wav");
        auto stemWriter = makeWriter (stemFile, sr, opt.bitDepth);
        if (stemWriter == nullptr)
        {
            result.error = "Could not write " + stemFile.getFullPathName();
            return result;
        }

        float stemPeak = 0.0f;
        std::int64_t done = 0;
        bool cancelled = false;
        renderTake (take, t, written, [&] (const float* l, const float* r, int n) {
            const float* chans[] = { l, r };
            stemWriter->writeFromFloatArrays (chans, 2, n);
            for (int i = 0; i < n; ++i)
                stemPeak = std::max ({ stemPeak, std::abs (l[i]), std::abs (r[i]) });
            done += n;
            if (! cancelled && ! report (1 + t, (double) done / (double) written))
                cancelled = true;
        });
        stemWriter.reset();

        if (cancelled)
        {
            result.error = "Cancelled";
            return result;
        }

        // A track that never made a sound (muted, empty or silent) gets no file.
        if (stemPeak <= 0.0f)
            stemFile.deleteFile();
        else
            result.files.add (stemFile);
    }

    result.ok = true;
    return result;
}
} // namespace ms
