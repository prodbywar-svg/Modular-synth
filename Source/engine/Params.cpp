#include "Params.h"

#include <cmath>
#include <cstdio>
#include <cstring>

namespace ms
{
namespace
{
enum class Curve
{
    Lin,
    Exp
};

struct ParamSpec
{
    const char* name; // nullptr = slot unused
    Curve curve;
    float lo, hi;
    const char* unit;
    float defaultValue;
};

constexpr ParamSpec kUnused { nullptr, Curve::Lin, 0.0f, 1.0f, "", 0.0f };

// clang-format off
const ParamSpec kVoiceSpecs[kNumVoiceTypes][kNumVoiceParams] = {
    // Kick: sine with a fast pitch drop, a click, then hard clipping.
    { { "PITCH", Curve::Exp, 30.0f, 120.0f,   "Hz", 0.35f },
      { "DECAY", Curve::Exp, 40.0f, 1500.0f,  "ms", 0.45f },
      { "DRIVE", Curve::Lin, 1.0f,  40.0f,    "x",  0.60f },
      { "CLICK", Curve::Lin, 0.0f,  1.0f,     "",   0.60f } },
    // Click hat: a single-sample impulse plus a tiny noise burst, high-passed.
    { { "DECAY", Curve::Exp, 0.3f,  80.0f,    "ms", 0.25f },
      { "TONE",  Curve::Exp, 1000.0f, 16000.0f, "Hz", 0.60f },
      { "NOISE", Curve::Lin, 0.0f,  1.0f,     "",   0.30f },
      kUnused },
    // Noise snare: pure white noise, sharp envelope, no tonal body.
    { { "DECAY", Curve::Exp, 5.0f,  600.0f,   "ms", 0.45f },
      { "HIPASS", Curve::Exp, 20.0f, 8000.0f, "Hz", 0.40f },
      { "SNAP",  Curve::Lin, 0.0f,  1.0f,     "",   0.70f },
      { "COLOR", Curve::Exp, 1000.0f, 20000.0f, "Hz", 1.00f } },
    // Sine tone: pure sine blips.
    { { "PITCH", Curve::Exp, 50.0f, 16000.0f, "Hz", 0.75f },
      { "LENGTH", Curve::Exp, 1.0f, 500.0f,   "ms", 0.30f },
      { "SWEEP", Curve::Lin, -1.0f, 1.0f,     "oct", 0.50f },
      kUnused },
    // Glitch: bit-crushed sample-and-hold noise bursts.
    { { "RATE",  Curve::Exp, 50.0f, 20000.0f, "Hz", 0.60f },
      { "BITS",  Curve::Lin, 1.0f,  8.0f,     "bit", 0.30f },
      { "LENGTH", Curve::Exp, 5.0f, 400.0f,   "ms", 0.40f },
      kUnused },
    // Sample: plays a loaded WAV/AIFF.
    { { "PITCH", Curve::Exp, 0.25f, 4.0f,     "x",  0.50f },
      { "DECAY", Curve::Exp, 10.0f, 8000.0f,  "ms", 1.00f },
      { "START", Curve::Lin, 0.0f,  0.9f,     "",   0.00f },
      kUnused },
};

const ParamSpec kFxSpecs[kNumFxParams] = {
    { "CUTOFF",  Curve::Exp, 20.0f, 20000.0f, "Hz", 1.00f },
    { "RESO",    Curve::Lin, 0.5f,  10.0f,    "Q",  0.00f },
    { "BITS",    Curve::Lin, 16.0f, 1.0f,     "bit", 0.00f },
    { "DOWNSMP", Curve::Lin, 1.0f,  64.0f,    "x",  0.00f },
    { "DLY TIME", Curve::Lin, 10.0f, 1000.0f, "ms", 0.375f },
    { "DLY FDBK", Curve::Lin, 0.0f, 0.95f,    "",   0.35f },
    { "DLY MIX", Curve::Lin, 0.0f,  1.0f,     "",   0.00f },
    { "REV SIZE", Curve::Lin, 0.0f, 1.0f,     "",   0.50f },
    { "REV MIX", Curve::Lin, 0.0f,  1.0f,     "",   0.00f },
};
// clang-format on

const char* const kVoiceNames[kNumVoiceTypes] = { "KICK", "CLICK", "NOISE", "SINE", "GLITCH", "SAMPLE" };

float mapSpec (const ParamSpec& s, float v)
{
    return s.curve == Curve::Exp ? expMap (v, s.lo, s.hi) : linMap (v, s.lo, s.hi);
}

void formatSpec (const ParamSpec& s, float v, char* out, int outSize)
{
    if (s.name == nullptr)
    {
        std::snprintf (out, (size_t) outSize, "-");
        return;
    }

    const float x = mapSpec (s, v);

    if (std::strcmp (s.unit, "Hz") == 0 && x >= 1000.0f)
        std::snprintf (out, (size_t) outSize, "%.2f kHz", x / 1000.0f);
    else if (std::strcmp (s.unit, "Hz") == 0)
        std::snprintf (out, (size_t) outSize, "%.1f Hz", x);
    else if (std::strcmp (s.unit, "ms") == 0)
        std::snprintf (out, (size_t) outSize, x < 10.0f ? "%.2f ms" : "%.0f ms", x);
    else if (std::strcmp (s.unit, "bit") == 0)
        std::snprintf (out, (size_t) outSize, "%.0f bit", std::round (x));
    else if (std::strcmp (s.unit, "x") == 0)
        std::snprintf (out, (size_t) outSize, "%.2fx", x);
    else if (std::strcmp (s.unit, "Q") == 0)
        std::snprintf (out, (size_t) outSize, "Q %.2f", x);
    else if (std::strcmp (s.unit, "oct") == 0)
        std::snprintf (out, (size_t) outSize, "%+.2f oct", x * 4.0f);
    else
        std::snprintf (out, (size_t) outSize, "%.3f", x);
}
} // namespace

const char* voiceTypeName (VoiceType t)
{
    const int i = (int) t;
    return i >= 0 && i < kNumVoiceTypes ? kVoiceNames[i] : "KICK";
}

VoiceType voiceTypeFromName (const char* name)
{
    for (int i = 0; i < kNumVoiceTypes; ++i)
        if (std::strcmp (name, kVoiceNames[i]) == 0)
            return (VoiceType) i;

    return VoiceType::Kick;
}

const char* voiceParamName (VoiceType t, int index)
{
    return kVoiceSpecs[(int) t][index].name;
}

VoiceParams defaultVoiceParams (VoiceType t)
{
    VoiceParams p {};
    for (int i = 0; i < kNumVoiceParams; ++i)
        p[(size_t) i] = kVoiceSpecs[(int) t][i].defaultValue;
    return p;
}

float voiceParamValue (VoiceType t, int index, float v)
{
    return mapSpec (kVoiceSpecs[(int) t][index], v);
}

const char* fxParamName (int index)
{
    return kFxSpecs[index].name;
}

FxParams defaultFxParams()
{
    FxParams p {};
    for (int i = 0; i < kNumFxParams; ++i)
        p[(size_t) i] = kFxSpecs[i].defaultValue;
    return p;
}

float expMap (float v, float lo, float hi) { return lo * std::pow (hi / lo, v); }
float linMap (float v, float lo, float hi) { return lo + (hi - lo) * v; }

float levelToGain (float v) { return 2.0f * v * v; }
float gainToDb (float gain) { return gain > 0.00001f ? 20.0f * std::log10 (gain) : -100.0f; }
float panToBalance (float v) { return v * 2.0f - 1.0f; }

float cutoffHz (float v) { return mapSpec (kFxSpecs[fxCutoff], v); }
float resonanceQ (float v) { return mapSpec (kFxSpecs[fxResonance], v); }
float crushBits (float v) { return mapSpec (kFxSpecs[fxCrushBits], v); }
float crushFactor (float v) { return mapSpec (kFxSpecs[fxCrushRate], v); }
float delayMs (float v) { return mapSpec (kFxSpecs[fxDelayTime], v); }
float delayFeedback (float v) { return mapSpec (kFxSpecs[fxDelayFeedback], v); }
float reverbRoom (float v) { return mapSpec (kFxSpecs[fxReverbSize], v); }

void formatVoiceParam (VoiceType t, int index, float v, char* out, int outSize)
{
    formatSpec (kVoiceSpecs[(int) t][index], v, out, outSize);
}

void formatFxParam (int index, float v, char* out, int outSize)
{
    formatSpec (kFxSpecs[index], v, out, outSize);
}
} // namespace ms
