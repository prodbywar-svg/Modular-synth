#pragma once

#include "PluginProcessor.h"
#include "Shared.h"

class VocalGrid;
class VocalPanel;

// A vocal track's window: the input, the recording along the song (a
// barcode of its samples on a numbered bar ruler), and the track's rack.
// Keys this view does not use are handed back to the studio window.
class VocalView : public juce::Component
{
public:
    VocalView (ModularSynthProcessor&, int tool);
    ~VocalView() override;

    std::function<bool (const juce::KeyPress&)> onUnhandledKey;

    // Called by the studio window's timer.
    void tick();

    void paint (juce::Graphics&) override;
    void resized() override;
    bool keyPressed (const juce::KeyPress&) override;

    // shared with the child components
    ModularSynthProcessor& processor() { return proc; }
    int toolSlot() const { return tool; }
    const ms::VocalState& state() const;
    void send (ms::CommandType type, int track, int index, float value);
    void setStatus (const juce::String&);

    void toggleRecord();
    void toggleMonitor();
    void toggleMonitorDry();
    void showCountInMenu();
    void showClickMenu();
    void setClick (int mode);
    void showInputMenu();

    // The input's level while recording, one value per history window from
    // where the recording started (drawn until the recording is laid down).
    const std::vector<float>& liveRecording() const { return livePeaks; }

private:
    void clearTake();        // empties the recording, resets the rack
    void resetToDefaults();  // monitor, gain, level, pan and rack back to the start
    void undo();

    struct Snapshot
    {
        ms::VocalState state;
        std::shared_ptr<const ms::VocalClip> held;
    };
    Snapshot snapshot() const;
    // Sends whatever differs from target, the recording included or not.
    void applyVocal (const Snapshot& target, bool clip);
    void remember(); // the state before a clear / reset

    bool isCompact() const;
    juce::String barText (std::int64_t samples) const;

    ModularSynthProcessor& proc;
    const int tool;

    DataButton recButton { "REC VOCAL" }, countButton { "COUNT 1" }, clickButton { "CLICK" }, inputButton { "IN 1" };
    DataButton monitorButton { "MON" }, dryButton { "FX" }, clearButton { "CLEAR" }, resetButton { "RESET" }, autoButton { "AUTO" };
    DataReadout positionReadout, takeReadout, latencyReadout;

    std::unique_ptr<VocalGrid> grid;
    std::unique_ptr<VocalPanel> panel;

    std::optional<Snapshot> lastUndo;
    juce::String undoText;
    int seenLayers = 0;

    std::vector<float> livePeaks;
    int lastHistoryPos = 0;
    bool wasRecording = false;

    juce::String status;
    juce::uint32 statusTime = 0;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (VocalView)
};
