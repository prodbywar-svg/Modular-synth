#include "DrumMachineView.h"

#include "SignalBox.h"

using namespace ms;

namespace
{
constexpr int kHeaderHeight = 72;
constexpr int kPanelHeight = 250;
constexpr int kStatusHeight = 24;
constexpr int kMargin = 16;

// Keys that play pads 1-8: number row and home row.
constexpr char kPadKeys[kNumTracks][2] = { { '1', 'a' }, { '2', 's' }, { '3', 'd' }, { '4', 'f' },
                                          { '5', 'g' }, { '6', 'h' }, { '7', 'j' }, { '8', 'k' } };

} // namespace

// =============================================================================
// Track grid: 8 tracks as horizontal data rows, plus the master row.

class TrackGrid : public juce::Component
{
public:
    explicit TrackGrid (DrumMachineView& e) : editor (e) {}

    void tick()
    {
        auto& proc = editor.processor();
        for (int t = 0; t < kNumTracks; ++t)
        {
            const int hits = proc.trackHitCount (t);
            if (hits != lastHits[(size_t) t])
            {
                lastHits[(size_t) t] = hits;
                flash[(size_t) t] = 1.0f;
            }
            else
            {
                flash[(size_t) t] *= 0.82f;
            }
        }
        repaint();
    }

    void paint (juce::Graphics& g) override
    {
        auto& proc = editor.processor();
        const auto& state = proc.getUiState();
        const auto geo = geometry();
        const int viewed = viewedPattern (state);
        const bool playing = proc.isPlaying() && proc.currentPattern() == viewed;
        const int playStep = proc.currentStep();

        for (int t = 0; t < kNumTracks; ++t)
            paintTrack (g, geo, t, state.tracks[(size_t) t], viewed, playing ? playStep : -1);

        paintMaster (g, geo, state);

        // step ruler along the top: step numbers, beats brighter
        g.setFont (look::mono (9.0f));
        for (int s = 0; s < kNumSteps; ++s)
        {
            g.setColour (s % 4 == 0 ? look::ink.withAlpha (0.7f) : look::dim);
            g.drawText (look::pad (s + 1, 2), juce::Rectangle<float> (geo.stepX (s), 0.0f, geo.stepW, (float) kRulerHeight),
                        juce::Justification::centred);
        }
        g.setFont (look::mono (9.5f, true));
        g.setColour (state.queuedPattern >= 0 ? look::blue : look::ink);
        g.drawText ("PATTERN P" + juce::String (viewed + 1) + (state.queuedPattern >= 0 ? " / QUEUED" : ""),
                    juce::Rectangle<float> (geo.nameX + 10.0f, 0.0f, geo.stepsX - geo.nameX - 14.0f, (float) kRulerHeight),
                    juce::Justification::centredLeft);
        g.setFont (look::mono (9.0f));
        g.setColour (look::dim);
        g.drawText ("SIGNAL // " + juce::String (ModularSynthProcessor::kHistoryLength) + " x "
                        + juce::String (ModularSynthProcessor::kHistoryWindow) + " SMP",
                    juce::Rectangle<float> (geo.barcodeX, 0.0f, geo.barcodeW, (float) kRulerHeight),
                    juce::Justification::centredLeft);

        // playhead: the one red line
        if (playing)
        {
            const float x = geo.stepX (playStep) + geo.stepW * 0.5f;
            g.setColour (look::red);
            g.fillRect (x - 0.5f, (float) kRulerHeight - 4.0f, 1.0f, geo.rowY (kNumTracks) - (float) kRulerHeight + 4.0f);
            juce::Path marker;
            marker.addTriangle (x - 4.0f, (float) kRulerHeight - 6.0f, x + 4.0f, (float) kRulerHeight - 6.0f, x, (float) kRulerHeight - 1.0f);
            g.fillPath (marker);
        }
    }

    void mouseDown (const juce::MouseEvent& e) override
    {
        const auto geo = geometry();
        const auto p = e.position;
        const int row = geo.rowAt (p.y);
        dragMode = DragMode::None;
        if (row < 0)
            return;

        if (row == kNumTracks)
        {
            editor.selectTrack (-1);
            return;
        }

        const int t = row;
        const auto& tr = editor.processor().getUiState().tracks[(size_t) t];

        if (p.x < geo.nameX)
        {
            const float rel = (p.y - geo.rowY (t)) / geo.rowH;
            editor.selectTrack (t);
            // pad hit: clicking higher in the box hits harder
            editor.send (CommandType::Trigger, t, 0, juce::jlimit (0.3f, 1.0f, 1.15f - rel * 0.8f));
            return;
        }

        if (p.x < geo.stepsX - 4.0f)
        {
            editor.selectTrack (t);
            if (e.mods.isPopupMenu())
                editor.showVoiceMenu (t);
            return;
        }

        const int s = geo.stepAt (p.x);
        if (s >= 0)
        {
            editor.selectTrack (t);
            dragTrack = t;
            dragStep = s;
            dragStartVelocity = steps (tr)[(size_t) s];
            if (dragStartVelocity > 0.0f)
            {
                dragMode = DragMode::PendingOff;
            }
            else
            {
                dragMode = DragMode::PaintOn;
                editor.send (CommandType::SetStep, t, stepIndex (s), 1.0f);
            }
            return;
        }

        if (p.x >= geo.muteX && p.x < geo.muteX + geo.toggleW)
            editor.send (CommandType::SetMute, t, 0, tr.mute ? 0.0f : 1.0f);
        else if (p.x >= geo.soloX && p.x < geo.soloX + geo.toggleW)
            editor.send (CommandType::SetSolo, t, 0, tr.solo ? 0.0f : 1.0f);
        else
            editor.selectTrack (t);
    }

    void mouseDrag (const juce::MouseEvent& e) override
    {
        const auto geo = geometry();

        if (dragMode == DragMode::PendingOff && std::abs (e.getDistanceFromDragStartY()) > 3)
            dragMode = DragMode::Velocity;

        if (dragMode == DragMode::Velocity)
        {
            const float v = juce::jlimit (0.05f, 1.0f, dragStartVelocity - (float) e.getDistanceFromDragStartY() / 90.0f);
            editor.send (CommandType::SetStep, dragTrack, stepIndex (dragStep), v);
        }
        else if (dragMode == DragMode::PaintOn)
        {
            const int s = geo.stepAt (e.position.x);
            if (s >= 0 && steps (editor.processor().getUiState().tracks[(size_t) dragTrack])[(size_t) s] <= 0.0f)
                editor.send (CommandType::SetStep, dragTrack, stepIndex (s), 1.0f);
        }
    }

