#pragma once

#include "engine/OfflineRenderer.h"

#include <juce_audio_processors/juce_audio_processors.h>

#include <atomic>
#include <optional>

// Runs the studio engine on the audio thread. The UI never touches the engine
// directly: it sends Commands through a lock-free queue, keeps its own copy of
// the studio, and reads meters back through atomics.
class ModularSynthProcessor : public juce::AudioProcessor
{
public:
    static constexpr int kHistoryLength = 512;  // barcode history per track
    static constexpr int kHistoryWindow = 256;  // samples per history entry
    static constexpr int kScopeLength = 4096;   // raw samples kept per track

    ModularSynthProcessor();
    ~ModularSynthProcessor() override = default;

    // ---- message thread ----------------------------------------------------
    void send (const ms::Command&);
    const ms::StudioState& getStudioState() const { return uiState; }
    // The drum machine's part of the studio.
    const ms::ProjectState& getUiState() const { return uiState.drums; }
    void replaceStudio (const ms::StudioState&);

    // Where the tool windows sit and which are open. Saved with the project;
    // the number changes whenever a newly opened project brings its own.
    juce::var getWindowLayout() const { return windowLayout; }
    void setWindowLayout (const juce::var& v) { windowLayout = v; }
    int getWindowLayoutVersion() const { return windowLayoutVersion; }

    // Loads a WAV/AIFF/FLAC as mono. The sample stays alive for the session.
    const ms::SampleData* loadSample (const juce::File&, juce::String& error);

    bool saveProject (const juce::File&);
    bool openProject (const juce::File&, juce::String& error);

    // Name used for a track on screen and in stem file names.
    juce::String trackName (int track) const;
    // The same for any tool's track: "KICK", "ACID".
    juce::String trackName (int tool, int track) const;

    // Returns a recording once Record has been stopped.
    std::optional<ms::Take> takeFinishedRecording();
    const std::optional<ms::Take>& getLastTake() const { return lastTake; }

    double getEngineSampleRate() const { return sampleRateForUi.load(); }

    // ---- read-outs (any thread) -------------------------------------------
    bool isPlaying() const { return playingFlag.load(); }
    bool isRecording() const { return recordingFlag.load(); }
    int currentStep() const { return stepIndex.load(); }
    int currentPattern (int tool = ms::kDrumTool) const { return currentPatterns[(size_t) tool].load(); }
    int queuedPattern (int tool = ms::kDrumTool) const { return queuedPatterns[(size_t) tool].load(); }
    // Call from the message thread regularly (the editor's timer does).
    void syncPatternFromEngine();
    std::int64_t recordedSamples() const { return recordedSampleCount.load(); }
    std::int64_t playedSamples() const { return playedSampleCount.load(); }
    int recordedEvents() const { return recordedEventCount.load(); }
    float masterPeak() const { return masterPeakValue.load(); }
    // A tool's own bus (drum bus, bass bus), before the studio master.
    float busPeak (int tool) const { return busPeaks[(size_t) tool].load(); }
    float toolPeak (int tool) const { return toolPeaks[(size_t) tool].load(); }
    // history entry age 0 = newest, like trackHistory
    float toolHistory (int tool, int age) const;

    // A tool's tracks (drums, bass rows) are its lanes: up to kLanesPerTool each.
    static constexpr int kLanesPerTool = 8;
    float trackPeak (int tool, int t) const { return trackPeaks[(size_t) lane (tool, t)].load(); }
    // history entry age 0 = newest
    float trackHistory (int tool, int t, int age) const;
    int historyWritePosition() const { return historyPos.load(); }
    // A track's raw output. Copies the newest `count` samples
    // (count <= kScopeLength) into dest, oldest first.
    void copyTrackScope (int tool, int t, float* dest, int count) const;
    // Increments every time the track is hit, so the UI can flash it.
    int trackHitCount (int tool, int t) const { return hitCounts[(size_t) lane (tool, t)].load(); }
    // A bass row's sounding (or last) MIDI note.
    float trackNote (int tool, int t) const { return trackNotes[(size_t) lane (tool, t)].load(); }

