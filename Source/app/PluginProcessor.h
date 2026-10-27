#pragma once

#include "engine/OfflineRenderer.h"

#include <juce_audio_processors/juce_audio_processors.h>

#include <atomic>
#include <optional>

// Runs the engine on the audio thread. The UI never touches the engine
// directly: it sends Commands through a lock-free queue, keeps its own copy of
// the project, and reads meters back through atomics.
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
    const ms::ProjectState& getUiState() const { return uiState; }
    void replaceProject (const ms::ProjectState&);

    // Loads a WAV/AIFF/FLAC as mono. The sample stays alive for the session.
    const ms::SampleData* loadSample (const juce::File&, juce::String& error);

    bool saveProject (const juce::File&);
    bool openProject (const juce::File&, juce::String& error);

    // Name used for a track on screen and in stem file names.
    juce::String trackName (int track) const;

    // Returns a recording once Record has been stopped.
    std::optional<ms::Take> takeFinishedRecording();
    const std::optional<ms::Take>& getLastTake() const { return lastTake; }

    double getEngineSampleRate() const { return sampleRateForUi.load(); }

    // ---- read-outs (any thread) -------------------------------------------
    bool isPlaying() const { return playingFlag.load(); }
    bool isRecording() const { return recordingFlag.load(); }
    int currentStep() const { return stepIndex.load(); }
    int currentPattern() const { return currentPatternIndex.load(); }
    int queuedPattern() const { return queuedPatternIndex.load(); }
    // Call from the message thread regularly (the editor's timer does).
    void syncPatternFromEngine();
    std::int64_t recordedSamples() const { return recordedSampleCount.load(); }
    std::int64_t playedSamples() const { return playedSampleCount.load(); }
    int recordedEvents() const { return recordedEventCount.load(); }
    float masterPeak() const { return masterPeakValue.load(); }
    float trackPeak (int t) const { return trackPeaks[(size_t) t].load(); }
    // history entry age 0 = newest
    float trackHistory (int t, int age) const;
    int historyWritePosition() const { return historyPos.load(); }
    // A track's raw output, age 0 = newest sample. Copies the newest `count`
    // samples (count <= kScopeLength) into dest, oldest first.
    void copyTrackScope (int t, float* dest, int count) const;
    // Increments every time the track is hit, so the UI can flash it.
    int trackHitCount (int t) const { return hitCounts[(size_t) t].load(); }

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

    ms::EngineCore engine;

    // UI -> audio queue
    juce::AbstractFifo fifo { 8192 };
    std::vector<ms::Command> fifoBuffer = std::vector<ms::Command> (8192);

    // message-thread copy of the project
    ms::ProjectState uiState = ms::makeDefaultProject();
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
    std::atomic<bool> playingFlag { false }, recordingFlag { false };
    std::atomic<int> stepIndex { 0 }, recordedEventCount { 0 };
    std::atomic<int> currentPatternIndex { 0 }, queuedPatternIndex { -1 };
    std::atomic<std::int64_t> recordedSampleCount { 0 }, playedSampleCount { 0 };
    std::atomic<float> masterPeakValue { 0.0f };
    std::atomic<double> sampleRateForUi { 48000.0 };
    std::array<std::atomic<float>, ms::kNumTracks> trackPeaks {};
    std::array<std::atomic<int>, ms::kNumTracks> hitCounts {};
    std::vector<std::atomic<float>> history = std::vector<std::atomic<float>> (ms::kNumTracks * kHistoryLength);
    std::atomic<int> historyPos { 0 };
    std::vector<std::atomic<float>> scopeSamples = std::vector<std::atomic<float>> (ms::kNumTracks * kScopeLength);
    std::atomic<int> scopePos { 0 };
    std::array<float, ms::kNumTracks> windowPeak {};
    int windowFill = 0;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (ModularSynthProcessor)
};
