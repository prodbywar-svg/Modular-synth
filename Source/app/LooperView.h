#pragma once

#include "PluginProcessor.h"
#include "Shared.h"

class LooperGrid;
class LooperPanel;

// A looper's window: the input, the loop playing (the whole loop as a
// barcode of its samples), the 16 slice steps, and the looper's rack. Keys
// this view does not use are handed back to the studio window.
class LooperView : public juce::Component
{
public:
    LooperView (ModularSynthProcessor&, int tool);
    ~LooperView() override;

    std::function<bool (const juce::KeyPress&)> onUnhandledKey;

    // Called by the studio window's timer.
    void tick();

    void paint (juce::Graphics&) override;
    void resized() override;
    bool keyPressed (const juce::KeyPress&) override;

    // shared with the child components
    ModularSynthProcessor& processor() { return proc; }
    int toolSlot() const { return tool; }
    const ms::LooperState& state() const;
    void send (ms::CommandType type, int track, int index, float value);
    void setStatus (const juce::String&);

    void recordLoop (bool overdub);
    void copySlot();
    void pasteSlot();
    void randomizeSlices();
    void straightenSlices();
    void toggleMonitor();
    void toggleMonitorDry();
    void showCountInMenu();
    void showInputMenu();
    void showBarsMenu();

private:
    void selectPattern (int pattern);
    void copySlotTo (int pattern);
    void clearSlot();       // empties the loop on screen, resets the rack
    void resetToDefaults(); // input, level, pan and rack back to the start
    void undo();

    // What a slot holds: the loop (kept alive here), its bars and slices.
    struct SlotCopy
    {
        std::shared_ptr<const ms::LoopAudio> audio;
        int bars = 2;
        ms::SlicePattern slices = ms::LoopSlot::straightSlices();
    };
    SlotCopy slotCopy (int slot) const;
    void writeSlot (int slot, const SlotCopy&);

    // Sends whatever differs from target, the slots included or not.
    struct Snapshot
    {
        ms::LooperState state;
        std::array<std::shared_ptr<const ms::LoopAudio>, ms::kNumPatterns> held;
    };
    Snapshot snapshot() const;
    void applyLooper (const Snapshot& target, bool slots);
    void remember(); // the state before a clear / paste / random / reset

    bool isCompact() const;

    ModularSynthProcessor& proc;
    const int tool;

    DataButton recButton { "REC LOOP" }, overdubButton { "OVERDUB" }, barsButton { "2 BARS" }, inputButton { "IN 1+2" };
    DataButton countButton { "COUNT 1" }, dryButton { "FX" };
    DataButton monitorButton { "MON" }, clearButton { "CLEAR" }, resetButton { "RESET" }, autoButton { "AUTO" };
    std::array<DataButton, ms::kNumPatterns> patternButtons { DataButton { "P1" }, DataButton { "P2" }, DataButton { "P3" }, DataButton { "P4" } };
    DataReadout stepReadout, loopReadout, latencyReadout;

    std::unique_ptr<LooperGrid> grid;
    std::unique_ptr<LooperPanel> panel;

    std::optional<Snapshot> lastUndo;
    juce::String undoText;
    std::array<int, ms::kNumPatterns> seenLayers {};
    std::optional<SlotCopy> clipboard;
    juce::String clipboardSource;

    juce::String status;
    juce::uint32 statusTime = 0;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (LooperView)
};
