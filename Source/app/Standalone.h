#pragma once

#include <juce_core/juce_core.h>

// The standalone app's audio device, for the loopers: none of this does
// anything when running as a plugin.
namespace standalone
{
// JUCE mutes the input of a standalone app with an input until told not to.
// Nothing reaches the speakers from it unless a looper monitors it.
void unmuteInput();
// How late the input is after the sound it was played to: output + input
// latency of the device, in samples.
int roundTripLatency();
// "FOCUSRITE USB // 2 IN" or "NO INPUT".
juce::String inputDescription();
void showAudioSettings();
} // namespace standalone
