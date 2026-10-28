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
    ~ModularSynthProcessor() override;

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

    // ---- loopers (message thread) -------------------------------------------
    // Starts recording into a looper's slot from the next bar (with the
    // transport stopped, from when PLAY is pressed): a new loop of the slot's
    // bars, or with overdub on top of the one there.
    void recordLoop (int tool, int slot, bool overdub);
    void cancelLoopRecord (int tool);
    // Puts the slot's audio back to before its last recording or change.
    // Returns false when there is nothing to go back to.
    bool undoLoopLayer (int tool, int slot);
    int loopLayers (int tool, int slot) const;
    // Sets a slot's audio (null empties it), remembering the old for undo.
    void setLoopAudio (int tool, int slot, const ms::LoopAudio*);
    // Keeps a loop alive while the caller holds it (undo, the clipboard).
    std::shared_ptr<const ms::LoopAudio> holdLoop (const ms::LoopAudio*) const;
    // Keeps what a take plays (loops, its input) alive while it is around.
    void holdTake (ms::Take&) const;
    // How late the input arrives after the sound it was played to, in
    // samples: the recordings move back by this much.
    void setInputLatency (int numSamples) { inputLatency = juce::jmax (0, numSamples); }
    int getInputLatency() const { return inputLatency.load(); }
    bool hasAudioInput() const { return getTotalNumInputChannels() > 0; }

    struct LooperReadout
    {
        ms::LooperEngine::Status status = ms::LooperEngine::Status::Idle;
        int slot = 0;
        bool overdub = false;
        double progress = 0.0; // of the recording, 0..1
        int countBeats = 0;     // beats of count-in left, while counting in
        double position = -1.0; // of the loop playing, 0..1, -1 = none
    };
    LooperReadout looperReadout (int tool) const;

    // Returns a recording once Record has been stopped.
    std::optional<ms::Take> takeFinishedRecording();
    const std::optional<ms::Take>& getLastTake() const { return lastTake; }

    double getEngineSampleRate() const { return sampleRateForUi.load(); }

    // ---- read-outs (any thread) -------------------------------------------
    bool isPlaying() const { return playingFlag.load(); }
    bool isRecording() const { return recordingFlag.load(); }
    int currentStep() const { return stepIndex.load(); }
    // Where the clock is in the bar, in steps (0 .. 16).
    double stepPosition() const { return stepPositionValue.load(); }

    // AUTO: while armed and playing, effect moves are recorded into the steps.
    bool isAutoArmed() const { return autoArmed; }
    void setAutoArmed (bool b) { autoArmed = b; }
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

    // loops: owned here, shared by pointer with the audio thread and takes
    ms::LoopAudio* newLoop (int bars, const ms::LoopAudio* copyOf);
    void pushLayer (int looper, int slot, const ms::LoopAudio* previous);
    void collectLoops();
    void topUpInputChunks();
    bool anyLooper() const;
    juce::var studioVarWithLoops (const juce::File& folder, bool relative);
    void loadLoops (ms::StudioState&, const std::array<ms::LoopFiles, ms::kMaxLoopers>&, const juce::File& base, juce::String& error);

    ms::StudioEngine engine;

    // UI -> audio queue
    juce::AbstractFifo fifo { 32768 };
    std::vector<ms::Command> fifoBuffer = std::vector<ms::Command> (32768);

    // message-thread copy of the studio
    ms::StudioState uiState = ms::makeDefaultStudio();
    juce::var windowLayout;
    int windowLayoutVersion = 0;
    std::vector<std::unique_ptr<ms::SampleData>> samples;

    struct HeldLoop
    {
        std::shared_ptr<ms::LoopAudio> audio;
        juce::uint32 lastUsed = 0;
    };
    std::vector<HeldLoop> loops;
    juce::uint32 lastCollect = 0;
    static constexpr size_t kMaxLayers = 16;
    std::array<std::array<std::vector<std::shared_ptr<const ms::LoopAudio>>, ms::kNumPatterns>, ms::kMaxLoopers> layers;
    std::atomic<int> inputLatency { 0 };

    // finished loop recordings, audio -> message thread
    struct FinishedLoop
    {
        int looper = 0, slot = 0;
        ms::LoopAudio* audio = nullptr;
    };
    juce::AbstractFifo finishedFifo { 64 };
    std::array<FinishedLoop, 64> finishedLoops {};
    std::array<int, ms::kMaxLoopers> finishedSeen {}; // audio thread

    // the input of takes: chunks made here, used up by the audio thread
    juce::AbstractFifo chunkFifo { 32 };
    std::array<float*, 32> chunkSlots {};
    float* takeChunk(); // audio thread
    std::shared_ptr<ms::InputRecording> pendingInput;
    std::atomic<ms::InputRecording*> nextInput { nullptr };
    std::vector<std::shared_ptr<ms::InputRecording>> inputs; // ones the audio thread has had
    ms::InputRecording* takeInput = nullptr;                  // audio thread: the take's, while recording
    std::atomic<const ms::InputRecording*> recordingInput { nullptr };

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
    std::atomic<double> stepPositionValue { 0.0 };
    bool autoArmed = false; // message thread
    std::array<std::atomic<int>, ms::kNumToolSlots> currentPatterns {}, queuedPatterns {};
    std::array<std::atomic<int>, ms::kMaxLoopers> looperStatus {}, looperSlot {}, looperCount {};
    std::array<std::atomic<bool>, ms::kMaxLoopers> looperOverdub {};
    std::array<std::atomic<double>, ms::kMaxLoopers> looperProgress {}, looperPosition {};
    std::array<std::atomic<const ms::LoopAudio*>, ms::kMaxLoopers> recordingLoops {};
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
