#pragma once

#include "PluginProcessor.h"
#include "Shared.h"

class BassGrid;
class BassPanel;

// A bass synth's window: 8 rows (one sound each) x 16 steps, every lit step
// a note in the key and scale, patch cables that join rows into cut groups,
// the selected row's controls and the bass bus. Keys this view does not use
// are handed back to the studio window.
class BassSynthView : public juce::Component
{
public:
    BassSynthView (ModularSynthProcessor&, int tool);
    ~BassSynthView() override;

    std::function<bool (const juce::KeyPress&)> onUnhandledKey;

    // Called by the studio window's timer.
    void tick();

    void paint (juce::Graphics&) override;
    void resized() override;
    bool keyPressed (const juce::KeyPress&) override;
    bool keyStateChanged (bool isKeyDown) override;

    // shared with the child components
    ModularSynthProcessor& processor() { return proc; }
    int toolSlot() const { return tool; }
    const ms::BassState& state() const;
    void send (ms::CommandType type, int track, int index, float value);
    void selectRow (int row); // -1 = the bass bus
    int selectedRow() const { return selected; }
    void showSoundMenu (int row);
    void setStatus (const juce::String&);

    // Plays a note on a row now (snapped to the scale), until noteOff.
    void auditionNote (int row, int note, float velocity = 0.9f);
    void releaseNote (int row);

    // The note a newly lit step starts with: the row's last note before it.
    int noteForNewStep (int row, int step) const;

    void copySequence (bool wholePattern);
    void pasteSequence();
    void randomizeSequence (int row);

private:
    void selectPattern (int pattern);
    void copyPatternTo (int pattern);
    void clearPattern();     // empties the pattern on screen, resets all effects
    void resetToDefaults();  // sounds, controls, levels, effects back to the start
    void showKeyMenu();
    void showScaleMenu();
    void undo();

    // Sends whatever differs from target, sequences included or not.
    void applyBass (const ms::BassState& target, bool sequences);
    void remember(); // the state before a clear / paste / random / reset

    // Writes one row's pattern: velocity, notes and lengths.
    struct RowSequence
    {
        ms::Pattern velocity {};
        ms::NotePattern notes {};
        ms::LengthPattern lengths {};
    };
    RowSequence rowSequence (int row, int pattern) const;
    void writeRowSequence (int row, int pattern, const RowSequence&);

    void playKey (int semitone);
    bool isCompact() const;

    ModularSynthProcessor& proc;
    const int tool;
    int selected = 0;

    DataButton clearButton { "CLEAR" }, resetButton { "RESET" }, keyButton { "KEY" }, scaleButton { "SCALE" }, autoButton { "AUTO" };
    std::array<DataButton, ms::kNumPatterns> patternButtons { DataButton { "P1" }, DataButton { "P2" }, DataButton { "P3" }, DataButton { "P4" } };
    DataReadout stepReadout, octaveReadout, busReadout;

    std::unique_ptr<BassGrid> grid;
    std::unique_ptr<BassPanel> panel;

    // computer keyboard piano: the key whose note is sounding
    int octave = 2; // the Z key plays C of this octave (C2 = MIDI 36)
    int heldKey = -1, heldRow = -1;

    std::optional<ms::BassState> lastUndo;
    juce::String undoText;

    struct Clipboard
    {
        bool wholePattern = false;
        std::array<RowSequence, ms::kBassRows> rows {}; // [0] holds a single row
        juce::String source;
    };
    std::optional<Clipboard> clipboard;

    juce::String status;
    juce::uint32 statusTime = 0;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (BassSynthView)
};
