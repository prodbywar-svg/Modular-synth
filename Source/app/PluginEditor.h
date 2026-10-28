#pragma once

#include "DrumMachineView.h"

class ToolStrip;
class MasterPanel;
class ToolWindow;

// The studio window: what every tool shares (play, tempo, record, export,
// save / open, the studio master rack) and the list of tools. Each tool opens
// in its own window, which can be closed and reopened, or the tool removed.
class ModularSynthEditor : public juce::AudioProcessorEditor, private juce::Timer
{
public:
    explicit ModularSynthEditor (ModularSynthProcessor&);
    ~ModularSynthEditor() override;

    void paint (juce::Graphics&) override;
    void resized() override;
    bool keyPressed (const juce::KeyPress&) override;

    ModularSynthProcessor& processor() { return proc; }
    void send (ms::CommandType type, int tool, int track, int index, float value);
    void setStatus (const juce::String&);

    // tool windows
    bool isToolWindowShowing (int tool) const;
    void showToolWindow (int tool, bool show);
    void addTool (int tool);
    void removeTool (int tool); // asks first

    // Keys every window shares: Space, Esc, R, E, Ctrl+S, Ctrl+O.
    bool handleStudioKey (const juce::KeyPress&);

private:
    void timerCallback() override;

    void togglePlay();
    void toggleRecord();
    void stop();
    void chooseExport();
    void startExport (ms::Take take, const juce::String& baseName);
    void saveProject();
    void openProject();

    void createToolWindow (int tool, juce::Rectangle<int> bounds);
    juce::Rectangle<int> defaultToolBounds (int tool) const;
    void storeWindowLayout();
    void applyWindowLayout();

    ModularSynthProcessor& proc;

    DataButton playButton { "PLAY" }, stopButton { "STOP" }, recButton { "REC" };
    DataButton exportButton { "EXPORT" }, saveButton { "SAVE" }, openButton { "OPEN" };
    DataReadout bpmReadout, swingReadout, stepReadout, posReadout, rateReadout, takeReadout;

    std::array<std::unique_ptr<ToolStrip>, ms::kNumToolSlots> strips;
    std::unique_ptr<MasterPanel> master;
    std::array<std::unique_ptr<ToolWindow>, ms::kNumToolSlots> windows;

    bool firstShow = true;
    int appliedLayoutVersion = 0;

    juce::String status;
    juce::uint32 statusTime = 0;

    std::unique_ptr<juce::FileChooser> chooser;
    juce::File lastFolder;

    // export runs on a background thread
    std::atomic<float> exportProgress { -1.0f };
    std::unique_ptr<juce::Thread> exportThread;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (ModularSynthEditor)
};