    void mouseUp (const juce::MouseEvent&) override
    {
        if (dragMode == DragMode::PendingOff)
            editor.send (CommandType::SetStep, dragTrack, stepIndex (dragStep), 0.0f);
        dragMode = DragMode::None;
    }

    void mouseDoubleClick (const juce::MouseEvent& e) override
    {
        const auto geo = geometry();
        const int row = geo.rowAt (e.position.y);
        if (row >= 0 && row < kNumTracks && e.position.x >= geo.nameX && e.position.x < geo.stepsX - 4.0f)
            editor.showVoiceMenu (row);
    }

private:
    static constexpr int kRulerHeight = 18;

    // the pattern on screen and the command index for one of its steps
    int viewed() const { return viewedPattern (editor.processor().getUiState()); }
    const Pattern& steps (const TrackState& tr) const { return tr.patterns[(size_t) viewed()]; }
    int stepIndex (int step) const { return viewed() * kNumSteps + step; }

    struct Geometry
    {
        float width = 0, rowH = 0, top = 0;
        float nameX = 44, stepsX = 190, stepW = 40, barcodeX = 0, barcodeW = 0;
        float muteX = 0, soloX = 0, toggleW = 26;

        float rowY (int r) const { return top + rowH * (float) r; }
        float stepX (int s) const { return stepsX + stepW * (float) s; }
        int rowAt (float y) const
        {
            const int r = (int) std::floor ((y - top) / rowH);
            return y >= top && r >= 0 && r <= kNumTracks ? r : -1;
        }
        int stepAt (float x) const
        {
            const int s = (int) std::floor ((x - stepsX) / stepW);
            return x >= stepsX && s >= 0 && s < kNumSteps ? s : -1;
        }
    };

    Geometry geometry() const
    {
        Geometry g;
        g.width = (float) getWidth();
        g.top = (float) kRulerHeight;
        g.rowH = ((float) getHeight() - g.top) / (float) (kNumTracks + 1);
        g.stepW = juce::jlimit (22.0f, 44.0f, (g.width - g.stepsX - 300.0f) / (float) kNumSteps);
        g.soloX = g.width - g.toggleW;
        g.muteX = g.soloX - g.toggleW - 6.0f;
        g.barcodeX = g.stepX (kNumSteps) + 24.0f;
        g.barcodeW = juce::jmin ((float) ModularSynthProcessor::kHistoryLength, g.muteX - 16.0f - g.barcodeX);
        g.barcodeX = g.muteX - 16.0f - g.barcodeW;
        return g;
    }

    void paintIndex (juce::Graphics& g, juce::Rectangle<float> box, const juce::String& text, float flashAmount, bool selected)
    {
        if (flashAmount > 0.02f)
        {
            g.setColour (look::ink.withAlpha (flashAmount));
            g.fillRect (box);
        }
        g.setColour (selected ? look::ink : look::dim);
        g.drawRect (box, 1.0f);
        g.setFont (look::mono (11.0f, true));
        g.setColour (flashAmount > 0.5f ? look::background : look::ink);
        g.drawText (text, box, juce::Justification::centred);
    }

    // An active step drawn as a white tag holding its velocity as a MIDI-style
    // number (127 = full). The step under the playhead turns into an outline.
    static void paintStepTag (juce::Graphics& g, juce::Rectangle<float> cell, float velocity, bool now, float alpha)
    {
        const float fontH = juce::jlimit (8.0f, 11.5f, cell.getWidth() / 1.9f);
        const auto tag = cell.withSizeKeepingCentre (cell.getWidth(), juce::jmin (cell.getHeight(), fontH + 7.0f));
        const auto label = look::pad (juce::jlimit (1, 127, juce::roundToInt (velocity * 127.0f)), 3);

        g.setColour (look::ink.withAlpha (alpha));
        if (now)
            g.drawRect (tag, 1.0f);
        else
            g.fillRect (tag);
        g.setColour (now ? look::ink.withAlpha (alpha) : look::background);
        g.setFont (look::mono (fontH, true));
        g.drawText (label, tag, juce::Justification::centred, false);
    }

    void paintToggle (juce::Graphics& g, float x, float y, float w, const char* text, bool on, bool rowSelected)
    {
        const juce::Rectangle<float> box (x, y, w, 18.0f);
        if (on)
        {
            g.setColour (look::ink);
            g.fillRect (box);
            g.setColour (look::background);
        }
        else
        {
            g.setColour (rowSelected ? look::dim : look::faint);
            g.drawRect (box, 1.0f);
            g.setColour (look::dim);
        }
        g.setFont (look::mono (10.0f, true));
        g.drawText (text, box, juce::Justification::centred);
    }

