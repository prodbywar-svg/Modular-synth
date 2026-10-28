// The browser version's bridge to the engine. The same .wasm file runs twice:
// inside the AudioWorklet it makes the live sound (and records takes); on the
// page it keeps the UI's copy of the project, formats readouts and renders
// exports. Everything is plain C functions over numbers, so the JavaScript
// side needs no generated glue code.

#include "engine/EngineCore.h"
#include "engine/Hr16Bank.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <memory>
#include <vector>

#ifdef __wasm__
// No exceptions in this build: allocation failure simply stops the module.
void* operator new (size_t n) { if (void* p = std::malloc (n ? n : 1)) return p; std::abort(); }
void* operator new[] (size_t n) { return operator new (n); }
void operator delete (void* p) noexcept { std::free (p); }
void operator delete[] (void* p) noexcept { std::free (p); }
void operator delete (void* p, size_t) noexcept { std::free (p); }
void operator delete[] (void* p, size_t) noexcept { std::free (p); }

// The prebuilt libc++ can still reach a throw (e.g. std::string length
// errors); without exception support that simply stops the module.
extern "C" void* __cxa_allocate_exception (size_t) { std::abort(); }
extern "C" void __cxa_throw (void*, void*, void (*) (void*)) { std::abort(); }
#endif

#define MS_EXPORT extern "C" __attribute__ ((visibility ("default")))

using namespace ms;

namespace
{
constexpr int kMaxBlock = 4096;
constexpr size_t kMaxRecordedEvents = 200000;

// A recorded change, with the sample referenced by id so it can travel
// between the two .wasm instances.
struct WebEvent
{
    double time;
    int type, track, index;
    float value;
    int sampleId;
};

// A performance: the project when Record was pressed plus every change made
// while recording (Take in OfflineRenderer.h, which needs JUCE).
struct Take
{
    ProjectState start;
    std::vector<TimedCommand> events;
    std::int64_t length = 0;
    double sampleRate = 48000.0;
};

// Renders a take block by block, applying recorded events at their exact
// sample. Same as TakePlayer in OfflineRenderer.cpp.
struct Player
{
    EngineCore engine;
    size_t nextEvent = 0;
    std::int64_t position = 0;
};

struct Web
{
    EngineCore engine;
    ProjectState ui = makeDefaultProject(); // the page's copy
    std::vector<std::unique_ptr<SampleData>> samples; // id = index + 1

    std::vector<float> left = std::vector<float> (kMaxBlock), right = std::vector<float> (kMaxBlock);
    char text[64] {};

    // live recording (worklet)
    bool recording = false;
    std::int64_t recordPosition = 0;
    std::vector<WebEvent> recorded;
    std::vector<double> recordedFlat;

    // export (page)
    Take take, lastTake;
    std::unique_ptr<Player> player;
};

Web& web()
{
    static Web w;
    return w;
}

int idForSample (const SampleData* s)
{
    auto& w = web();
    for (size_t i = 0; i < w.samples.size(); ++i)
        if (w.samples[i].get() == s)
            return (int) i + 1;
    return 0;
}

const SampleData* sampleForId (int id)
{
    auto& w = web();
    return id >= 1 && id <= (int) w.samples.size() ? w.samples[(size_t) id - 1].get() : nullptr;
}

Command makeCommand (int type, int track, int index, float value, int sampleId)
{
    Command c;
    c.type = (CommandType) type;
    c.track = track;
    c.index = index;
    c.value = value;
    c.sample = sampleForId (sampleId);
    return c;
}

TrackState* trackAt (ProjectState& p, int t)
{
    return t >= 0 && t < kNumTracks ? &p.tracks[(size_t) t] : nullptr;
}

const char* copyText (const char* s)
{
    auto& w = web();
    std::snprintf (w.text, sizeof (w.text), "%s", s != nullptr ? s : "");
    return w.text;
}

void finishRecording()
{
    auto& w = web();
    w.recording = false;
}
} // namespace

