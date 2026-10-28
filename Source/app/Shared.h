#pragma once

#include "Widgets.h"

#include "engine/ProjectState.h"

// Pieces shared by the studio window and the tool windows.

juce::String formatTime (double seconds);
juce::String formatDb (float gain);
juce::String formatPan (float v);

// A horizontal peak meter centred on y = mid: a bar, a falling peak-hold mark
// (updated in hold), a red 0 dB line, red past 0 dB.
void paintPeakMeter (juce::Graphics&, float x, float mid, float width, float peak, float& hold);

// Sends what it takes to make a rack's automation on toPattern a copy of
// `from` on fromPattern (send: type, rack, index, value).
void copyFxAutomation (const std::function<void (ms::CommandType, int, int, float)>& send, int rack,
                       const ms::FxAutomation& from, int fromPattern, int toPattern);

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

    // Effect automation for the rack on screen. While recording() is true
    // (AUTO armed and playing), moving a control writes it into the step
    // playing; holding it writes every step it passes. The recorded moves show
    // on the controls, and right-click clears a control's moves.
    struct Automation
    {
        std::function<const ms::FxAutomation*()> moves; // the rack on screen
        std::function<int()> pattern;                    // the pattern they are for
        std::function<bool()> recording;
        std::function<int()> step;                       // the step playing
        std::function<double()> position;                // in steps, -1 when stopped
        std::function<void (ms::CommandType, int index, float value)> send; // to the rack on screen
    };
    void setAutomation (Automation);
    // Call regularly (the window timers do) with the rack's set values.
    void tickAutomation (const ms::FxParams& set);
    // columnX(c) is the left edge of rack column c (0..3).
    void layout (const std::function<int (int)>& columnX, int y0, int rowHeight, int columnWidth);

private:
    void record (int index, float value);

    std::array<DataSlider, ms::kNumFxParams> sliders;
    Automation automation;
    std::array<bool, ms::kNumFxParams> held {};
    std::array<int, ms::kNumFxParams> lastStep {};
};