    void paintTrack (juce::Graphics& g, const Geometry& geo, int t, const TrackState& tr, int pattern, int playStep)
    {
        auto& proc = editor.processor();
        const float y0 = geo.rowY (t), h = geo.rowH, mid = y0 + h * 0.5f;
        const bool selected = editor.selectedTrack() == t;
        const float alpha = tr.mute ? 0.35f : 1.0f;

        g.setColour (look::faint);
        g.fillRect (0.0f, y0, geo.width, 1.0f);

        paintIndex (g, { 4.0f, y0 + 6.0f, 30.0f, h - 12.0f }, look::pad (t + 1, 2), flash[(size_t) t], selected);

        // name + summary line
        if (selected)
        {
            g.setColour (look::ink);
            g.fillRect (geo.nameX, y0 + 8.0f, 2.0f, h - 16.0f);
        }
        g.setFont (look::mono (13.0f, true));
        g.setColour (look::ink.withAlpha (alpha * (selected ? 1.0f : 0.8f)));
        g.drawText (proc.trackName (t), juce::Rectangle<float> (geo.nameX + 10.0f, y0 + 4.0f, geo.stepsX - geo.nameX - 14.0f, h * 0.5f - 2.0f),
                    juce::Justification::bottomLeft, true);
        g.setFont (look::mono (9.5f));
        g.setColour (look::dim);
        char first[32];
        formatVoiceParam (tr.voice, tr.character, 0, tr.voiceParams[0], first, (int) sizeof (first));
        juce::String sub = juce::String (characterName (tr.character)) + " / " + juce::String (first);
        if (tr.voice == VoiceType::Sample && tr.sample == nullptr)
            sub = "EMPTY / RIGHT-CLICK";
        g.drawText (sub, juce::Rectangle<float> (geo.nameX + 10.0f, mid + 2.0f, geo.stepsX - geo.nameX - 14.0f, h * 0.5f - 6.0f),
                    juce::Justification::topLeft, true);

        // steps: a thin data line, active steps as white dashes (height = velocity)
        const float stepsEnd = geo.stepX (kNumSteps);
        g.setColour (look::faint.brighter (0.2f));
        g.fillRect (geo.stepsX, mid, stepsEnd - geo.stepsX, 1.0f);
        for (int s = 0; s < kNumSteps; ++s)
        {
            const float x = geo.stepX (s);
            g.setColour (s % 4 == 0 ? look::dim : look::faint);
            g.fillRect (x, y0 + h - 7.0f, 1.0f, 4.0f);

            const float v = tr.patterns[(size_t) pattern][(size_t) s];
            if (v > 0.0f)
                paintStepTag (g, { x + 2.0f, y0 + 7.0f, geo.stepW - 4.0f, h - 16.0f }, v, s == playStep, alpha);
        }

        // signal panel: the track's harmonic lattice, see paintSignalBox
        const auto bx = geo.barcodeX, bw = geo.barcodeW;
        paintSignalBox (g, juce::Rectangle<float> (bx - 1.0f, y0 + 5.0f, bw + 2.0f, h - 10.0f), t, tr, alpha);
        float lastPeak = 0.0f;
        for (int c = 0; c < 8; ++c)
            lastPeak = juce::jmax (lastPeak, proc.trackHistory (t, c));
        g.setFont (look::mono (9.0f));
        g.setColour (lastPeak > 1.0f ? look::red : look::dim);
        g.drawText (formatDb (lastPeak), juce::Rectangle<float> (bx - 66.0f, y0 + 4.0f, 60.0f, 12.0f), juce::Justification::topRight);

        paintToggle (g, geo.muteX, mid - 9.0f, geo.toggleW, "M", tr.mute, selected);
        paintToggle (g, geo.soloX, mid - 9.0f, geo.toggleW, "S", tr.solo, selected);
    }

    // The track's tuned pitch in Hz, or 0 for unpitched voices (clicks,
    // noise, samples, voices tuned in semitones).
    static float fundamentalHz (const TrackState& tr)
    {
        if (tr.voice == VoiceType::Sample)
            return 0.0f;
        for (int i = 0; i < kNumVoiceParams; ++i)
            if (voiceParamIsPitch (tr.voice, tr.character, i) && voiceParamSteps (tr.voice, tr.character, i) == 0)
                return voiceParamValue (tr.voice, tr.character, i, tr.voiceParams[(size_t) i]);
        return 0.0f;
    }

    // the drum's pitch and its readout, for the signal panel
    void paintSignalBox (juce::Graphics& g, juce::Rectangle<float> box, int t, const TrackState& tr, float alpha)
    {
        auto& proc = editor.processor();
        proc.copyTrackScope (t, signal.scope(), SignalBox::kFftSize);
        const float f0 = fundamentalHz (tr);
        juce::String pitchText;
        for (int i = 0; i < kNumVoiceParams && f0 > 0.0f; ++i)
            if (voiceParamIsPitch (tr.voice, tr.character, i))
            {
                char buf[32];
                formatVoiceParam (tr.voice, tr.character, i, tr.voiceParams[(size_t) i], buf, (int) sizeof (buf));
                pitchText = buf;
                break;
            }
        signal.paint (g, box, proc.getEngineSampleRate(), f0, pitchText, alpha);
    }

    void paintMaster (juce::Graphics& g, const Geometry& geo, const ProjectState& state)
    {
        auto& proc = editor.processor();
        const float y0 = geo.rowY (kNumTracks), h = geo.rowH, mid = y0 + h * 0.5f;
        const bool selected = editor.selectedTrack() < 0;

        g.setColour (look::dim);
        g.fillRect (0.0f, y0, geo.width, 1.0f);
        paintIndex (g, { 4.0f, y0 + 6.0f, 30.0f, h - 12.0f }, "BS", 0.0f, selected);

        if (selected)
        {
            g.setColour (look::ink);
            g.fillRect (geo.nameX, y0 + 8.0f, 2.0f, h - 16.0f);
        }
        g.setFont (look::mono (13.0f, true));
        g.setColour (look::ink);
        g.drawText ("DRUM BUS", juce::Rectangle<float> (geo.nameX + 10.0f, y0 + 4.0f, 120.0f, h * 0.5f - 2.0f), juce::Justification::bottomLeft);
        g.setFont (look::mono (9.5f));
        g.setColour (look::dim);
        g.drawText ("SUM + RACK " + formatDb (levelToGain (state.masterLevel)),
                    juce::Rectangle<float> (geo.nameX + 10.0f, mid + 2.0f, geo.stepsX - geo.nameX - 14.0f, h * 0.5f - 6.0f),
                    juce::Justification::topLeft, true);

        // effects summary as a raw data line
        juce::String data;
        for (int i : { fxCrushBits, fxDistDrive, fxDistMix, fxCutoff, fxDelayTime, fxDelayMix, fxGrainTime, fxGrainMix,
                       fxReverbTime, fxReverbMix, fxChorusMix, fxPsyStages, fxPsyMix })
        {
            char buf[32];
            formatFxParam (state.masterFx, i, buf, (int) sizeof (buf));
            data << fxParamName (i) << " " << buf << "   ";
        }
        g.setFont (look::mono (9.5f));
        g.setColour (look::dim);
        g.drawFittedText (data, juce::Rectangle<float> (geo.stepsX, y0 + 6.0f, geo.stepX (kNumSteps) - geo.stepsX, h - 12.0f).toNearestInt(),
                          juce::Justification::centredLeft, 3, 1.0f);

        paintPeakMeter (g, geo.barcodeX, mid, geo.barcodeW, proc.drumBusPeak(), masterHold);
    }

    enum class DragMode
    {
        None,
        PaintOn,
        PendingOff,
        Velocity
    };

    DrumMachineView& editor;
    std::array<int, kNumTracks> lastHits {};
    std::array<float, kNumTracks> flash {};
    SignalBox signal; // reused for every track
    float masterHold = 0.0f;
    DragMode dragMode = DragMode::None;
    int dragTrack = 0, dragStep = 0;
    float dragStartVelocity = 0.0f;
};