// ---- memory ------------------------------------------------------------------

MS_EXPORT float* ms_alloc_floats (int count) { return new float[(size_t) std::max (1, count)]; }
MS_EXPORT void ms_free_floats (float* p) { delete[] p; }

// ---- samples -----------------------------------------------------------------

MS_EXPORT void ms_set_hr16 (int fileNumber, const float* data, int length, double sampleRate)
{
    setHr16File (fileNumber, data, length, sampleRate);
}

// Adds a user sample (mono) and returns its id. Both instances add samples in
// the same order, so ids match.
MS_EXPORT int ms_add_sample (const float* data, int length, double sampleRate)
{
    auto s = std::make_unique<SampleData>();
    s->samples.assign (data, data + std::max (0, length));
    s->sampleRate = sampleRate;
    web().samples.push_back (std::move (s));
    return (int) web().samples.size();
}

// ---- live engine (worklet) ---------------------------------------------------

MS_EXPORT void ms_prepare (double sampleRate)
{
    web().engine.prepare (sampleRate, kMaxBlock);
}

// Back to the starter project (before opening a saved one).
MS_EXPORT void ms_engine_load_default()
{
    auto& w = web();
    w.engine.setPlaying (false);
    w.engine.setState (makeDefaultProject());
    w.engine.reset();
}

MS_EXPORT float* ms_left() { return web().left.data(); }
MS_EXPORT float* ms_right() { return web().right.data(); }

// Every command from the page, transport included (mirrors the desktop
// processor's handleOnAudioThread).
MS_EXPORT void ms_command (int type, int track, int index, float value, int sampleId)
{
    auto& w = web();
    auto& engine = w.engine;
    const auto c = makeCommand (type, track, index, value, sampleId);

    switch (c.type)
    {
        case CommandType::Play:
            engine.setPlaying (true);
            break;
        case CommandType::Stop:
        case CommandType::RecordStop:
            finishRecording();
            engine.setPlaying (false);
            break;
        case CommandType::RecordStart:
            finishRecording();
            // Start from silence at step 1, exactly like the export will.
            engine.setPlaying (false);
            engine.reset();
            w.recorded.clear();
            w.recordPosition = 0;
            w.recording = true;
            engine.setPlaying (true);
            break;
        default:
            engine.apply (c);
            if (w.recording)
            {
                if (w.recorded.size() >= kMaxRecordedEvents)
                    finishRecording();
                else
                    w.recorded.push_back ({ (double) w.recordPosition, type, track, index, value, sampleId });
            }
            break;
    }
}

// Renders n samples (n <= 4096) into ms_left() / ms_right().
MS_EXPORT void ms_process (int n)
{
    auto& w = web();
    n = std::clamp (n, 0, kMaxBlock);
    w.engine.process (w.left.data(), w.right.data(), n);
    if (w.recording)
        w.recordPosition += n;
}

MS_EXPORT int ms_playing() { return web().engine.isPlaying() ? 1 : 0; }
MS_EXPORT int ms_recording() { return web().recording ? 1 : 0; }
MS_EXPORT double ms_record_position() { return (double) web().recordPosition; }
MS_EXPORT int ms_recorded_count() { return (int) web().recorded.size(); }
MS_EXPORT int ms_step() { return web().engine.currentStep(); }
MS_EXPORT int ms_pattern() { return web().engine.currentPattern(); }
MS_EXPORT int ms_queued_pattern() { return web().engine.queuedPattern(); }
MS_EXPORT float ms_track_peak (int t) { return t >= 0 && t < kNumTracks ? web().engine.trackPeak (t) : 0.0f; }
MS_EXPORT float ms_master_peak() { return web().engine.masterPeak(); }
MS_EXPORT const float* ms_track_output (int t) { return web().engine.trackOutput (std::clamp (t, 0, kNumTracks - 1)); }
MS_EXPORT int ms_track_triggered (int t) { return t >= 0 && t < kNumTracks && web().engine.trackTriggered (t) ? 1 : 0; }
MS_EXPORT void ms_clear_triggered() { web().engine.clearTriggered(); }

