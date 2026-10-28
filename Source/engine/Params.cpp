#include "Params.h"

#include "Hr16Bank.h"

#include <algorithm>
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
    Exp,
    Step,   // integer positions lo..hi
    Toggle, // MS / NOTE
    Sound,  // HR-16 sound number, range from the bank
};

struct ParamSpec
{
    const char* name; // nullptr = slot unused
    Curve curve;
    float lo, hi;
    const char* unit;
    float defaultValue;
    bool pitch = false;
};

constexpr ParamSpec kUnused { nullptr, Curve::Lin, 0.0f, 1.0f, "", 0.0f };
constexpr ParamSpec kHr16Sound { "SOUND", Curve::Sound, 0.0f, 0.0f, "snd", 0.0f };
constexpr ParamSpec kHr16Tune { "TUNE", Curve::Lin, -12.0f, 12.0f, "st", 0.5f };
constexpr ParamSpec kHr16Decay { "DECAY", Curve::Exp, 10.0f, 4000.0f, "ms", 1.0f };

// clang-format off
// [voice][character][param], characters in the order LIVE, DIGITAL, HR-16.
const ParamSpec kVoiceSpecs[kNumVoiceTypes][kNumCharacters][kNumVoiceParams] = {
    { // KICK
        { { "TUNE",   Curve::Exp, 40.0f, 110.0f,   "Hz", 0.315f, true },
          { "DECAY",  Curve::Exp, 80.0f, 900.0f,   "ms", 0.50f },
          { "BEATER", Curve::Lin, 0.0f,  1.0f,     "",   0.50f },
          { "SHELL",  Curve::Lin, 0.0f,  1.0f,     "",   0.40f } },
        { { "PITCH",  Curve::Exp, 30.0f, 120.0f,   "Hz", 0.35f, true },
          { "DECAY",  Curve::Exp, 40.0f, 1500.0f,  "ms", 0.45f },
          { "DRIVE",  Curve::Lin, 1.0f,  40.0f,    "x",  0.60f },
          { "CLICK",  Curve::Lin, 0.0f,  1.0f,     "",   0.60f } },
        { kHr16Sound, kHr16Tune, kHr16Decay, kUnused },
    },
    { // CLICK / HAT
        { { "DECAY",  Curve::Exp, 5.0f,  900.0f,   "ms", 0.30f },
          { "TONE",   Curve::Exp, 3000.0f, 14000.0f, "Hz", 0.50f },
          { "METAL",  Curve::Lin, 0.0f,  1.0f,     "",   0.50f },
          kUnused },
        { { "DECAY",  Curve::Exp, 0.3f,  80.0f,    "ms", 0.25f },
          { "TONE",   Curve::Exp, 1000.0f, 16000.0f, "Hz", 0.60f },
          { "NOISE",  Curve::Lin, 0.0f,  1.0f,     "",   0.30f },
          kUnused },
        { kHr16Sound, kHr16Tune, kHr16Decay, kUnused },
    },
    { // NOISE / SNARE
        { { "TUNE",   Curve::Exp, 120.0f, 320.0f,  "Hz", 0.40f, true },
          { "DECAY",  Curve::Exp, 40.0f, 600.0f,   "ms", 0.45f },
          { "WIRES",  Curve::Lin, 0.0f,  1.0f,     "",   0.60f },
          { "TONE",   Curve::Exp, 2000.0f, 16000.0f, "Hz", 0.60f } },
        { { "DECAY",  Curve::Exp, 5.0f,  600.0f,   "ms", 0.45f },
          { "HIPASS", Curve::Exp, 20.0f, 8000.0f,  "Hz", 0.40f },
          { "SNAP",   Curve::Lin, 0.0f,  1.0f,     "",   0.70f },
          { "COLOR",  Curve::Exp, 1000.0f, 20000.0f, "Hz", 1.00f } },
        { kHr16Sound, kHr16Tune, kHr16Decay, kUnused },
    },
    { // SINE / TOM
        { { "PITCH",  Curve::Exp, 50.0f, 400.0f,   "Hz", 0.45f, true },
          { "DECAY",  Curve::Exp, 80.0f, 1200.0f,  "ms", 0.50f },
          { "STICK",  Curve::Lin, 0.0f,  1.0f,     "",   0.40f },
          { "BEND",   Curve::Lin, 0.0f,  1.0f,     "",   0.50f } },
        { { "PITCH",  Curve::Exp, 50.0f, 16000.0f, "Hz", 0.75f, true },
          { "LENGTH", Curve::Exp, 1.0f,  500.0f,   "ms", 0.30f },
          { "SWEEP",  Curve::Lin, -1.0f, 1.0f,     "oct", 0.50f },
          kUnused },
        { kHr16Sound, kHr16Tune, kHr16Decay, kUnused },
    },
    { // GLITCH / CLAP / PERC
        { { "DECAY",  Curve::Exp, 50.0f, 800.0f,   "ms", 0.40f },
          { "TONE",   Curve::Exp, 600.0f, 3000.0f, "Hz", 0.50f },
          { "SPREAD", Curve::Exp, 4.0f,  20.0f,    "ms", 0.50f },
          kUnused },
        { { "RATE",   Curve::Exp, 50.0f, 20000.0f, "Hz", 0.60f, true },
          { "BITS",   Curve::Lin, 1.0f,  8.0f,     "bit", 0.30f },
          { "LENGTH", Curve::Exp, 5.0f,  400.0f,   "ms", 0.40f },
          kUnused },
        { kHr16Sound, kHr16Tune, kHr16Decay, kUnused },
    },
    { // SAMPLE (same for every character)
        { { "PITCH",  Curve::Exp, 0.25f, 4.0f,     "x",  0.50f },
          { "DECAY",  Curve::Exp, 10.0f, 8000.0f,  "ms", 1.00f },
          { "START",  Curve::Lin, 0.0f,  0.9f,     "",   0.00f },
          kUnused },
        { { "PITCH",  Curve::Exp, 0.25f, 4.0f,     "x",  0.50f },
          { "DECAY",  Curve::Exp, 10.0f, 8000.0f,  "ms", 1.00f },
          { "START",  Curve::Lin, 0.0f,  0.9f,     "",   0.00f },
          kUnused },
        { { "PITCH",  Curve::Exp, 0.25f, 4.0f,     "x",  0.50f },
          { "DECAY",  Curve::Exp, 10.0f, 8000.0f,  "ms", 1.00f },
          { "START",  Curve::Lin, 0.0f,  0.9f,     "",   0.00f },
          kUnused },
    },
};