// =============================================================================
// Parameter panel for the selected track (or the master).

class ParamPanel : public juce::Component
{
public:
    explicit ParamPanel (DrumMachineView& e) : editor (e)
    {
        addAndMakeVisible (typeButton);
        typeButton.onClick = [this] {
            if (editor.selectedTrack() >= 0)
                editor.showVoiceMenu (editor.selectedTrack());
        };

        addAndMakeVisible (copyButton);
        addAndMakeVisible (copyPatternButton);
        addAndMakeVisible (pasteButton);
        addAndMakeVisible (randomButton);
        copyButton.onClick = [this] { editor.copySequence (false); };
        copyPatternButton.onClick = [this] { editor.copySequence (true); };
        pasteButton.onClick = [this] { editor.pasteSequence(); };
        randomButton.onClick = [this] { editor.randomizeSequence (editor.selectedTrack()); };

        for (int c = 0; c < kNumCharacters; ++c)
        {
            auto& b = characterButtons[(size_t) c];
            b.setText (characterName ((Character) c));
            addAndMakeVisible (b);
            b.onClick = [this, c] {
                if (editor.selectedTrack() >= 0)
                    editor.send (CommandType::SetCharacter, editor.selectedTrack(), c, 0.0f);
            };
        }

        for (int i = 0; i < kNumVoiceParams; ++i)
        {
            auto& s = voice[(size_t) i];
            addAndMakeVisible (s);
            s.onChange = [this, i] (float v) { editor.send (CommandType::SetVoiceParam, editor.selectedTrack(), i, v); };
        }

        addAndMakeVisible (level);
        level.setLabel ("LEVEL");
        level.setDefault (0.7071f);
        level.format = [] (float v) { return formatDb (levelToGain (v)); };
        level.onChange = [this] (float v) {
            if (editor.selectedTrack() < 0)
                editor.send (CommandType::SetMasterLevel, 0, 0, v);
            else
                editor.send (CommandType::SetLevel, editor.selectedTrack(), 0, v);
        };

        addAndMakeVisible (pan);
        pan.setLabel ("PAN");
        pan.setDefault (0.5f);
        pan.setBipolar (true);
        pan.format = [] (float v) { return formatPan (v); };
        pan.onChange = [this] (float v) { editor.send (CommandType::SetPan, editor.selectedTrack(), 0, v); };

        fx.attach (*this, [this] { return shownFx(); },
                   [this] (int i, float v) { editor.send (CommandType::SetFxParam, editor.selectedTrack(), i, v); });
    }

    void refresh()
    {
        const int t = editor.selectedTrack();
        const auto& state = editor.processor().getUiState();
        const bool master = t < 0;
        const auto voiceType = master ? VoiceType::Count : state.tracks[(size_t) t].voice;
        const auto character = master ? Character::Count : state.tracks[(size_t) t].character;

        if (t != shownTrack || voiceType != shownVoice || character != shownCharacter)
        {
            shownTrack = t;
            shownVoice = voiceType;
            shownCharacter = character;
            typeButton.setVisible (! master);
            randomButton.setVisible (! master);
            copyButton.setVisible (! master);
            resized();
            for (int c = 0; c < kNumCharacters; ++c)
            {
                characterButtons[(size_t) c].setVisible (! master);
                characterButtons[(size_t) c].setOn (character == (Character) c);
            }
            pan.setVisible (! master);
            level.setDefault (master ? ProjectState().masterLevel : TrackState().level);

            const auto defaults = master ? VoiceParams {} : defaultVoiceParams (voiceType, character);
            for (int i = 0; i < kNumVoiceParams; ++i)
            {
                auto& s = voice[(size_t) i];
                const char* name = master ? nullptr : voiceParamName (voiceType, character, i);
                s.setVisible (name != nullptr);
                if (name == nullptr)
                    continue;
                s.setLabel (name);
                s.setDefault (defaults[(size_t) i]);
                s.setSteps (voiceParamSteps (voiceType, character, i));
                s.snap = nullptr;
                if (voiceParamIsPitch (voiceType, character, i))
                    s.snap = [voiceType, character, i] (float v) { return snapVoiceParamToNote (voiceType, character, i, v); };
                s.format = [voiceType, character, i] (float v) {
                    char buf[32];
                    formatVoiceParam (voiceType, character, i, v, buf, (int) sizeof (buf));
                    return juce::String (buf);
                };
                s.repaint();
            }
            if (! master)
                typeButton.setText ("TYPE: " + juce::String (drumName (voiceType, character)));
            repaint();
        }

        if (master)
        {
            level.setValue (state.masterLevel);
            fx.setValues (state.masterFx);
            return;
        }

        const auto& tr = state.tracks[(size_t) t];
        for (int i = 0; i < kNumVoiceParams; ++i)
            voice[(size_t) i].setValue (tr.voiceParams[(size_t) i]);
        level.setValue (tr.level);
        pan.setValue (tr.pan);
        fx.setValues (tr.fx);
    }

    void paint (juce::Graphics& g) override
    {
        g.setColour (look::dim);
        g.fillRect (0, 0, getWidth(), 1);

        const int t = editor.selectedTrack();
        const auto title = t < 0 ? juce::String ("DRUM BUS // ALL 8 DRUMS + EFFECTS RACK")
                                 : look::pad (t + 1, 2) + " " + editor.processor().trackName (t) + " // PARAMETERS";
        g.setFont (look::mono (11.0f, true));
        g.setColour (look::ink);
        g.drawText (title, 0, 6, getWidth() / 2, 18, juce::Justification::centredLeft);

        g.setFont (look::mono (9.5f));
        g.setColour (look::dim);
        for (int c = 0; c < kColumns; ++c)
            g.drawText (c == 0 ? "VOICE + MIX" : FxRackControls::groupName (c - 1), columnX (c), 30, columnX (1) - 24, 14, juce::Justification::centredLeft);
        g.drawFittedText ("DRAG / SHIFT FINE / ALT OFF-NOTE / DOUBLE-CLICK RESET",
                          juce::Rectangle<int> (columnX (0), 48 + 6 * 20 + 18, columnX (1) - 24, 28), juce::Justification::topLeft, 2);
    }