// The recorded events as 6 doubles each: time, type, track, index, value, sample id.
MS_EXPORT const double* ms_recorded_events()
{
    auto& w = web();
    w.recordedFlat.clear();
    for (const auto& e : w.recorded)
        w.recordedFlat.insert (w.recordedFlat.end(), { e.time, (double) e.type, (double) e.track,
                                                        (double) e.index, (double) e.value, (double) e.sampleId });
    return w.recordedFlat.data();
}

// ---- the page's copy of the project -------------------------------------------

MS_EXPORT void ms_ui_apply (int type, int track, int index, float value, int sampleId)
{
    applyToState (web().ui, makeCommand (type, track, index, value, sampleId));
}

MS_EXPORT void ms_ui_reset() { web().ui = makeDefaultProject(); }

// The engine switches to a queued pattern by itself at the end of a bar.
MS_EXPORT void ms_ui_sync_pattern (int pattern, int queued)
{
    auto& p = web().ui;
    p.pattern = std::clamp (pattern, 0, kNumPatterns - 1);
    p.queuedPattern = queued;
}

MS_EXPORT float ms_ui_bpm() { return web().ui.bpm; }
MS_EXPORT float ms_ui_swing() { return web().ui.swing; }
MS_EXPORT float ms_ui_master_level() { return web().ui.masterLevel; }
MS_EXPORT int ms_ui_pattern() { return web().ui.pattern; }
MS_EXPORT int ms_ui_queued_pattern() { return web().ui.queuedPattern; }
MS_EXPORT int ms_ui_viewed_pattern() { return viewedPattern (web().ui); }

MS_EXPORT int ms_ui_voice (int t) { auto* tr = trackAt (web().ui, t); return tr ? (int) tr->voice : 0; }
MS_EXPORT int ms_ui_character (int t) { auto* tr = trackAt (web().ui, t); return tr ? (int) tr->character : 0; }
MS_EXPORT float ms_ui_voice_param (int t, int i)
{
    auto* tr = trackAt (web().ui, t);
    return tr && i >= 0 && i < kNumVoiceParams ? tr->voiceParams[(size_t) i] : 0.0f;
}
MS_EXPORT float ms_ui_level (int t) { auto* tr = trackAt (web().ui, t); return tr ? tr->level : 0.0f; }
MS_EXPORT float ms_ui_pan (int t) { auto* tr = trackAt (web().ui, t); return tr ? tr->pan : 0.5f; }
MS_EXPORT int ms_ui_mute (int t) { auto* tr = trackAt (web().ui, t); return tr && tr->mute ? 1 : 0; }
MS_EXPORT int ms_ui_solo (int t) { auto* tr = trackAt (web().ui, t); return tr && tr->solo ? 1 : 0; }
MS_EXPORT int ms_ui_sample_id (int t) { auto* tr = trackAt (web().ui, t); return tr ? idForSample (tr->sample) : 0; }

MS_EXPORT float ms_ui_fx (int t, int i)
{
    auto& p = web().ui;
    if (i < 0 || i >= kNumFxParams) return 0.0f;
    if (t < 0) return p.masterFx[(size_t) i];
    auto* tr = trackAt (p, t);
    return tr ? tr->fx[(size_t) i] : 0.0f;
}

MS_EXPORT float ms_ui_step (int t, int pattern, int step)
{
    auto* tr = trackAt (web().ui, t);
    if (tr == nullptr || pattern < 0 || pattern >= kNumPatterns || step < 0 || step >= kNumSteps) return 0.0f;
    return tr->patterns[(size_t) pattern][(size_t) step];
}

// ---- names and readouts --------------------------------------------------------