const ParamSpec kFxSpecs[kNumFxParams] = {
    { "BITS",      Curve::Lin,  16.0f, 1.0f,     "bit", 0.00f },
    { "DOWNSMP",   Curve::Lin,  1.0f,  64.0f,    "x",   0.00f },

    { "DRIVE",     Curve::Exp,  1.0f,  60.0f,    "x",   0.00f },
    { "DIST TYPE", Curve::Step, 0.0f,  3.0f,     "type", 0.00f },
    { "DIST TONE", Curve::Exp,  500.0f, 20000.0f, "Hz", 1.00f },
    { "DIST MIX",  Curve::Lin,  0.0f,  1.0f,     "",    0.00f },

    { "CUTOFF",    Curve::Exp,  20.0f, 20000.0f, "Hz",  1.00f },
    { "RESO",      Curve::Lin,  0.5f,  10.0f,    "Q",   0.00f },

    { "DLY TIME",  Curve::Exp,  5.0f,  2000.0f,  "ms",  0.50f },
    { "DLY SYNC",  Curve::Toggle, 0.0f, 1.0f,    "",    1.00f },
    { "DLY FDBK",  Curve::Lin,  0.0f,  0.95f,    "",    0.35f },
    { "DLY MIX",   Curve::Lin,  0.0f,  1.0f,     "",    0.00f },

    { "GRN TIME",  Curve::Exp,  10.0f, 2000.0f,  "ms",  0.50f },
    { "GRN SIZE",  Curve::Exp,  10.0f, 500.0f,   "ms",  0.45f },
    { "GRN SYNC",  Curve::Toggle, 0.0f, 1.0f,    "",    0.00f },
    { "GRN PITCH", Curve::Step, -12.0f, 12.0f,   "st",  0.50f },
    { "GRN SPRAY", Curve::Lin,  0.0f,  1.0f,     "",    0.20f },
    { "GRN FDBK",  Curve::Lin,  0.0f,  0.9f,     "",    0.30f },
    { "GRN MIX",   Curve::Lin,  0.0f,  1.0f,     "",    0.00f },

    { "REV TIME",  Curve::Exp,  100.0f, 20000.0f, "ms", 0.45f },
    { "REV SYNC",  Curve::Toggle, 0.0f, 1.0f,    "",    0.00f },
    { "REV DAMP",  Curve::Lin,  0.0f,  1.0f,     "",    0.40f },
    { "REV MIX",   Curve::Lin,  0.0f,  1.0f,     "",    0.00f },

    { "PSY DEPTH", Curve::Lin,  0.0f,  1.0f,     "",    0.60f },
    { "PSY STAGES", Curve::Step, 1.0f, 6.0f,     "stg", 1.00f },
    { "PSY TIME",  Curve::Exp,  5.0f,  1000.0f,  "ms",  0.40f },
    { "PSY SYNC",  Curve::Toggle, 0.0f, 1.0f,    "",    0.00f },
    { "PSY MIX",   Curve::Lin,  0.0f,  1.0f,     "",    0.00f },

    { "CHR MODE",  Curve::Toggle, 0.0f, 1.0f,    "mode", 0.00f },
    { "CHR RATE",  Curve::Exp,  0.05f, 5.0f,     "Hz",  0.55f },
    { "CHR DEPTH", Curve::Lin,  0.0f,  1.0f,     "",    0.50f },
    { "CHR MIX",   Curve::Lin,  0.0f,  1.0f,     "",    0.00f },
};
// clang-format on

