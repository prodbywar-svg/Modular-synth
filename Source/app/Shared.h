#pragma once

#include "Widgets.h"

#include "engine/Params.h"

// Pieces shared by the studio window and the tool windows.

juce::String formatTime (double seconds);
juce::String formatDb (float gain);
juce::String formatPan (float v);

// A horizontal peak meter centred on y = mid: a bar, a falling peak-hold mark
// (updated in hold), a red 0 dB line, red past 0 dB.
void paintPeakMeter (juce::Graphics&, float x, float mid, float width, float peak, float& hold);

// The datamatics look for JUCE's own widgets (menus, dialogs).
juce::LookAndFeel& sharedLookAndFeel();

// The effects rack's controls, the same on every track, bus and the studio
// master, in four columns: CRUSH > DISTORT > FILTER, DELAY > REVERB,
// GRAIN DELAY, CHORUS + PSYCHOSIS.
class FxRackControls
{
public:
    static constexpr int kColumns = 4;
    static const char* groupName (int column);

    // shown() is the rack on screen: time readouts depend on their sync switch.
    void attach (juce::Component& parent, std::function<ms::FxParams()> shown,
                 std::function<void (int index, float value)> onChange);
    void setValues (const ms::FxParams&);
    // columnX(c) is the left edge of rack column c (0..3).
    void layout (const std::function<int (int)>& columnX, int y0, int rowHeight, int columnWidth);

private:
    std::array<DataSlider, ms::kNumFxParams> sliders;
};