    void resized() override
    {
        const int rowH = 20, y0 = 48;
        const int colW = columnX (1) - columnX (0) - 24;

        auto top = juce::Rectangle<int> (columnX (1), 4, getWidth() - columnX (1), 20);
        typeButton.setBounds (top.removeFromLeft (150));
        top.removeFromLeft (12);
        for (auto& b : characterButtons)
        {
            b.setBounds (top.removeFromLeft (70));
            top.removeFromLeft (4);
        }
        // sequence tools: right after the characters on a track; where the
        // track buttons would be on the master
        if (editor.selectedTrack() < 0)
            top = juce::Rectangle<int> (columnX (1), 4, getWidth() - columnX (1), 20);
        else
            top.removeFromLeft (20);
        if (editor.selectedTrack() >= 0)
        {
            copyButton.setBounds (top.removeFromLeft (92));
            top.removeFromLeft (4);
        }
        copyPatternButton.setBounds (top.removeFromLeft (112));
        top.removeFromLeft (4);
        pasteButton.setBounds (top.removeFromLeft (64));
        top.removeFromLeft (4);
        randomButton.setBounds (top.removeFromLeft (76));

        for (int i = 0; i < kNumVoiceParams; ++i)
            voice[(size_t) i].setBounds (columnX (0), y0 + i * rowH, colW, rowH);
        level.setBounds (columnX (0), y0 + 4 * rowH + 8, colW, rowH);
        pan.setBounds (columnX (0), y0 + 5 * rowH + 8, colW, rowH);

        fx.layout ([this] (int c) { return columnX (c + 1); }, y0, rowH, colW);
    }

private:
    static constexpr int kColumns = 5;
    int columnX (int c) const { return c * (getWidth() + 24) / kColumns; }

    FxParams shownFx() const
    {
        const auto& state = editor.processor().getUiState();
        const int t = editor.selectedTrack();
        return t < 0 ? state.masterFx : state.tracks[(size_t) t].fx;
    }

    DrumMachineView& editor;
    DataButton typeButton { "TYPE" };
    DataButton copyButton { "COPY DRUM" }, copyPatternButton { "COPY PATTERN" }, pasteButton { "PASTE" }, randomButton { "RANDOM" };
    std::array<DataButton, kNumCharacters> characterButtons { DataButton { "LIVE" }, DataButton { "DIGITAL" }, DataButton { "HR-16" } };
    std::array<DataSlider, kNumVoiceParams> voice;
    DataSlider level, pan;
    FxRackControls fx;
    int shownTrack = -2;
    VoiceType shownVoice = VoiceType::Count;
    Character shownCharacter = Character::Count;
};

// =============================================================================

void DrumMachineView::send (CommandType type, int track, int index, float value, const SampleData* sample)
{
    Command c;
    c.type = type;
    c.track = track;
    c.index = index;
    c.value = value;
    c.sample = sample;
    proc.send (c);
}

void DrumMachineView::selectTrack (int track)
{
    if (selected != track)
    {
        selected = track;
        panel->refresh();
        panel->repaint();
    }
}

void DrumMachineView::setStatus (const juce::String& s)
{
    status = s;
    statusTime = juce::Time::getMillisecondCounter();
    repaint (0, getHeight() - kStatusHeight, getWidth(), kStatusHeight);
}

void DrumMachineView::hitPad (int track, float velocity)
{
    send (CommandType::Trigger, track, 0, velocity);
}

void DrumMachineView::showVoiceMenu (int track)
{
    juce::PopupMenu m;
    m.addSectionHeader (look::pad (track + 1, 2) + " VOICE");
    const auto& tr = proc.getUiState().tracks[(size_t) track];
    for (int v = 0; v < kNumVoiceTypes; ++v)
        if ((VoiceType) v != VoiceType::Sample)
            m.addItem (1 + v, drumName ((VoiceType) v, tr.character), true, tr.voice == (VoiceType) v);
    if (tr.voice != VoiceType::Sample)
    {
        m.addSectionHeader ("CHARACTER");
        for (int c = 0; c < kNumCharacters; ++c)
            m.addItem (200 + c, characterName ((Character) c), true, tr.character == (Character) c);
    }
    m.addSectionHeader ("SEQUENCE");
    m.addItem (300, "COPY THIS DRUM  (CTRL+C)");
    m.addItem (303, "COPY WHOLE PATTERN  (CTRL+SHIFT+C)");
    m.addItem (301, "PASTE  (CTRL+V)", clipboard.has_value());
    m.addItem (302, "RANDOM  (CTRL+R)");
    m.addSeparator();
    m.addItem (100, "LOAD SAMPLE...");

    m.showMenuAsync (juce::PopupMenu::Options().withMousePosition(), [this, track] (int r) {
        if (r >= 300)
            selectTrack (track);
        if (r == 300)
            copySequence (false);
        else if (r == 303)
            copySequence (true);
        else if (r == 301)
            pasteSequence();
        else if (r == 302)
            randomizeSequence (track);
        else if (r == 100)
            loadSampleInto (track);
        else if (r >= 200)
            send (CommandType::SetCharacter, track, r - 200, 0.0f);
        else if (r > 0)
            send (CommandType::SetVoiceType, track, r - 1, 0.0f);
        panel->refresh();
    });
}

void DrumMachineView::loadSampleInto (int track)
{
    chooser = std::make_unique<juce::FileChooser> ("Load a sample into track " + juce::String (track + 1), lastFolder,
                                                   "*.wav;*.aif;*.aiff;*.flac");
    chooser->launchAsync (juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles,
                          [this, track] (const juce::FileChooser& fc) {
                              const auto file = fc.getResult();
                              if (file == juce::File())
                                  return;
                              juce::String error;
                              const auto* sample = proc.loadSample (file, error);
                              if (sample == nullptr)
                              {
                                  setStatus (error.toUpperCase());
                                  return;
                              }
                              send (CommandType::SetVoiceType, track, (int) VoiceType::Sample, 0.0f);
                              send (CommandType::SetSample, track, 0, 0.0f, sample);
                              selectTrack (track);
                              panel->refresh();
                              setStatus ("LOADED " + file.getFileName().toUpperCase() + " INTO TRACK " + look::pad (track + 1, 2));
                          });
}

void DrumMachineView::selectPattern (int pattern)
{
    // While playing, the new pattern waits for the current one to finish.
    const bool queue = proc.isPlaying();
    send (CommandType::SelectPattern, 0, pattern, queue ? 1.0f : 0.0f);
    const auto& state = proc.getUiState();
    if (queue && state.queuedPattern >= 0)
        setStatus ("P" + juce::String (pattern + 1) + " QUEUED / STARTS WHEN P" + juce::String (state.pattern + 1) + " ENDS");
}