MS_EXPORT int ms_num_tracks() { return kNumTracks; }
MS_EXPORT int ms_num_steps() { return kNumSteps; }
MS_EXPORT int ms_num_patterns() { return kNumPatterns; }
MS_EXPORT int ms_num_voice_params() { return kNumVoiceParams; }
MS_EXPORT int ms_num_fx_params() { return kNumFxParams; }
MS_EXPORT int ms_num_voice_types() { return kNumVoiceTypes; }
MS_EXPORT int ms_num_characters() { return kNumCharacters; }

MS_EXPORT const char* ms_voice_type_name (int v) { return copyText (voiceTypeName ((VoiceType) v)); }
MS_EXPORT const char* ms_character_name (int c) { return copyText (characterName ((Character) c)); }
MS_EXPORT const char* ms_drum_name (int v, int c) { return copyText (drumName ((VoiceType) v, (Character) c)); }

MS_EXPORT const char* ms_voice_param_name (int t, int i)
{
    auto* tr = trackAt (web().ui, t);
    return copyText (tr ? voiceParamName (tr->voice, tr->character, i) : nullptr);
}
MS_EXPORT int ms_voice_param_is_pitch (int t, int i)
{
    auto* tr = trackAt (web().ui, t);
    return tr && voiceParamIsPitch (tr->voice, tr->character, i) ? 1 : 0;
}
MS_EXPORT int ms_voice_param_steps (int t, int i)
{
    auto* tr = trackAt (web().ui, t);
    return tr ? voiceParamSteps (tr->voice, tr->character, i) : 0;
}
MS_EXPORT float ms_snap_voice_param (int t, int i, float v)
{
    auto* tr = trackAt (web().ui, t);
    return tr ? snapVoiceParamToNote (tr->voice, tr->character, i, v) : v;
}
MS_EXPORT float ms_default_voice_param (int t, int i)
{
    auto* tr = trackAt (web().ui, t);
    return tr && i >= 0 && i < kNumVoiceParams ? defaultVoiceParams (tr->voice, tr->character)[(size_t) i] : 0.0f;
}
// Readout for value v of voice parameter i on track t.
MS_EXPORT const char* ms_format_voice_param (int t, int i, float v)
{
    auto& w = web();
    auto* tr = trackAt (w.ui, t);
    if (tr == nullptr) return copyText ("");
    formatVoiceParam (tr->voice, tr->character, i, v, w.text, (int) sizeof (w.text));
    return w.text;
}

MS_EXPORT const char* ms_fx_param_name (int i) { return copyText (fxParamName (i)); }
MS_EXPORT int ms_fx_param_is_toggle (int i) { return fxParamIsToggle (i) ? 1 : 0; }
MS_EXPORT int ms_fx_param_steps (int i) { return fxParamSteps (i); }
MS_EXPORT int ms_fx_sync_param_for (int i) { return fxSyncParamFor (i); }
MS_EXPORT float ms_default_fx_param (int i) { return i >= 0 && i < kNumFxParams ? defaultFxParams()[(size_t) i] : 0.0f; }
// Readout for fx parameter i of track t (-1 = master) if it were set to v.
MS_EXPORT const char* ms_format_fx_param (int t, int i, float v)
{
    auto& w = web();
    if (i < 0 || i >= kNumFxParams) return copyText ("");
    FxParams fx = t < 0 ? w.ui.masterFx : (trackAt (w.ui, t) ? trackAt (w.ui, t)->fx : defaultFxParams());
    fx[(size_t) i] = v;
    formatFxParam (fx, i, w.text, (int) sizeof (w.text));
    return w.text;
}

MS_EXPORT const char* ms_note_name (float hz)
{
    auto& w = web();
    noteNameForFrequency (hz, w.text, (int) sizeof (w.text));
    return w.text;
}

