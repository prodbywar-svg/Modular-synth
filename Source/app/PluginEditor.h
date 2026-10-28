#pragma once

#include "PluginProcessor.h"
#include "Widgets.h"

class TrackGrid;
class ParamPanel;

class ModularSynthEditor : public juce::AudioProcessorEditor, private juce::Timer
{
public:
    explicit ModularSynthEditor (ModularSynthProcessor&);
    ~ModularSynthEditor() override;

    void paint (juce::Graphics&) override;
    void resized() override;
    bool keyPressed (const juce::KeyPress&) override;
    bool keyStateChanged (bool isKeyDown) override;

    // shared with the child components
    ModularSynthProcessor& processor() { return proc; }
    void send (ms::CommandType type, int track, int index, float value, const ms::SampleData* sample = nullptr);
    void selectTrack (int track); // -1 = master
    int selectedTrack() const { return selected; }
    void showVoiceMenu (int track);
    void setStatus (const juce::String&);

    // Sequence clipboard: one drum's steps, or every drum in the pattern on
    // screen. Paste writes whichever was copied into the pattern on screen.
    void copySequence (bool wholePattern);
    void pasteSequence();
    void randomizeSequence (int track);

private:
    void timerCallback() override;

    void togglePlay();
    void toggleRecord();
    void stop();
    void chooseExport();
    void startExport (ms::Take take, const juce::String& baseName);
    void saveProject();
    void openProject();
    void loadSampleInto (int track);
    void selectPattern (int pattern);
    void copyPatternTo (int pattern); // copies the pattern on screen
    void clearPattern();              // empties the pattern on screen, resets all effects
    void resetToDefaults();           // every sound, effect, level and the tempo back to defaults
    void applySettings (const ms::ProjectState& target); // all but the sequences and loaded samples
    void undo();                      // takes back the last clear, paste, random or reset

    void hitPad (int track, float velocity);

    ModularSynthProcessor& proc;
    int selected = 0;

    DataButton playButton { "PLAY" }, stopButton { "STOP" }, recButton { "REC" };
    DataButton exportButton { "EXPORT" }, saveButton { "SAVE" }, openButton { "OPEN" }, clearButton { "CLEAR" }, resetButton { "RESET" };
    std::array<DataButton, ms::kNumPatterns> patternButtons { DataButton { "P1" }, DataButton { "P2" }, DataButton { "P3" }, DataButton { "P4" } };
    DataReadout bpmReadout, swingReadout, stepReadout, posReadout, rateReadout, takeReadout;

    std::unique_ptr<TrackGrid> grid;
    std::unique_ptr<ParamPanel> panel;

    std::array<bool, ms::kNumTracks> padKeyHeld {};
    std::array<juce::uint32, ms::kNumTracks> lastPadKeyTime {};
    bool keyboardFocusRequested = false;

    // What a clear / paste / random / reset replaced, so Ctrl+Z can put it back.
    struct UndoSnapshot
    {
        int pattern = 0;
        std::array<ms::Pattern, ms::kNumTracks> steps {};
        bool hasFx = false;
        std::array<ms::FxParams, ms::kNumTracks> fx {};
        ms::FxParams masterFx {};
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

    // export runs on a background thread
    std::atomic<float> exportProgress { -1.0f };
    std::unique_ptr<juce::Thread> exportThread;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (ModularSynthEditor)
};