void DrumMachineView::copyPatternTo (int pattern)
{
    const auto& state = proc.getUiState();
    const int from = viewedPattern (state);
    if (from == pattern)
        return;
    for (int t = 0; t < kNumTracks; ++t)
    {
        const auto source = state.tracks[(size_t) t].patterns[(size_t) from];
        for (int s = 0; s < kNumSteps; ++s)
            send (CommandType::SetStep, t, pattern * kNumSteps + s, source[(size_t) s]);
    }
    setStatus ("COPIED P" + juce::String (from + 1) + " TO P" + juce::String (pattern + 1));
}

DrumMachineView::UndoSnapshot DrumMachineView::takeSnapshot (int pattern, bool withFx) const
{
    const auto& state = proc.getUiState();
    UndoSnapshot snap;
    snap.pattern = pattern;
    snap.hasFx = withFx;
    snap.masterFx = state.masterFx;
    for (int t = 0; t < kNumTracks; ++t)
    {
        snap.steps[(size_t) t] = state.tracks[(size_t) t].patterns[(size_t) pattern];
        snap.fx[(size_t) t] = state.tracks[(size_t) t].fx;
    }
    return snap;
}

void DrumMachineView::clearPattern()
{
    // Empties the pattern on screen and puts every effects rack (the 8 tracks
    // and the master) back to its defaults.
    const auto& state = proc.getUiState();
    const int pattern = viewedPattern (state);
    const auto defaults = defaultFxParams();
    const auto before = takeSnapshot (pattern, true);

    bool changed = before.masterFx != defaults;
    for (int t = 0; t < kNumTracks; ++t)
    {
        changed = changed || before.fx[(size_t) t] != defaults;
        for (float v : before.steps[(size_t) t])
            changed = changed || v > 0.0f;
    }
    if (! changed)
        return;

    for (int t = 0; t < kNumTracks; ++t)
    {
        for (int s = 0; s < kNumSteps; ++s)
            if (before.steps[(size_t) t][(size_t) s] > 0.0f)
                send (CommandType::SetStep, t, pattern * kNumSteps + s, 0.0f);
        for (int i = 0; i < kNumFxParams; ++i)
            if (! juce::exactlyEqual (before.fx[(size_t) t][(size_t) i], defaults[(size_t) i]))
                send (CommandType::SetFxParam, t, i, defaults[(size_t) i]);
    }
    for (int i = 0; i < kNumFxParams; ++i)
        if (! juce::exactlyEqual (before.masterFx[(size_t) i], defaults[(size_t) i]))
            send (CommandType::SetFxParam, -1, i, defaults[(size_t) i]);

    lastUndo = before;
    setStatus ("CLEARED P" + juce::String (pattern + 1) + " + RESET ALL EFFECTS / CTRL+Z TO BRING IT BACK");
}

void DrumMachineView::applySettings (const ProjectState& target)
{
    // Only sends what differs. The voice and character go first because
    // changing them resets that drum's voice controls.
    const auto& state = proc.getUiState();
    auto set = [this] (CommandType type, int track, int index, float now, float want) {
        if (! juce::exactlyEqual (now, want))
            send (type, track, index, want);
    };
    for (int t = 0; t < kNumTracks; ++t)
    {
        const auto& want = target.tracks[(size_t) t];
        const auto& now = state.tracks[(size_t) t];
        if (now.voice != want.voice)
            send (CommandType::SetVoiceType, t, (int) want.voice, 0.0f);
        if (now.character != want.character)
            send (CommandType::SetCharacter, t, (int) want.character, 0.0f);
        for (int i = 0; i < kNumVoiceParams; ++i)
            set (CommandType::SetVoiceParam, t, i, now.voiceParams[(size_t) i], want.voiceParams[(size_t) i]);
        set (CommandType::SetLevel, t, 0, now.level, want.level);
        set (CommandType::SetPan, t, 0, now.pan, want.pan);
        set (CommandType::SetMute, t, 0, now.mute ? 1.0f : 0.0f, want.mute ? 1.0f : 0.0f);
        set (CommandType::SetSolo, t, 0, now.solo ? 1.0f : 0.0f, want.solo ? 1.0f : 0.0f);
        for (int i = 0; i < kNumFxParams; ++i)
            set (CommandType::SetFxParam, t, i, now.fx[(size_t) i], want.fx[(size_t) i]);
    }
    for (int i = 0; i < kNumFxParams; ++i)
        set (CommandType::SetFxParam, -1, i, state.masterFx[(size_t) i], target.masterFx[(size_t) i]);
    set (CommandType::SetMasterLevel, 0, 0, state.masterLevel, target.masterLevel);
}

void DrumMachineView::resetToDefaults()
{
    // Puts every drum (voice, character, controls, level, pan, mute, solo),
    // every effects rack and the drum bus back to how the app starts. The
    // sequences, loaded samples and the studio's tempo stay as they are.
    const auto& state = proc.getUiState();
    UndoSnapshot before;
    before.hasSettings = true;
    before.settings = state;

    applySettings (makeDefaultProject());
    lastUndo = before;
    panel->refresh();
    setStatus ("RESET ALL SETTINGS / SEQUENCES KEPT / CTRL+Z TO UNDO");
}

void DrumMachineView::undo()
{
    if (! lastUndo.has_value())
    {
        setStatus ("NOTHING TO UNDO");
        return;
    }
    const auto& snap = *lastUndo;
    const auto& state = proc.getUiState();
    for (int t = 0; t < kNumTracks; ++t)
    {
        const auto& now = state.tracks[(size_t) t].patterns[(size_t) snap.pattern];
        for (int s = 0; s < kNumSteps && ! snap.hasSettings; ++s)
            if (! juce::exactlyEqual (now[(size_t) s], snap.steps[(size_t) t][(size_t) s]))
                send (CommandType::SetStep, t, snap.pattern * kNumSteps + s, snap.steps[(size_t) t][(size_t) s]);
        if (snap.hasFx)
            for (int i = 0; i < kNumFxParams; ++i)
                send (CommandType::SetFxParam, t, i, snap.fx[(size_t) t][(size_t) i]);
    }
    if (snap.hasFx)
        for (int i = 0; i < kNumFxParams; ++i)
            send (CommandType::SetFxParam, -1, i, snap.masterFx[(size_t) i]);
    if (snap.hasSettings)
    {
        applySettings (snap.settings);
        panel->refresh();
        setStatus ("UNDONE / SETTINGS BEFORE THE RESET RESTORED");
    }
    else
        setStatus ("UNDONE / P" + juce::String (snap.pattern + 1) + (snap.hasFx ? " AND THE EFFECTS RESTORED" : " RESTORED"));
    lastUndo.reset();
}

