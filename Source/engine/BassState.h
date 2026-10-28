#pragma once

#include "ProjectState.h"

namespace ms
{
// The bass synth: 8 rows, one sound each, on the same 16 steps x 4 patterns
// as the drums. Every lit step holds a note, a velocity and a length. Rows
// joined by patch cables form a cut group: a new note in one cuts the others.
constexpr int kBassRows = 8;
constexpr int kBassLowNote = 24;  // C1
constexpr int kBassHighNote = 72; // C5
constexpr int kMaxStepLength = kNumSteps;

enum class BassSound
{
    Sub,    // sine with added harmonics
    Pulse,  // pulse wave, variable width
    Saw,    // saw with a sub-octave sine
    Acid,   // saw / square through a screaming resonant filter
    Fm,     // two-operator FM growl
    Reese,  // two detuned saws
    OneBit, // 1-bit square, sample-rate crushed
    Boom,   // sine with a pitch drop, 808 style
    Count
};
constexpr int kNumBassSounds = (int) BassSound::Count;

enum BassParam
{
    bpTone, // what it does depends on the sound, see bassParamName
    bpCutoff,
    bpResonance,
    bpEnvAmount,
    bpFilterDecay,
    bpDrive,
    bpGlide,
    bpAttack,
    bpDecay,
    bpSustain,
    bpRelease,
    kNumBassParams
};
using BassParams = std::array<float, kNumBassParams>;

using NotePattern = std::array<std::int8_t, kNumSteps>;    // MIDI note per step
using LengthPattern = std::array<std::uint8_t, kNumSteps>; // steps a note holds, 1..16

struct BassRow
{
    BassSound sound = BassSound::Sub;
    BassParams params {};
    float level = 0.7071f; // unity gain
    float pan = 0.5f;
    bool mute = false;
    bool solo = false;
    FxParams fx = defaultFxParams();
    FxAutomation fxAuto;
    std::array<Pattern, kNumPatterns> velocity {}; // 0 = step off
    std::array<NotePattern, kNumPatterns> notes {};
    std::array<LengthPattern, kNumPatterns> lengths {};
    int cable = -1; // the row this row's cable goes to, -1 = none
};

struct BassState
{
    float bpm = 132.0f;
    float swing = 0.0f;
    std::array<BassRow, kBassRows> rows {};
    float masterLevel = 0.5f; // the bass bus, -6 dB like the drum bus
    FxParams masterFx = defaultFxParams();
    FxAutomation masterFxAuto;
    int pattern = 0;
    int queuedPattern = -1;
    int key = 0;   // 0 = C .. 11 = B
    int scale = 2; // see scaleName; 2 = minor
};

inline int viewedPattern (const BassState& b) { return b.queuedPattern >= 0 ? b.queuedPattern : b.pattern; }

// The starter bass synth: one sound per row, a bassline on P1.
BassState makeDefaultBass();

// ---- sounds and their controls ----------------------------------------------
const char* bassSoundName (BassSound);
BassSound bassSoundFromName (const char*);
const char* bassParamName (BassSound, int index);
BassParams defaultBassParams (BassSound);
float bassParamValue (BassSound, int index, float v); // Hz, ms, octaves...
void formatBassParam (BassSound, int index, float v, char* out, int outSize);

// ---- notes and scales -------------------------------------------------------
constexpr int kNumScales = 13;
const char* scaleName (int scale);
int scaleFromName (const char*);
const char* keyName (int key);
// "C2", "D#3"
void noteName (int midiNote, char* out, int outSize);
float noteHz (float midiNote);
bool noteInScale (int midiNote, int key, int scale);
// The nearest note in the scale (the lower one on a tie), inside the bass range.
int snapToScale (int midiNote, int key, int scale);
// Moves a note by a number of scale steps, inside the bass range.
int stepInScale (int midiNote, int key, int scale, int steps);

// Which cut group each row is in: rows joined by cables share a number.
std::array<int, kBassRows> cutGroups (const BassState&);

// Applies a state-changing command for this bass synth (its tool already
// matched). Trigger, NoteOff and transport commands are ignored.
void applyToBass (BassState&, const Command&);

#if ! MS_NO_JUCE
juce::var bassToVar (const BassState&);
bool bassFromVar (const juce::var&, BassState&);
#endif
} // namespace ms
