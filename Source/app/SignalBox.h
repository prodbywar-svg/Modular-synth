#pragma once

#include "Widgets.h"

#include "engine/Params.h"

// A white (or coloured) label with black text, like the word boxes in the
// Datamatics installation. Returns its width.
float paintTag (juce::Graphics&, const juce::String& text, float x, float y, juce::Colour fill);

// The signal panel at the end of a track row: an annotated harmonic lattice
// of the track's newest samples (see paint). One instance can draw any number
// of tracks in turn.
class SignalBox
{
public:
    static constexpr int kFftSize = 2048;

    SignalBox();

    // Fill with the track's newest kFftSize samples, oldest first, then paint.
    float* scope() { return scopeBuf.data(); }

    // f0: the sound's pitch in Hz (0 = unpitched); pitchText labels it.
    void paint (juce::Graphics&, juce::Rectangle<float> box, double sampleRate, float f0, const juce::String& pitchText, float alpha);

private:
    void paintPhasePortrait (juce::Graphics&, juce::Rectangle<float> r, float f0, double sr);

    std::array<float, kFftSize> scopeBuf {}, fftRe {}, fftIm {}, fftWindow {};
    std::array<float, kFftSize / 2> mags {};
};