void DrumMachineView::copySequence (bool wholePattern)
{
    const auto& state = proc.getUiState();
    const int pattern = viewedPattern (state);
    SequenceClipboard clip;
    clip.wholePattern = wholePattern || selected < 0;
    if (clip.wholePattern)
    {
        for (int t = 0; t < kNumTracks; ++t)
            clip.steps[(size_t) t] = state.tracks[(size_t) t].patterns[(size_t) pattern];
        clip.source = "P" + juce::String (pattern + 1);
    }
    else
    {
        clip.steps[0] = state.tracks[(size_t) selected].patterns[(size_t) pattern];
        clip.source = look::pad (selected + 1, 2) + " " + proc.trackName (selected) + " P" + juce::String (pattern + 1);
    }
    clipboard = clip;
    setStatus ("COPIED " + clip.source + (clip.wholePattern ? " (ALL TRACKS)" : "")
               + " / PICK A " + (clip.wholePattern ? "PATTERN (P1-P4)" : "DRUM OR PATTERN") + " AND PASTE (CTRL+V)");
}

void DrumMachineView::pasteSequence()
{
    if (! clipboard.has_value())
    {
        setStatus ("NOTHING COPIED YET / SELECT A TRACK AND PRESS COPY (CTRL+C)");
        return;
    }
    const int pattern = viewedPattern (proc.getUiState());
    const auto& clip = *clipboard;
    lastUndo = takeSnapshot (pattern, false);

    auto write = [this, pattern] (int track, const Pattern& steps) {
        const auto& now = proc.getUiState().tracks[(size_t) track].patterns[(size_t) pattern];
        for (int s = 0; s < kNumSteps; ++s)
            if (! juce::exactlyEqual (now[(size_t) s], steps[(size_t) s]))
                send (CommandType::SetStep, track, pattern * kNumSteps + s, steps[(size_t) s]);
    };

    if (clip.wholePattern)
    {
        for (int t = 0; t < kNumTracks; ++t)
            write (t, clip.steps[(size_t) t]);
        setStatus ("PASTED " + clip.source + " INTO P" + juce::String (pattern + 1) + " / CTRL+Z TO UNDO");
    }
    else if (selected >= 0)
    {
        write (selected, clip.steps[0]);
        setStatus ("PASTED " + clip.source + " INTO " + look::pad (selected + 1, 2) + " " + proc.trackName (selected)
                   + " P" + juce::String (pattern + 1) + " / CTRL+Z TO UNDO");
    }
    else
    {
        lastUndo.reset();
        setStatus ("SELECT A TRACK TO PASTE ONE SEQUENCE INTO");
    }
}

void DrumMachineView::randomizeSequence (int track)
{
    if (track < 0 || track >= kNumTracks)
        return;
    const auto& state = proc.getUiState();
    const int pattern = viewedPattern (state);
    const auto voice = state.tracks[(size_t) track].voice;
    lastUndo = takeSnapshot (pattern, false);

    // Chance of a hit on each step, shaped by what the drum is so the result
    // sits in a groove: kicks lean on the beat, hats run, snares find the
    // backbeat, everything else is sparse.
    auto chance = [voice] (int s) {
        const bool beat = s % 4 == 0, eighth = s % 2 == 0;
        switch (voice)
        {
            case VoiceType::Kick:       return s == 0 ? 1.0f : beat ? 0.55f : eighth ? 0.2f : 0.12f;
            case VoiceType::ClickHat:   return eighth ? 0.8f : 0.45f;
            case VoiceType::NoiseSnare: return (s == 4 || s == 12) ? 0.8f : beat ? 0.1f : 0.14f;
            case VoiceType::SineTone:
            case VoiceType::Glitch:
            case VoiceType::Sample:
            case VoiceType::Count:      break;
        }
        return beat ? 0.3f : 0.2f;
    };

    auto& rng = juce::Random::getSystemRandom();
    Pattern steps {};
    int hits = 0;
    while (hits == 0)
    {
        for (int s = 0; s < kNumSteps; ++s)
        {
            steps[(size_t) s] = 0.0f;
            if (rng.nextFloat() < chance (s))
            {
                const float accent = s % 4 == 0 ? 0.25f : 0.0f;
                steps[(size_t) s] = juce::jlimit (0.3f, 1.0f, 0.45f + accent + rng.nextFloat() * 0.45f);
                ++hits;
            }
        }
    }

    for (int s = 0; s < kNumSteps; ++s)
        send (CommandType::SetStep, track, pattern * kNumSteps + s, steps[(size_t) s]);
    setStatus ("RANDOM " + look::pad (track + 1, 2) + " " + proc.trackName (track) + " P" + juce::String (pattern + 1)
               + " / PRESS AGAIN FOR ANOTHER / CTRL+Z TO UNDO");
}

bool DrumMachineView::keyStateChanged (bool)
{
    for (int t = 0; t < kNumTracks; ++t)
        if (padKeyHeld[(size_t) t]
            && ! juce::KeyPress::isKeyCurrentlyDown (kPadKeys[t][0])
            && ! juce::KeyPress::isKeyCurrentlyDown (kPadKeys[t][1])
            && ! juce::KeyPress::isKeyCurrentlyDown (juce::CharacterFunctions::toUpperCase ((juce::juce_wchar) kPadKeys[t][1])))
            padKeyHeld[(size_t) t] = false;
    return false;
}


