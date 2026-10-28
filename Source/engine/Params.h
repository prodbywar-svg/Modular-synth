#pragma once

#include <array>

// All user-facing values are stored normalised to 0..1. The functions here turn
// them into real units (Hz, ms, dB, note lengths...) for the DSP and for
// on-screen readouts.

namespace ms
{
constexpr int kNumTracks = 8;
constexpr int kNumSteps = 16;
constexpr int kNumPatterns = 4;
constexpr int kNumVoiceParams = 4;

enum class VoiceType
{
    Kick,
    ClickHat,
    NoiseSnare,
    SineTone,
    Glitch,
    Sample,
    Count
};

constexpr int kNumVoiceTypes = (int) VoiceType::Count;

// Three takes on every drum: acoustic-sounding, the stark datamatics synth,
// and the Alesis HR-16 samples.
enum class Character
{
    Live,
    Digital,
    Hr16,
    Count
};

constexpr int kNumCharacters = (int) Character::Count;

enum FxParam
{
    fxCrushBits,
    fxCrushRate,

    fxDistDrive,
    fxDistType,
    fxDistTone,
    fxDistMix,

    fxCutoff,
    fxResonance,

    fxDelayTime,
    fxDelaySync,
    fxDelayFeedback,
    fxDelayMix,

    fxGrainTime,
    fxGrainSize,
    fxGrainSync,
    fxGrainPitch,
    fxGrainSpray,
    fxGrainFeedback,
    fxGrainMix,

    fxReverbTime,
    fxReverbSync,
    fxReverbDamp,
    fxReverbMix,

    fxPsyDepth,
    fxPsyStages,
    fxPsyTime,
    fxPsySync,
    fxPsyMix,

    // added after the original rack, so older files and the browser version
    // keep their numbering; it runs after the filter
    fxChorusMode, // ENSEMBLE / DOUBLER
    fxChorusRate,
    fxChorusDepth,
    fxChorusMix,

    kNumFxParams
};

using VoiceParams = std::array<float, kNumVoiceParams>;
using FxParams = std::array<float, kNumFxParams>;

// ---- voices -----------------------------------------------------------------
const char* voiceTypeName (VoiceType);
VoiceType voiceTypeFromName (const char*);
const char* characterName (Character);
Character characterFromName (const char*);

// What the drum is called with that character, e.g. SINE (digital) / TOM (live).
const char* drumName (VoiceType, Character);

// Name of a voice parameter, or nullptr when that voice doesn't use the slot.
const char* voiceParamName (VoiceType, Character, int index);
VoiceParams defaultVoiceParams (VoiceType, Character);

// A voice parameter in real units (Hz, ms, semitones, sound number...).
float voiceParamValue (VoiceType, Character, int index, float v);

// True for parameters that set a pitch: they show a note name and snap to
// semitones while dragged.
bool voiceParamIsPitch (VoiceType, Character, int index);
// Number of discrete positions (e.g. HR-16 sounds), or 0 for a continuous control.
int voiceParamSteps (VoiceType, Character, int index);
// Nearest value whose pitch is an exact equal-tempered note (A4 = 440 Hz).
float snapVoiceParamToNote (VoiceType, Character, int index, float v);

void formatVoiceParam (VoiceType, Character, int index, float v, char* out, int outSize);

// ---- effects ----------------------------------------------------------------
const char* fxParamName (int index);
int fxParamFromName (const char*);
FxParams defaultFxParams();

// An effect parameter in real units (ignores sync; see fxTimeSeconds).
float fxParamValue (int index, float v);
bool fxParamIsToggle (int index);
int fxParamSteps (int index); // discrete positions, 0 = continuous

// For a time parameter, the toggle that switches it between ms and note
// lengths; -1 for anything else.
int fxSyncParamFor (int index);

// Seconds for a time parameter, honouring its sync toggle and the tempo.
double fxTimeSeconds (const FxParams&, int index, double bpm);

// Readout text; the whole set is needed because time params depend on sync.
void formatFxParam (const FxParams&, int index, char* out, int outSize);

// ---- note lengths -----------------------------------------------------------
constexpr int kNumNoteDivisions = 19;
const char* noteDivisionName (int division);
double noteDivisionBeats (int division); // quarter note = 1
int noteDivisionIndex (float v);         // 0..1 -> division

// ---- pitch ------------------------------------------------------------------
// "E1", "F#3 +12c": nearest note and cents.
void noteNameForFrequency (float hz, char* out, int outSize);

// ---- general mappings -------------------------------------------------------
float expMap (float v, float lo, float hi);
float linMap (float v, float lo, float hi);

float levelToGain (float v); // 0..1 -> 0..2 (0.7071 = unity)
float gainToDb (float gain);
float panToBalance (float v); // -1..1

float cutoffHz (float v);
float resonanceQ (float v);
float crushBits (float v);   // 16 (off) .. 1
float crushFactor (float v); // 1 (off) .. 64 samples held
float delayFeedback (float v);
} // namespace ms
