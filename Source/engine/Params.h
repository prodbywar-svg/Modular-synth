#pragma once

#include <array>

// All user-facing values are stored normalised to 0..1. The functions here turn
// them into real units (Hz, ms, dB...) for the DSP and for on-screen readouts.

namespace ms
{
constexpr int kNumTracks = 8;
constexpr int kNumSteps = 16;
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

enum FxParam
{
    fxCutoff,
    fxResonance,
    fxCrushBits,
    fxCrushRate,
    fxDelayTime,
    fxDelayFeedback,
    fxDelayMix,
    fxReverbSize,
    fxReverbMix,
    kNumFxParams
};

using VoiceParams = std::array<float, kNumVoiceParams>;
using FxParams = std::array<float, kNumFxParams>;

const char* voiceTypeName (VoiceType);
VoiceType voiceTypeFromName (const char*);

// Name of a voice parameter, or nullptr when that voice doesn't use the slot.
const char* voiceParamName (VoiceType, int index);
VoiceParams defaultVoiceParams (VoiceType);

// A voice parameter in real units (Hz, ms, ratio...), as the DSP uses it.
float voiceParamValue (VoiceType, int index, float v);

const char* fxParamName (int index);
FxParams defaultFxParams();

// ---- unit mappings (shared by DSP and UI) ---------------------------------
float expMap (float v, float lo, float hi); // exponential sweep lo..hi
float linMap (float v, float lo, float hi);

float levelToGain (float v); // 0..1 -> 0..2 (0.7071 = unity)
float gainToDb (float gain);
float panToBalance (float v); // -1..1

float cutoffHz (float v);
float resonanceQ (float v);
float crushBits (float v);   // 16 (off) .. 1
float crushFactor (float v); // 1 (off) .. 64 samples held
float delayMs (float v);
float delayFeedback (float v);
float reverbRoom (float v);

// Text for a readout, e.g. "1.2 kHz", "-3.0 dB", "48 ms".
void formatVoiceParam (VoiceType, int index, float v, char* out, int outSize);
void formatFxParam (int index, float v, char* out, int outSize);
} // namespace ms