const char* const kVoiceNames[kNumVoiceTypes] = { "KICK", "CLICK", "NOISE", "SINE", "GLITCH", "SAMPLE" };
const char* const kCharacterNames[kNumCharacters] = { "LIVE", "DIGITAL", "HR-16" };
const char* const kDrumNames[kNumVoiceTypes][kNumCharacters] = {
    { "KICK", "KICK", "KICK" },
    { "HAT", "CLICK", "HAT" },
    { "SNARE", "NOISE", "SNARE" },
    { "TOM", "SINE", "TOM" },
    { "CLAP", "GLITCH", "PERC" },
    { "SAMPLE", "SAMPLE", "SAMPLE" },
};
const char* const kDistTypes[] = { "SOFT", "HARD", "FOLD", "RECT" };

struct Division
{
    const char* name;
    double beats;
};

const Division kDivisions[kNumNoteDivisions] = {
    { "1/64", 0.0625 }, { "1/32T", 1.0 / 12.0 }, { "1/32", 0.125 }, { "1/16T", 1.0 / 6.0 },
    { "1/16", 0.25 },   { "1/8T", 1.0 / 3.0 },   { "1/16D", 0.375 }, { "1/8", 0.5 },
    { "1/4T", 2.0 / 3.0 }, { "1/8D", 0.75 },     { "1/4", 1.0 },     { "1/2T", 4.0 / 3.0 },
    { "1/4D", 1.5 },    { "1/2", 2.0 },          { "1/2D", 3.0 },    { "1 BAR", 4.0 },
    { "2 BARS", 8.0 },  { "4 BARS", 16.0 },      { "8 BARS", 32.0 },
};