    // ---- juce::AudioProcessor ---------------------------------------------
    void prepareToPlay (double sampleRate, int samplesPerBlock) override;
    void releaseResources() override {}
    bool isBusesLayoutSupported (const BusesLayout&) const override;
    void processBlock (juce::AudioBuffer<float>&, juce::MidiBuffer&) override;
    using AudioProcessor::processBlock;

    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override { return true; }

    const juce::String getName() const override { return "Modular-synth"; }
    bool acceptsMidi() const override { return true; }
    bool producesMidi() const override { return false; }
    bool isMidiEffect() const override { return false; }
    double getTailLengthSeconds() const override { return 2.0; }

    int getNumPrograms() override { return 1; }
    int getCurrentProgram() override { return 0; }
    void setCurrentProgram (int) override {}
    const juce::String getProgramName (int) override { return {}; }
    void changeProgramName (int, const juce::String&) override {}

    void getStateInformation (juce::MemoryBlock&) override;
    void setStateInformation (const void*, int) override;

private:
    void handleOnAudioThread (const ms::Command&);
    void finishRecording();
    void recordEvent (const ms::Command&);
    void loadSamplePaths (ms::StudioState&, const std::array<juce::String, ms::kNumTracks>&, juce::String& error);

    ms::StudioEngine engine;

    // UI -> audio queue
    juce::AbstractFifo fifo { 32768 };
    std::vector<ms::Command> fifoBuffer = std::vector<ms::Command> (32768);

    // message-thread copy of the studio
    ms::StudioState uiState = ms::makeDefaultStudio();
    juce::var windowLayout;
    int windowLayoutVersion = 0;
    std::vector<std::unique_ptr<ms::SampleData>> samples;

    // recording: two takes so a new recording never overwrites one the UI is
    // still collecting
    std::array<ms::Take, 2> takes;
    int writeTake = 0;
    bool recording = false; // audio thread
    std::int64_t recordPosition = 0;
    std::atomic<int> finishedTake { -1 };
    std::optional<ms::Take> lastTake;

    // read-outs
    static constexpr int kLanes = ms::kNumToolSlots * kLanesPerTool;
    static int lane (int tool, int t) { return tool * kLanesPerTool + t; }
    std::atomic<bool> playingFlag { false }, recordingFlag { false };
    std::atomic<int> stepIndex { 0 }, recordedEventCount { 0 };
    std::array<std::atomic<int>, ms::kNumToolSlots> currentPatterns {}, queuedPatterns {};
    std::atomic<std::int64_t> recordedSampleCount { 0 }, playedSampleCount { 0 };
    std::atomic<float> masterPeakValue { 0.0f };
    std::array<std::atomic<float>, ms::kNumToolSlots> toolPeaks {}, busPeaks {};
    std::vector<std::atomic<float>> toolHistoryValues = std::vector<std::atomic<float>> (ms::kNumToolSlots * kHistoryLength);
    std::array<float, ms::kNumToolSlots> toolWindowPeak {}, busBlockPeak {};
    std::atomic<double> sampleRateForUi { 48000.0 };
    std::array<std::atomic<float>, kLanes> trackPeaks {}, trackNotes {};
    std::array<std::atomic<int>, kLanes> hitCounts {};
    std::vector<std::atomic<float>> history = std::vector<std::atomic<float>> (kLanes * kHistoryLength);
    std::atomic<int> historyPos { 0 };
    std::vector<std::atomic<float>> scopeSamples = std::vector<std::atomic<float>> (kLanes * kScopeLength);
    std::atomic<int> scopePos { 0 };
    std::array<float, kLanes> windowPeak {};
    int windowFill = 0;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (ModularSynthProcessor)
};