bool DrumMachineView::keyPressed (const juce::KeyPress& key)
{
    const auto mods = key.getModifiers();
    const auto c = (juce::juce_wchar) juce::CharacterFunctions::toLowerCase (key.getTextCharacter());
    auto unhandled = [this, &key] { return onUnhandledKey != nullptr && onUnhandledKey (key); };

    if (mods.isCommandDown())
    {
        if (key.getKeyCode() == 'Z') { undo(); return true; }
        if (key.getKeyCode() == 'C') { copySequence (mods.isShiftDown()); return true; }
        if (key.getKeyCode() == 'V') { pasteSequence(); return true; }
        if (key.getKeyCode() == 'R') { randomizeSequence (selected); return true; }
        return unhandled();
    }

    for (int p = 0; p < kNumPatterns; ++p)
    {
        if (key.getKeyCode() == juce::KeyPress::F1Key + p)
        {
            if (mods.isShiftDown())
                copyPatternTo (p);
            else
                selectPattern (p);
            return true;
        }
    }

    if (key == juce::KeyPress::deleteKey || key == juce::KeyPress::backspaceKey) { clearPattern(); return true; }
    if (key == juce::KeyPress::upKey) { selectTrack (selected < 0 ? kNumTracks - 1 : juce::jmax (0, selected - 1)); return true; }
    if (key == juce::KeyPress::downKey) { selectTrack (selected < 0 || selected >= kNumTracks - 1 ? -1 : selected + 1); return true; }

    for (int t = 0; t < kNumTracks; ++t)
    {
        if (c == (juce::juce_wchar) kPadKeys[t][0] || c == (juce::juce_wchar) kPadKeys[t][1])
        {
            // Ignore auto-repeat: only the first press of a held key hits.
            const auto now = juce::Time::getMillisecondCounter();
            if (now - lastPadKeyTime[(size_t) t] > 60 || ! padKeyHeld[(size_t) t])
                hitPad (t, mods.isShiftDown() ? 0.5f : 1.0f);
            padKeyHeld[(size_t) t] = true;
            lastPadKeyTime[(size_t) t] = now;
            return true;
        }
    }
    return unhandled();
}

DrumMachineView::DrumMachineView (ModularSynthProcessor& p) : proc (p)
{
    grid = std::make_unique<TrackGrid> (*this);
    panel = std::make_unique<ParamPanel> (*this);
    addAndMakeVisible (*grid);
    addAndMakeVisible (*panel);

    for (auto* b : { &clearButton, &resetButton })
        addAndMakeVisible (*b);
    clearButton.onClick = [this] { clearPattern(); };
    resetButton.onClick = [this] { resetToDefaults(); };
    for (int i = 0; i < kNumPatterns; ++i)
    {
        auto& b = patternButtons[(size_t) i];
        b.setText ("P" + juce::String (i + 1));
        addAndMakeVisible (b);
        b.onClick = [this, i] {
            if (juce::ModifierKeys::getCurrentModifiers().isShiftDown())
                copyPatternTo (i);
            else
                selectPattern (i);
        };
    }

    addAndMakeVisible (stepReadout);
    addAndMakeVisible (busReadout);
    stepReadout.setLabel ("PATTERN / STEP");
    busReadout.setLabel ("DRUM BUS");

    lastFolder = juce::File::getSpecialLocation (juce::File::userMusicDirectory).getChildFile ("Modular-synth");

    setWantsKeyboardFocus (true);
    setSize (1320, 820);
    panel->refresh();
}

DrumMachineView::~DrumMachineView() = default;

void DrumMachineView::tick()
{
    const auto& state = proc.getUiState();
    for (int p = 0; p < kNumPatterns; ++p)
    {
        patternButtons[(size_t) p].setOn (state.pattern == p);
        patternButtons[(size_t) p].setMarked (state.queuedPattern == p);
    }

    stepReadout.setText ("P" + juce::String (state.pattern + 1) + (state.queuedPattern >= 0 ? ">P" + juce::String (state.queuedPattern + 1) : juce::String())
                         + "  " + look::pad (proc.currentStep() + 1, 2) + "/" + juce::String (kNumSteps));
    busReadout.setText (formatDb (levelToGain (state.masterLevel)));

    grid->tick();
    panel->refresh();
    repaint (0, getHeight() - kStatusHeight, getWidth(), kStatusHeight);
}

void DrumMachineView::paint (juce::Graphics& g)
{
    g.fillAll (look::background);

    g.setColour (look::ink);
    g.setFont (look::mono (16.0f, true));
    g.drawText ("01 DRUM MACHINE", kMargin, 10, 300, 20, juce::Justification::centredLeft);
    g.setFont (look::mono (9.5f));
    g.setColour (look::dim);
    g.drawText ("DATAMATICS KIT // 8 TRACKS x 16 STEPS x 4 PATTERNS", kMargin, 30, 360, 14, juce::Justification::centredLeft);

    g.setColour (look::faint);
    g.fillRect (0, kHeaderHeight - 1, getWidth(), 1);

    const auto statusArea = juce::Rectangle<int> (kMargin, getHeight() - kStatusHeight, getWidth() - 2 * kMargin, kStatusHeight);
    g.setColour (look::faint);
    g.fillRect (0, statusArea.getY(), getWidth(), 1);
    g.setFont (look::mono (10.0f));
    const bool fresh = status.isNotEmpty() && juce::Time::getMillisecondCounter() - statusTime < 12000;
    g.setColour (proc.isRecording() ? look::red : look::ink);
    g.drawText (fresh ? status : juce::String ("READY"), statusArea, juce::Justification::centredLeft, true);
    g.setColour (look::dim);
    g.drawText ("SPACE PLAY  R REC  E EXPORT  1-8 / A-K PADS  F1-F4 PATTERN  CTRL+C/V COPY/PASTE (+SHIFT = WHOLE PATTERN)  CTRL+R RANDOM  DEL CLEAR  DRAG STEP = VELOCITY  RIGHT-CLICK NAME = VOICE",
                statusArea, juce::Justification::centredRight, true);
}

void DrumMachineView::resized()
{
    auto area = getLocalBounds();
    auto header = area.removeFromTop (kHeaderHeight).reduced (kMargin, 0);

    auto buttons = header.removeFromBottom (30).withTrimmedBottom (8);
    for (auto& b : patternButtons)
    {
        b.setBounds (buttons.removeFromLeft (44));
        buttons.removeFromLeft (4);
    }
    buttons.removeFromLeft (8);
    clearButton.setBounds (buttons.removeFromLeft (72));
    buttons.removeFromLeft (6);
    resetButton.setBounds (buttons.removeFromLeft (72));

    auto readouts = header.withTrimmedTop (8).withTrimmedLeft (juce::jmax (380, header.getWidth() - 260));
    stepReadout.setBounds (readouts.removeFromLeft (120).withHeight (36));
    readouts.removeFromLeft (8);
    busReadout.setBounds (readouts.removeFromLeft (110).withHeight (36));

    area.removeFromBottom (kStatusHeight);
    panel->setBounds (area.removeFromBottom (kPanelHeight).reduced (kMargin, 0));
    grid->setBounds (area.reduced (kMargin, 6));
}