const ParamSpec& voiceSpec (VoiceType t, Character c, int index)
{
    const int ti = std::clamp ((int) t, 0, kNumVoiceTypes - 1);
    const int ci = std::clamp ((int) c, 0, kNumCharacters - 1);
    return kVoiceSpecs[ti][ci][std::clamp (index, 0, kNumVoiceParams - 1)];
}

int stepsOf (const ParamSpec& s, VoiceType t)
{
    if (s.curve == Curve::Sound)
        return std::max (1, hr16SoundCount (t));
    if (s.curve == Curve::Step)
        return (int) std::lround (std::abs (s.hi - s.lo)) + 1;
    if (s.curve == Curve::Toggle)
        return 2;
    return 0;
}

float stepIndex (float v, int steps)
{
    return (float) std::lround (std::clamp (v, 0.0f, 1.0f) * (float) (steps - 1));
}

float mapSpec (const ParamSpec& s, float v, VoiceType t = VoiceType::Kick)
{
    switch (s.curve)
    {
        case Curve::Exp:    return expMap (v, s.lo, s.hi);
        case Curve::Lin:    return linMap (v, s.lo, s.hi);
        case Curve::Step:   return s.lo + stepIndex (v, stepsOf (s, t)) * (s.hi > s.lo ? 1.0f : -1.0f);
        case Curve::Sound:  return stepIndex (v, stepsOf (s, t));
        case Curve::Toggle: return v >= 0.5f ? 1.0f : 0.0f;
    }
    return v;
}

void formatMs (float ms, char* out, int outSize)
{
    if (ms >= 1000.0f)
        std::snprintf (out, (size_t) outSize, "%.2f s", ms / 1000.0f);
    else
        std::snprintf (out, (size_t) outSize, ms < 10.0f ? "%.2f ms" : "%.0f ms", ms);
}

void formatSpec (const ParamSpec& s, float v, char* out, int outSize, VoiceType t = VoiceType::Kick)
{
    if (s.name == nullptr)
    {
        std::snprintf (out, (size_t) outSize, "-");
        return;
    }

    const float x = mapSpec (s, v, t);
    const char* u = s.unit;

    if (s.pitch)
    {
        char note[16];
        noteNameForFrequency (x, note, (int) sizeof (note));
        if (x >= 1000.0f)
            std::snprintf (out, (size_t) outSize, "%s %.2fk", note, x / 1000.0f);
        else
            std::snprintf (out, (size_t) outSize, "%s %.1f Hz", note, x);
    }
    else if (std::strcmp (u, "Hz") == 0)
        std::snprintf (out, (size_t) outSize, x >= 1000.0f ? "%.2f kHz" : x < 10.0f ? "%.2f Hz" : "%.1f Hz", x >= 1000.0f ? x / 1000.0f : x);
    else if (std::strcmp (u, "ms") == 0)
        formatMs (x, out, outSize);
    else if (std::strcmp (u, "bit") == 0)
        std::snprintf (out, (size_t) outSize, "%.0f bit", std::round (x));
    else if (std::strcmp (u, "x") == 0)
        std::snprintf (out, (size_t) outSize, "%.2fx", x);
    else if (std::strcmp (u, "Q") == 0)
        std::snprintf (out, (size_t) outSize, "Q %.2f", x);
    else if (std::strcmp (u, "oct") == 0)
        std::snprintf (out, (size_t) outSize, "%+.2f oct", x * 4.0f);
    else if (std::strcmp (u, "st") == 0)
        std::snprintf (out, (size_t) outSize, std::abs (x) < 0.05f ? "0 st" : "%+.1f st", x);
    else if (std::strcmp (u, "snd") == 0)
        std::snprintf (out, (size_t) outSize, "%02d / HR16 #%02d", (int) x + 1, hr16FileNumber (t, (int) x));
    else if (std::strcmp (u, "type") == 0)
        std::snprintf (out, (size_t) outSize, "%s", kDistTypes[std::clamp ((int) x, 0, 3)]);
    else if (std::strcmp (u, "stg") == 0)
        std::snprintf (out, (size_t) outSize, "%d x OTT", (int) x);
    else if (std::strcmp (u, "mode") == 0)
        std::snprintf (out, (size_t) outSize, "%s", x > 0.5f ? "DOUBLER" : "ENSEMBLE");
    else if (s.curve == Curve::Toggle)
        std::snprintf (out, (size_t) outSize, "%s", x > 0.5f ? "NOTE" : "MS");
    else
        std::snprintf (out, (size_t) outSize, "%.3f", x);
}
} // namespace

