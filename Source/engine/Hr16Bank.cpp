#include "Hr16Bank.h"

#include "ProjectState.h"

#if ! MS_NO_JUCE
 #include <BinaryData.h>
 #include <juce_audio_formats/juce_audio_formats.h>
#endif

#include <array>
#include <mutex>

namespace ms
{
namespace
{
// Sorted by ear-free analysis (length, spectrum, envelope) of the 56 files.
constexpr int kKicks[] = { 1, 2, 3, 4, 5, 6, 7, 8 };
constexpr int kHats[] = { 10, 11, 12, 13, 14, 16, 9, 15, 32, 52, 27, 28, 21 }; // closed, open, cymbals
constexpr int kSnares[] = { 39, 40, 41, 42, 44, 45, 46, 47, 48, 49, 50, 34, 23, 31, 22, 24, 26 };
constexpr int kToms[] = { 53, 54, 55, 56, 20, 30 };
constexpr int kPercussion[] = { 17, 18, 19, 25, 43, 33, 51, 29, 35, 36, 37, 38 }; // clap, bells, rims, reverse

struct Group
{
    const int* files;
    int count;
};

template <size_t N>
constexpr Group group (const int (&a)[N]) { return { a, (int) N }; }

Group groupFor (VoiceType t)
{
    switch (t)
    {
        case VoiceType::Kick:       return group (kKicks);
        case VoiceType::ClickHat:   return group (kHats);
        case VoiceType::NoiseSnare: return group (kSnares);
        case VoiceType::SineTone:   return group (kToms);
        case VoiceType::Glitch:     return group (kPercussion);
        case VoiceType::Sample:
        case VoiceType::Count:      break;
    }
    return { nullptr, 0 };
}

constexpr int kNumFiles = 56;
std::array<SampleData, kNumFiles + 1> bank; // indexed by file number
#if ! MS_NO_JUCE
std::once_flag loaded;

void decodeAll()
{
    juce::WavAudioFormat wav;
    for (int n = 1; n <= kNumFiles; ++n)
    {
        const auto name = "Alesis_HR16A_" + juce::String (n).paddedLeft ('0', 2) + "_wav";
        int size = 0;
        const char* data = BinaryData::getNamedResource (name.toRawUTF8(), size);
        if (data == nullptr)
            continue;

        auto stream = std::make_unique<juce::MemoryInputStream> (data, (size_t) size, false);
        std::unique_ptr<juce::AudioFormatReader> reader (wav.createReaderFor (stream.release(), true));
        if (reader == nullptr)
            continue;

        const int length = (int) reader->lengthInSamples;
        juce::AudioBuffer<float> buffer ((int) reader->numChannels, length);
        reader->read (&buffer, 0, length, 0, true, true);

        auto& s = bank[(size_t) n];
        s.sampleRate = reader->sampleRate;
        s.path = "HR16 #" + juce::String (n).paddedLeft ('0', 2);
        s.samples.assign ((size_t) length, 0.0f);
        const float scale = 1.0f / (float) juce::jmax (1, buffer.getNumChannels());
        for (int ch = 0; ch < buffer.getNumChannels(); ++ch)
            for (int i = 0; i < length; ++i)
                s.samples[(size_t) i] += buffer.getSample (ch, i) * scale;
    }
}
#endif
} // namespace

int hr16SoundCount (VoiceType t)
{
    return groupFor (t).count;
}

int hr16FileNumber (VoiceType t, int index)
{
    const auto g = groupFor (t);
    return index >= 0 && index < g.count ? g.files[index] : 0;
}

#if MS_NO_JUCE
void loadHr16Bank() {}

void setHr16File (int n, const float* samples, int length, double sampleRate)
{
    if (n < 1 || n > kNumFiles || length < 0)
        return;
    auto& s = bank[(size_t) n];
    s.sampleRate = sampleRate;
    s.path = "HR16 #" + std::string (n < 10 ? "0" : "") + std::to_string (n);
    s.samples.assign (samples, samples + length);
}
#else
void loadHr16Bank()
{
    std::call_once (loaded, decodeAll);
}
#endif

const SampleData* hr16Sample (VoiceType t, int index)
{
    const int n = hr16FileNumber (t, index);
    if (n <= 0 || bank[(size_t) n].samples.empty())
        return nullptr;
    return &bank[(size_t) n];
}
} // namespace ms
