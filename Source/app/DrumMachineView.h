#pragma once

#include "PluginProcessor.h"
#include "Shared.h"

class TrackGrid;
class ParamPanel;

// The drum machine's window: 8 tracks x 16 steps, patterns, the selected
// track's controls and the drum bus. Play, record, export and saving live in
// the studio window; keys this view does not use are handed back to it.
class DrumMachineView : public juce::Component
{
public:
    explicit DrumMachineView (ModularSynthProcessor&);
    ~DrumMachineView() override;

    // Keys this view does not handle (Space, R, E, Ctrl+S...) go here.
    std::function<bool (const juce::KeyPress&)> onUnhandledKey;

    // Called by the studio window's timer.
    void tick();

    void paint (juce::Graphics&) override;
    void resized() override;
    bool keyPressed (const juce::KeyPress&) override;
    bool keyStateChanged (bool isKeyDown) override;

    // shared with the child components
    ModularSynthProcessor& processor() { return proc; }
    void send (ms::CommandType type, int track, int index, float value, const ms::SampleData* sample = nullptr);
    void selectTrack (int track); // -1 = the drum bus
    int selectedTrack() const { return selected; }
    void showVoiceMenu (int track);
    void setStatus (const juce::String&);

    // Sequence clipboard: one drum's steps, or every drum in the pattern on
    // screen. Paste writes whichever was copied into the pattern on screen.
    void copySequence (bool wholePattern);
    void pasteSequence();
    void randomizeSequence (int track);

private:
    void loadSampleInto (int track);
    void selectPattern (int pattern);
    void copyPatternTo (int pattern); // copies the pattern on screen
    void clearPattern();              // empties the pattern on screen, resets all effects
    void resetToDefaults();           // every sound, effect and level back to defaults
    void applySettings (const ms::ProjectState& target); // all but the sequences and loaded samples
    void undo();                      // takes back the last clear, paste, random or reset

    void hitPad (int track, float velocity);
    bool isCompact() const;

    ModularSynthProcessor& proc;
    int selected = 0;

    DataButton clearButton { "CLEAR" }, resetButton { "RESET" }, autoButton { "AUTO" };
    std::array<DataButton, ms::kNumPatterns> patternButtons { DataButton { "P1" }, DataButton { "P2" }, DataButton { "P3" }, DataButton { "P4" } };
    DataReadout stepReadout, busReadout;

    std::unique_ptr<TrackGrid> grid;
    std::unique_ptr<ParamPanel> panel;

    std::array<bool, ms::kNumTracks> padKeyHeld {};
    std::array<juce::uint32, ms::kNumTracks> lastPadKeyTime {};

    // What a clear / paste / random / reset replaced, so Ctrl+Z can put it back.
    struct UndoSnapshot
    {
        int pattern = 0;
        std::array<ms::Pattern, ms::kNumTracks> steps {};
        bool hasFx = false;
        std::array<ms::FxParams, ms::kNumTracks> fx {};
        ms::FxParams masterFx {};
        std::array<ms::FxAutomation, ms::kNumTracks> fxAuto {};
        ms::FxAutomation masterFxAuto {};
        bool hasSettings = false;  // a reset: every setting in `settings` comes back
        ms::ProjectState settings {};
    };
    UndoSnapshot takeSnapshot (int pattern, bool withFx) const;
    std::optional<UndoSnapshot> lastUndo;

    struct SequenceClipboard
    {
        bool wholePattern = false;
        std::array<ms::Pattern, ms::kNumTracks> steps {}; // [0] holds a single track
        juce::String source;
    };
    std::optional<SequenceClipboard> clipboard;

    juce::String status;
    juce::uint32 statusTime = 0;

    std::unique_ptr<juce::FileChooser> chooser;
    juce::File lastFolder;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (DrumMachineView)
};
