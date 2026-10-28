#pragma once

#include "Params.h"

namespace ms
{
struct SampleData;

// The Alesis HR-16 sounds, compiled into the app and grouped by the drum they
// stand in for (kick, hat/cymbal, snare, tom, percussion).
int hr16SoundCount (VoiceType);
int hr16FileNumber (VoiceType, int index); // the NN in Alesis_HR16A_NN.wav

// Decodes the embedded WAVs. Call once before audio starts (cheap afterwards).
void loadHr16Bank();

// nullptr if the index is out of range or the bank isn't loaded.
const SampleData* hr16Sample (VoiceType, int index);

#if MS_NO_JUCE
// Without JUCE the host decodes the WAVs and hands each one over (mono).
constexpr int kNumHr16Files = 56;
void setHr16File (int fileNumber, const float* samples, int length, double sampleRate);
#endif
} // namespace ms