// ---- voices -------------------------------------------------------------------

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

const char* characterName (Character c)
{
    const int i = (int) c;
    return i >= 0 && i < kNumCharacters ? kCharacterNames[i] : "DIGITAL";
}

Character characterFromName (const char* name)
{
    for (int i = 0; i < kNumCharacters; ++i)
        if (std::strcmp (name, kCharacterNames[i]) == 0)
            return (Character) i;
    return Character::Digital;
}

const char* drumName (VoiceType t, Character c)
{
    return kDrumNames[std::clamp ((int) t, 0, kNumVoiceTypes - 1)][std::clamp ((int) c, 0, kNumCharacters - 1)];
}

const char* voiceParamName (VoiceType t, Character c, int index)
{
    return voiceSpec (t, c, index).name;
}

VoiceParams defaultVoiceParams (VoiceType t, Character c)
{
    VoiceParams p {};
    for (int i = 0; i < kNumVoiceParams; ++i)
        p[(size_t) i] = voiceSpec (t, c, i).defaultValue;
    return p;
}

float voiceParamValue (VoiceType t, Character c, int index, float v)
{
    return mapSpec (voiceSpec (t, c, index), v, t);
}

bool voiceParamIsPitch (VoiceType t, Character c, int index)
{
    return voiceSpec (t, c, index).pitch;
}

int voiceParamSteps (VoiceType t, Character c, int index)
{
    const auto& s = voiceSpec (t, c, index);
    if (s.unit != nullptr && std::strcmp (s.unit, "st") == 0)
        return (int) std::lround (s.hi - s.lo) + 1; // semitone steps
    return stepsOf (s, t);
}

float snapVoiceParamToNote (VoiceType t, Character c, int index, float v)
{
    const auto& s = voiceSpec (t, c, index);
    if (! s.pitch || s.curve != Curve::Exp)
        return v;

    const double hz = expMap (v, s.lo, s.hi);
    const double midi = std::round (69.0 + 12.0 * std::log2 (hz / 440.0));
    double snapped = 440.0 * std::pow (2.0, (midi - 69.0) / 12.0);
    // stay inside the range: step inwards if the nearest note falls outside
    if (snapped < s.lo) snapped *= std::pow (2.0, 1.0 / 12.0);
    if (snapped > s.hi) snapped /= std::pow (2.0, 1.0 / 12.0);
    return (float) std::clamp (std::log (snapped / s.lo) / std::log ((double) s.hi / s.lo), 0.0, 1.0);
}

void formatVoiceParam (VoiceType t, Character c, int index, float v, char* out, int outSize)
{
    formatSpec (voiceSpec (t, c, index), v, out, outSize, t);
}

// ---- effects ------------------------------------------------------------------

const char* fxParamName (int index)
{
    return kFxSpecs[index].name;
}

int fxParamFromName (const char* name)
{
    for (int i = 0; i < kNumFxParams; ++i)
        if (std::strcmp (name, kFxSpecs[i].name) == 0)
            return i;
    return -1;
}

