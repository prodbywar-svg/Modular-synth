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
    void hitPad (int track, float velocity);

    ModularSynthProcessor& proc;
    int selected = 0;

    DataButton playButton { "PLAY" }, stopButton { "STOP" }, recButton { "REC" };
    DataButton exportButton { "EXPORT" }, saveButton { "SAVE" }, openButton { "OPEN" };
    std::array<DataButton, ms::kNumPatterns> patternButtons { DataButton { "P1" }, DataButton { "P2" }, DataButton { "P3" }, DataButton { "P4" } };
    DataReadout bpmReadout, swingReadout, stepReadout, posReadout, rateReadout, takeReadout;

    std::unique_ptr<TrackGrid> grid;
    std::unique_ptr<ParamPanel> panel;

    std::array<bool, ms::kNumTracks> padKeyHeld {};
    std::array<juce::uint32, ms::kNumTracks> lastPadKeyTime {};
    bool keyboardFocusRequested = false;

    juce::String status;
    juce::uint32 statusTime = 0;

    std::unique_ptr<juce::FileChooser> chooser;
    juce::File lastFolder;

    // export runs on a background thread
    std::atomic<float> exportProgress { -1.0f };
    std::unique_ptr<juce::Thread> exportThread;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (ModularSynthEditor)
};