// The drum's pitch in Hz for the signal box's harmonic lattice (0 when it
// has none, like noise or samples). Same as the desktop's fundamentalHz().
MS_EXPORT float ms_fundamental_hz (int t)
{
    auto* tr = trackAt (web().ui, t);
    if (tr == nullptr || tr->voice == VoiceType::Sample)
        return 0.0f;
    for (int i = 0; i < kNumVoiceParams; ++i)
        if (voiceParamIsPitch (tr->voice, tr->character, i) && voiceParamSteps (tr->voice, tr->character, i) == 0)
            return voiceParamValue (tr->voice, tr->character, i, tr->voiceParams[(size_t) i]);
    return 0.0f;
}

MS_EXPORT float ms_gain_db (float gain) { return gainToDb (gain); }

MS_EXPORT float ms_level_db (float v) { return gainToDb (levelToGain (v)); }

// ---- export (page) --------------------------------------------------------------

// Recording on the page side: the project as it was when REC was pressed,
// then the events the worklet recorded once it stops.
MS_EXPORT void ms_rec_begin (double sampleRate)
{
    auto& w = web();
    w.lastTake = Take {};
    w.lastTake.start = w.ui;
    w.lastTake.sampleRate = sampleRate;
}

MS_EXPORT void ms_rec_add_event (double time, int type, int track, int index, float value, int sampleId)
{
    web().lastTake.events.push_back ({ (std::int64_t) time, makeCommand (type, track, index, value, sampleId) });
}

MS_EXPORT void ms_rec_set_length (double samples) { web().lastTake.length = (std::int64_t) samples; }
MS_EXPORT double ms_rec_length() { return (double) web().lastTake.length; }

// Makes the last recording the take to export. Returns 0 if there is none.
MS_EXPORT int ms_take_use_recording()
{
    auto& w = web();
    if (w.lastTake.length <= 0)
        return 0;
    w.take = w.lastTake;
    return 1;
}

// A take that plays one pattern a number of times (the desktop's EXPORT >
// PATTERN xN).
MS_EXPORT void ms_take_pattern (double sampleRate, int loops, int pattern)
{
    auto& w = web();
    w.take = Take {};
    w.take.start = w.ui;
    w.take.start.pattern = pattern >= 0 ? pattern : viewedPattern (w.ui);
    w.take.start.queuedPattern = -1;
    w.take.sampleRate = sampleRate;
    const double stepLen = sampleRate * 60.0 / (double) w.ui.bpm / 4.0;
    w.take.length = (std::int64_t) std::llround (stepLen * kNumSteps * std::max (1, loops));
}

MS_EXPORT double ms_take_length() { return (double) web().take.length; }

// Starts rendering the take: soloTrack -1 = master, else that track's stem.
MS_EXPORT void ms_render_start (int soloTrack)
{
    auto& w = web();
    w.player = std::make_unique<Player>();
    auto& e = w.player->engine;
    e.prepare (w.take.sampleRate, kMaxBlock);
    e.setState (w.take.start);
    e.reset();
    e.setSoloRender (soloTrack);
    e.setPlaying (w.take.length > 0);
}

// Renders up to maxSamples into ms_left() / ms_right(), stopping early at the
// next event, and returns the number rendered.
MS_EXPORT int ms_render_next (int maxSamples)
{
    auto& w = web();
    if (w.player == nullptr) return 0;
    auto& p = *w.player;
    const auto& take = w.take;

    while (p.nextEvent < take.events.size() && take.events[p.nextEvent].time <= p.position)
        p.engine.apply (take.events[p.nextEvent++].command);

    if (p.position >= take.length)
        p.engine.setPlaying (false);

    std::int64_t limit = std::clamp (maxSamples, 1, kMaxBlock);
    if (p.nextEvent < take.events.size())
        limit = std::min (limit, take.events[p.nextEvent].time - p.position);
    if (p.position < take.length)
        limit = std::min (limit, take.length - p.position);

    const int n = (int) std::max<std::int64_t> (1, limit);
    p.engine.process (w.left.data(), w.right.data(), n);
    p.position += n;
    return n;
}