FxParams defaultFxParams()
{
    FxParams p {};
    for (int i = 0; i < kNumFxParams; ++i)
        p[(size_t) i] = kFxSpecs[i].defaultValue;
    return p;
}

float fxParamValue (int index, float v)
{
    return mapSpec (kFxSpecs[index], v);
}

bool fxParamIsToggle (int index)
{
    return kFxSpecs[index].curve == Curve::Toggle;
}

int fxParamSteps (int index)
{
    return stepsOf (kFxSpecs[index], VoiceType::Kick);
}

int fxSyncParamFor (int index)
{
    switch (index)
    {
        case fxDelayTime:  return fxDelaySync;
        case fxGrainTime:
        case fxGrainSize:  return fxGrainSync;
        case fxReverbTime: return fxReverbSync;
        case fxPsyTime:    return fxPsySync;
        default:           return -1;
    }
}

double fxTimeSeconds (const FxParams& p, int index, double bpm)
{
    const int sync = fxSyncParamFor (index);
    const float v = p[(size_t) index];
    if (sync >= 0 && p[(size_t) sync] >= 0.5f)
        return noteDivisionBeats (noteDivisionIndex (v)) * 60.0 / std::max (1.0, bpm);
    return mapSpec (kFxSpecs[index], v) * 0.001;
}

void formatFxParam (const FxParams& p, int index, char* out, int outSize)
{
    const int sync = fxSyncParamFor (index);
    if (sync >= 0 && p[(size_t) sync] >= 0.5f)
    {
        std::snprintf (out, (size_t) outSize, "%s", noteDivisionName (noteDivisionIndex (p[(size_t) index])));
        return;
    }
    formatSpec (kFxSpecs[index], p[(size_t) index], out, outSize);
}

// ---- note lengths -------------------------------------------------------------

const char* noteDivisionName (int d) { return kDivisions[std::clamp (d, 0, kNumNoteDivisions - 1)].name; }
double noteDivisionBeats (int d) { return kDivisions[std::clamp (d, 0, kNumNoteDivisions - 1)].beats; }
int noteDivisionIndex (float v) { return (int) stepIndex (v, kNumNoteDivisions); }

// ---- pitch --------------------------------------------------------------------

void noteNameForFrequency (float hz, char* out, int outSize)
{
    static const char* const names[] = { "C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B" };
    if (hz <= 0.0f)
    {
        std::snprintf (out, (size_t) outSize, "-");
        return;
    }
    const double midi = 69.0 + 12.0 * std::log2 (hz / 440.0);
    const int nearest = (int) std::lround (midi);
    const int cents = (int) std::lround ((midi - nearest) * 100.0);
    const int octave = (int) std::floor (nearest / 12.0) - 1;
    const char* name = names[((nearest % 12) + 12) % 12];
    if (cents == 0)
        std::snprintf (out, (size_t) outSize, "%s%d", name, octave);
    else
        std::snprintf (out, (size_t) outSize, "%s%d%+dc", name, octave, cents);
}

// ---- general mappings ---------------------------------------------------------

float expMap (float v, float lo, float hi) { return lo * std::pow (hi / lo, v); }
float linMap (float v, float lo, float hi) { return lo + (hi - lo) * v; }

float levelToGain (float v) { return 2.0f * v * v; }
float gainToDb (float gain) { return gain > 0.00001f ? 20.0f * std::log10 (gain) : -100.0f; }
float panToBalance (float v) { return v * 2.0f - 1.0f; }

float cutoffHz (float v) { return mapSpec (kFxSpecs[fxCutoff], v); }
float resonanceQ (float v) { return mapSpec (kFxSpecs[fxResonance], v); }
float crushBits (float v) { return mapSpec (kFxSpecs[fxCrushBits], v); }
float crushFactor (float v) { return mapSpec (kFxSpecs[fxCrushRate], v); }
float delayFeedback (float v) { return mapSpec (kFxSpecs[fxDelayFeedback], v); }
} // namespace ms
