#include "PluginEditor.h"

using namespace ms;

namespace
{
constexpr int kHeaderHeight = 84;
constexpr int kPanelHeight = 178;
constexpr int kStatusHeight = 24;
constexpr int kMargin = 16;

// Keys that play pads 1-8: number row and home row.
constexpr char kPadKeys[kNumTracks][2] = { { '1', 'a' }, { '2', 's' }, { '3', 'd' }, { '4', 'f' },
                                          { '5', 'g' }, { '6', 'h' }, { '7', 'j' }, { '8', 'k' } };

juce::String formatTime (double seconds)
{
    const int totalMs = (int) std::round (seconds * 1000.0);
    return look::pad (totalMs / 60000, 2) + ":" + look::pad ((totalMs / 1000) % 60, 2) + "." + look::pad (totalMs % 1000, 3);
}

juce::String formatDb (float gain)
{
    const float db = gainToDb (gain);
    return db <= -99.0f ? juce::String ("-INF dB") : juce::String (db, 1) + " dB";
}

juce::String formatPan (float v)
{
    const int p = juce::roundToInt (panToBalance (v) * 100.0f);
    if (p == 0)
        return "C";
    return (p < 0 ? "L " : "R ") + juce::String (std::abs (p));
}

class DatamaticsLookAndFeel : public juce::LookAndFeel_V4
{
public:
    DatamaticsLookAndFeel()
        : LookAndFeel_V4 (juce::LookAndFeel_V4::ColourScheme (
            look::background, look::background, look::background, look::dim, look::ink, look::faint,
            look::background, look::ink, look::ink))
    {
        setColour (juce::PopupMenu::backgroundColourId, look::background);
        setColour (juce::PopupMenu::textColourId, look::ink);
        setColour (juce::PopupMenu::highlightedBackgroundColourId, look::ink);
        setColour (juce::PopupMenu::highlightedTextColourId, look::background);
        setColour (juce::ResizableWindow::backgroundColourId, look::background);
        setColour (juce::TextButton::buttonColourId, look::background);
        setColour (juce::ComboBox::backgroundColourId, look::background);
        setColour (juce::ComboBox::outlineColourId, look::dim);
    }

    juce::Font getPopupMenuFont() override { return look::mono (13.0f); }
    juce::Font getTextButtonFont (juce::TextButton&, int) override { return look::mono (12.0f, true); }
    juce::Font getLabelFont (juce::Label&) override { return look::mono (12.0f); }
    juce::Font getComboBoxFont (juce::ComboBox&) override { return look::mono (12.0f); }
};

DatamaticsLookAndFeel& sharedLookAndFeel()
{
    static DatamaticsLookAndFeel laf;
    return laf;
}
} // namespace

// =============================================================================
// Track grid: 8 tracks as horizontal data rows, plus the master row.

class TrackGrid : public juce::Component
{
public:
    explicit TrackGrid (ModularSynthEditor& e) : editor (e) {}

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
        const bool playing = proc.isPlaying();
        const int playStep = proc.currentStep();

        for (int t = 0; t < kNumTracks; ++t)
            paintTrack (g, geo, t, state.tracks[(size_t) t], playing ? playStep : -1);

        paintMaster (g, geo, state);

        // step ruler along the top: step numbers, beats brighter
        g.setFont (look::mono (9.0f));
        for (int s = 0; s < kNumSteps; ++s)
        {
            g.setColour (s % 4 == 0 ? look::ink.withAlpha (0.7f) : look::dim);
            g.drawText (look::pad (s + 1, 2), juce::Rectangle<float> (geo.stepX (s), 0.0f, geo.stepW, (float) kRulerHeight),
                        juce::Justification::centred);
        }
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
            dragStartVelocity = tr.steps[(size_t) s];
            if (dragStartVelocity > 0.0f)
            {
                dragMode = DragMode::PendingOff;
            }
            else
            {
                dragMode = DragMode::PaintOn;
                editor.send (CommandType::SetStep, t, s, 1.0f);
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
            editor.send (CommandType::SetStep, dragTrack, dragStep, v);
        }
        else if (dragMode == DragMode::PaintOn)
        {
            const int s = geo.stepAt (e.position.x);
            if (s >= 0 && editor.processor().getUiState().tracks[(size_t) dragTrack].steps[(size_t) s] <= 0.0f)
                editor.send (CommandType::SetStep, dragTrack, s, 1.0f);
        }
    }

    void mouseUp (const juce::MouseEvent&) override
    {
        if (dragMode == DragMode::PendingOff)
            editor.send (CommandType::SetStep, dragTrack, dragStep, 0.0f);
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

    // An active step drawn as a small barcode: more and longer bars = harder hit.
    // The bar pattern is fixed per cell so it reads as data, not noise.
    static void paintStepGlyph (juce::Graphics& g, juce::Rectangle<float> cell, float velocity, int seed, bool now, float alpha)
    {
        if (now)
        {
            g.setColour (look::ink.withAlpha (alpha));
            g.fillRect (cell);
            return;
        }

        const float mid = cell.getCentreY();
        const float barH = juce::jmax (3.0f, cell.getHeight() * (0.25f + 0.75f * velocity));
        const int bars = 2 + juce::roundToInt (velocity * 6.0f);
        auto rng = (juce::uint32) seed * 2654435761u + 12345u;
        float x = cell.getX();
        g.setColour (look::ink.withAlpha (alpha * 0.92f));
        for (int i = 0; i < bars && x < cell.getRight(); ++i)
        {
            rng = rng * 1664525u + 1013904223u;
            const float w = 1.0f + (float) ((rng >> 24) % 3u);
            g.fillRect (x, mid - barH * 0.5f, juce::jmin (w, cell.getRight() - x), barH);
            x += w + 1.0f + (float) ((rng >> 16) % 3u);
        }
        g.fillRect (cell.getX(), mid - 0.5f, cell.getWidth(), 1.0f);
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

    void paintTrack (juce::Graphics& g, const Geometry& geo, int t, const TrackState& tr, int playStep)
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
        formatVoiceParam (tr.voice, 0, tr.voiceParams[0], first, (int) sizeof (first));
        juce::String sub = juce::String (first) + " / " + formatDb (levelToGain (tr.level));
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

            const float v = tr.steps[(size_t) s];
            if (v > 0.0f)
                paintStepGlyph (g, { x + 3.0f, y0 + 7.0f, geo.stepW - 6.0f, h - 16.0f }, v, t * 31 + s, s == playStep, alpha);
        }

        // barcode: the track's signal over time, newest on the right
        const auto bx = geo.barcodeX, bw = geo.barcodeW;
        const int columns = (int) bw;
        float lastPeak = 0.0f;
        for (int c = 0; c < columns; ++c)
        {
            const float level = proc.trackHistory (t, c);
            if (c < 8)
                lastPeak = juce::jmax (lastPeak, level);
            if (level < 0.002f)
                continue;
            const float x = bx + bw - 1.0f - (float) c;
            const float strength = juce::jlimit (0.0f, 1.0f, std::sqrt (level));
            g.setColour (level > 1.0f ? look::red : look::ink.withAlpha (alpha * juce::jmax (0.3f, strength)));
            const float barH = h - 12.0f;
            g.fillRect (x, y0 + 6.0f + (1.0f - strength) * barH * 0.5f, level > 0.25f ? 2.0f : 1.0f, barH * (0.35f + strength * 0.65f));
        }
        g.setColour (look::faint);
        g.drawRect (juce::Rectangle<float> (bx - 1.0f, y0 + 5.0f, bw + 2.0f, h - 10.0f), 1.0f);
        g.setFont (look::mono (9.0f));
        g.setColour (lastPeak > 1.0f ? look::red : look::dim);
        g.drawText (formatDb (lastPeak), juce::Rectangle<float> (bx - 66.0f, y0 + 4.0f, 60.0f, 12.0f), juce::Justification::topRight);

        paintToggle (g, geo.muteX, mid - 9.0f, geo.toggleW, "M", tr.mute, selected);
        paintToggle (g, geo.soloX, mid - 9.0f, geo.toggleW, "S", tr.solo, selected);
    }

    void paintMaster (juce::Graphics& g, const Geometry& geo, const ProjectState& state)
    {
        auto& proc = editor.processor();
        const float y0 = geo.rowY (kNumTracks), h = geo.rowH, mid = y0 + h * 0.5f;
        const bool selected = editor.selectedTrack() < 0;

        g.setColour (look::dim);
        g.fillRect (0.0f, y0, geo.width, 1.0f);
        paintIndex (g, { 4.0f, y0 + 6.0f, 30.0f, h - 12.0f }, "MS", 0.0f, selected);

        if (selected)
        {
            g.setColour (look::ink);
            g.fillRect (geo.nameX, y0 + 8.0f, 2.0f, h - 16.0f);
        }
        g.setFont (look::mono (13.0f, true));
        g.setColour (look::ink);
        g.drawText ("MASTER", juce::Rectangle<float> (geo.nameX + 10.0f, y0 + 4.0f, 120.0f, h * 0.5f - 2.0f), juce::Justification::bottomLeft);
        g.setFont (look::mono (9.5f));
        g.setColour (look::dim);
        g.drawText ("SUM + RACK " + formatDb (levelToGain (state.masterLevel)),
                    juce::Rectangle<float> (geo.nameX + 10.0f, mid + 2.0f, geo.stepsX - geo.nameX - 14.0f, h * 0.5f - 6.0f),
                    juce::Justification::topLeft, true);

        // effects summary as a raw data line
        juce::String data;
        for (int i = 0; i < kNumFxParams; ++i)
        {
            char buf[32];
            formatFxParam (i, state.masterFx[(size_t) i], buf, (int) sizeof (buf));
            data << fxParamName (i) << " " << buf << "   ";
        }
        g.setFont (look::mono (9.5f));
        g.setColour (look::dim);
        g.drawFittedText (data, juce::Rectangle<float> (geo.stepsX, y0 + 6.0f, geo.stepX (kNumSteps) - geo.stepsX, h - 12.0f).toNearestInt(),
                          juce::Justification::centredLeft, 3, 1.0f);

        // master peak meter: horizontal bar, red past 0 dB
        const float peak = proc.masterPeak();
        masterHold = juce::jmax (peak, masterHold * 0.97f);
        const float meterW = geo.barcodeW;
        const float x0 = geo.barcodeX;
        const float fill = juce::jlimit (0.0f, 1.0f, std::sqrt (peak) / std::sqrt (1.4f)) * meterW;
        const float hold = juce::jlimit (0.0f, 1.0f, std::sqrt (masterHold) / std::sqrt (1.4f)) * meterW;
        const float zeroDb = std::sqrt (1.0f) / std::sqrt (1.4f) * meterW;
        g.setColour (look::faint);
        g.fillRect (x0, mid, meterW, 1.0f);
        g.setColour (peak > 1.0f ? look::red : look::ink);
        g.fillRect (x0, mid - 5.0f, fill, 10.0f);
        g.setColour (masterHold > 1.0f ? look::red : look::ink);
        g.fillRect (x0 + hold, mid - 9.0f, 1.0f, 18.0f);
        g.setColour (look::red.withAlpha (0.8f));
        g.fillRect (x0 + zeroDb, mid - 12.0f, 1.0f, 24.0f);
        g.setFont (look::mono (9.0f));
        g.setColour (masterHold > 1.0f ? look::red : look::dim);
        g.drawText ("PEAK " + formatDb (masterHold), juce::Rectangle<float> (x0, y0 + 4.0f, 160.0f, 12.0f), juce::Justification::topLeft);
        g.setColour (look::dim);
        g.drawText ("0 dB", juce::Rectangle<float> (x0 + zeroDb + 4.0f, y0 + h - 16.0f, 50.0f, 12.0f), juce::Justification::topLeft);
    }

    enum class DragMode
    {
        None,
        PaintOn,
        PendingOff,
        Velocity
    };

    ModularSynthEditor& editor;
    std::array<int, kNumTracks> lastHits {};
    std::array<float, kNumTracks> flash {};
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
    explicit ParamPanel (ModularSynthEditor& e) : editor (e)
    {
        addAndMakeVisible (typeButton);
        typeButton.onClick = [this] {
            if (editor.selectedTrack() >= 0)
                editor.showVoiceMenu (editor.selectedTrack());
        };

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

        const auto defaults = defaultFxParams();
        for (int i = 0; i < kNumFxParams; ++i)
        {
            auto& s = fx[(size_t) i];
            addAndMakeVisible (s);
            s.setLabel (fxParamName (i));
            s.setDefault (defaults[(size_t) i]);
            s.format = [i] (float v) {
                char buf[32];
                formatFxParam (i, v, buf, (int) sizeof (buf));
                return juce::String (buf);
            };
            s.onChange = [this, i] (float v) { editor.send (CommandType::SetFxParam, editor.selectedTrack(), i, v); };
        }
    }

    void refresh()
    {
        const int t = editor.selectedTrack();
        const auto& state = editor.processor().getUiState();
        const bool master = t < 0;
        const auto voiceType = master ? VoiceType::Count : state.tracks[(size_t) t].voice;

        if (t != shownTrack || voiceType != shownVoice)
        {
            shownTrack = t;
            shownVoice = voiceType;
            typeButton.setVisible (! master);
            pan.setVisible (! master);
            level.setDefault (master ? ProjectState().masterLevel : TrackState().level);

            const auto defaults = master ? VoiceParams {} : defaultVoiceParams (voiceType);
            for (int i = 0; i < kNumVoiceParams; ++i)
            {
                auto& s = voice[(size_t) i];
                const char* name = master ? nullptr : voiceParamName (voiceType, i);
                s.setVisible (name != nullptr);
                if (name == nullptr)
                    continue;
                s.setLabel (name);
                s.setDefault (defaults[(size_t) i]);
                s.format = [voiceType, i] (float v) {
                    char buf[32];
                    formatVoiceParam (voiceType, i, v, buf, (int) sizeof (buf));
                    return juce::String (buf);
                };
                s.repaint();
            }
            if (! master)
                typeButton.setText ("TYPE: " + juce::String (voiceTypeName (voiceType)));
            repaint();
        }

        if (master)
        {
            level.setValue (state.masterLevel);
            for (int i = 0; i < kNumFxParams; ++i)
                fx[(size_t) i].setValue (state.masterFx[(size_t) i]);
            return;
        }

        const auto& tr = state.tracks[(size_t) t];
        for (int i = 0; i < kNumVoiceParams; ++i)
            voice[(size_t) i].setValue (tr.voiceParams[(size_t) i]);
        level.setValue (tr.level);
        pan.setValue (tr.pan);
        for (int i = 0; i < kNumFxParams; ++i)
            fx[(size_t) i].setValue (tr.fx[(size_t) i]);
    }

    void paint (juce::Graphics& g) override
    {
        g.setColour (look::dim);
        g.fillRect (0, 0, getWidth(), 1);

        const int t = editor.selectedTrack();
        const auto title = t < 0 ? juce::String ("MS MASTER // MIX BUS + EFFECTS RACK")
                                 : look::pad (t + 1, 2) + " " + editor.processor().trackName (t) + " // PARAMETERS";
        g.setFont (look::mono (11.0f, true));
        g.setColour (look::ink);
        g.drawText (title, 0, 6, getWidth() / 2, 18, juce::Justification::centredLeft);

        g.setFont (look::mono (9.5f));
        g.setColour (look::dim);
        const char* groups[] = { "VOICE", "MIX", "CRUSH + FILTER", "DELAY + REVERB" };
        for (int c = 0; c < 4; ++c)
            g.drawText (groups[c], columnX (c), 30, 200, 14, juce::Justification::centredLeft);
        g.drawText ("DRAG / SHIFT FINE / DOUBLE-CLICK RESET", getWidth() / 2, 6, getWidth() / 2, 18, juce::Justification::centredRight);
    }

    void resized() override
    {
        const int rowH = 24, y0 = 48;
        const int colW = columnX (1) - columnX (0) - 24;

        typeButton.setBounds (columnX (1), 4, 140, 20);

        for (int i = 0; i < kNumVoiceParams; ++i)
            voice[(size_t) i].setBounds (columnX (0), y0 + i * rowH, colW, rowH);

        level.setBounds (columnX (1), y0, colW, rowH);
        pan.setBounds (columnX (1), y0 + rowH, colW, rowH);

        const int c2[] = { fxCrushBits, fxCrushRate, fxCutoff, fxResonance };
        for (int i = 0; i < 4; ++i)
            fx[(size_t) c2[i]].setBounds (columnX (2), y0 + i * rowH, colW, rowH);

        const int c3[] = { fxDelayTime, fxDelayFeedback, fxDelayMix, fxReverbSize, fxReverbMix };
        for (int i = 0; i < 5; ++i)
            fx[(size_t) c3[i]].setBounds (columnX (3), y0 + i * rowH, colW, rowH);
    }

private:
    int columnX (int c) const { return c * (getWidth() + 24) / 4; }

    ModularSynthEditor& editor;
    DataButton typeButton { "TYPE" };
    std::array<DataSlider, kNumVoiceParams> voice;
    DataSlider level, pan;
    std::array<DataSlider, kNumFxParams> fx;
    int shownTrack = -2;
    VoiceType shownVoice = VoiceType::Count;
};

// =============================================================================
// Export runs off the message thread so the UI keeps drawing.

namespace
{
class ExportThread : public juce::Thread
{
public:
    ExportThread (Take t, ExportOptions o, std::atomic<float>& progressOut,
                  std::function<void (ExportResult)> done)
        : Thread ("Export"), take (std::move (t)), options (std::move (o)), progress (progressOut), onDone (std::move (done))
    {
    }

    void run() override
    {
        auto result = exportTake (take, options, [this] (float p) {
            progress = p;
            return ! threadShouldExit();
        });
        progress = -1.0f;
        juce::MessageManager::callAsync ([cb = onDone, result] { cb (result); });
    }

private:
    Take take;
    ExportOptions options;
    std::atomic<float>& progress;
    std::function<void (ExportResult)> onDone;
};
} // namespace

// =============================================================================

ModularSynthEditor::ModularSynthEditor (ModularSynthProcessor& p) : AudioProcessorEditor (&p), proc (p)
{
    juce::LookAndFeel::setDefaultLookAndFeel (&sharedLookAndFeel());
    setLookAndFeel (&sharedLookAndFeel());

    grid = std::make_unique<TrackGrid> (*this);
    panel = std::make_unique<ParamPanel> (*this);
    addAndMakeVisible (*grid);
    addAndMakeVisible (*panel);

    for (auto* b : { &playButton, &stopButton, &recButton, &exportButton, &saveButton, &openButton })
        addAndMakeVisible (*b);
    recButton.setAccent (look::red);

    playButton.onClick = [this] { togglePlay(); };
    stopButton.onClick = [this] { stop(); };
    recButton.onClick = [this] { toggleRecord(); };
    exportButton.onClick = [this] { chooseExport(); };
    saveButton.onClick = [this] { saveProject(); };
    openButton.onClick = [this] { openProject(); };

    for (auto* r : { &bpmReadout, &swingReadout, &stepReadout, &posReadout, &rateReadout, &takeReadout })
        addAndMakeVisible (*r);

    bpmReadout.setLabel ("BPM / DRAG");
    bpmReadout.onDrag = [this] (float px, bool fine) {
        send (CommandType::SetBpm, 0, 0, proc.getUiState().bpm + px * (fine ? 0.1f : 0.5f));
    };
    bpmReadout.onDoubleClick = [this] { send (CommandType::SetBpm, 0, 0, 132.0f); };

    swingReadout.setLabel ("SWING / DRAG");
    swingReadout.onDrag = [this] (float px, bool fine) {
        send (CommandType::SetSwing, 0, 0, proc.getUiState().swing + px * (fine ? 0.002f : 0.01f));
    };
    swingReadout.onDoubleClick = [this] { send (CommandType::SetSwing, 0, 0, 0.0f); };

    stepReadout.setLabel ("STEP");
    posReadout.setLabel ("POSITION");
    rateReadout.setLabel ("SAMPLE RATE");
    takeReadout.setLabel ("TAKE");

    lastFolder = juce::File::getSpecialLocation (juce::File::userMusicDirectory).getChildFile ("Modular-synth");

    setWantsKeyboardFocus (true);
    setResizable (true, true);
    setResizeLimits (980, 640, 2600, 1700);
    setSize (1240, 800);
    panel->refresh();
    startTimerHz (60);
}

ModularSynthEditor::~ModularSynthEditor()
{
    stopTimer();
    if (exportThread != nullptr)
        exportThread->stopThread (15000);
    setLookAndFeel (nullptr);
    juce::LookAndFeel::setDefaultLookAndFeel (nullptr);
}

void ModularSynthEditor::send (CommandType type, int track, int index, float value, const SampleData* sample)
{
    Command c;
    c.type = type;
    c.track = track;
    c.index = index;
    c.value = value;
    c.sample = sample;
    proc.send (c);
}

void ModularSynthEditor::selectTrack (int track)
{
    if (selected != track)
    {
        selected = track;
        panel->refresh();
        panel->repaint();
    }
}

void ModularSynthEditor::setStatus (const juce::String& s)
{
    status = s;
    statusTime = juce::Time::getMillisecondCounter();
    repaint (0, getHeight() - kStatusHeight, getWidth(), kStatusHeight);
}

void ModularSynthEditor::hitPad (int track, float velocity)
{
    send (CommandType::Trigger, track, 0, velocity);
}

void ModularSynthEditor::showVoiceMenu (int track)
{
    juce::PopupMenu m;
    m.addSectionHeader (look::pad (track + 1, 2) + " VOICE");
    const auto current = proc.getUiState().tracks[(size_t) track].voice;
    for (int v = 0; v < kNumVoiceTypes; ++v)
        if ((VoiceType) v != VoiceType::Sample)
            m.addItem (1 + v, voiceTypeName ((VoiceType) v), true, current == (VoiceType) v);
    m.addSeparator();
    m.addItem (100, "LOAD SAMPLE...");

    m.showMenuAsync (juce::PopupMenu::Options().withMousePosition(), [this, track] (int r) {
        if (r == 100)
            loadSampleInto (track);
        else if (r > 0)
            send (CommandType::SetVoiceType, track, r - 1, 0.0f);
        panel->refresh();
    });
}

void ModularSynthEditor::loadSampleInto (int track)
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

void ModularSynthEditor::togglePlay()
{
    send (proc.isPlaying() ? CommandType::Stop : CommandType::Play, 0, 0, 0.0f);
}

void ModularSynthEditor::stop()
{
    send (CommandType::Stop, 0, 0, 0.0f);
}

void ModularSynthEditor::toggleRecord()
{
    if (proc.isRecording())
    {
        send (CommandType::RecordStop, 0, 0, 0.0f);
    }
    else
    {
        send (CommandType::RecordStart, 0, 0, 0.0f);
        setStatus ("RECORDING / PLAY PADS, MUTE, MOVE ANYTHING / R OR STOP TO END");
    }
}

void ModularSynthEditor::chooseExport()
{
    if (exportThread != nullptr && exportThread->isThreadRunning())
    {
        setStatus ("AN EXPORT IS ALREADY RUNNING");
        return;
    }

    juce::PopupMenu m;
    m.addSectionHeader ("EXPORT WAV // MASTER + STEMS");
    const auto& take = proc.getLastTake();
    if (take.has_value())
        m.addItem (1, "RECORDED TAKE  " + formatTime ((double) take->length / take->sampleRate));
    else
        m.addItem (1, "RECORDED TAKE  (PRESS REC FIRST)", false);
    m.addSeparator();
    for (int loops : { 1, 4, 8, 16 })
        m.addItem (100 + loops, "PATTERN x" + juce::String (loops));

    m.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (&exportButton), [this] (int r) {
        if (r <= 0)
            return;

        const auto stamp = juce::Time::getCurrentTime().formatted ("%Y%m%d-%H%M%S");
        if (r == 1 && proc.getLastTake().has_value())
            startExport (*proc.getLastTake(), "take-" + stamp);
        else if (r > 100)
            startExport (makePatternTake (proc.getUiState(), r - 100, proc.getEngineSampleRate()),
                         "pattern-x" + juce::String (r - 100) + "-" + stamp);
    });
}

void ModularSynthEditor::startExport (Take take, const juce::String& baseName)
{
    lastFolder.createDirectory();
    chooser = std::make_unique<juce::FileChooser> ("Choose where to save the WAV files", lastFolder);
    chooser->launchAsync (juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectDirectories,
                          [this, take = std::move (take), baseName] (const juce::FileChooser& fc) mutable {
                              const auto dir = fc.getResult();
                              if (dir == juce::File())
                                  return;
                              lastFolder = dir;

                              ExportOptions opt;
                              opt.folder = dir.getChildFile (baseName);
                              opt.baseName = baseName;
                              opt.bitDepth = 24;
                              for (int t = 0; t < kNumTracks; ++t)
                                  opt.trackNames[(size_t) t] = proc.trackName (t);

                              exportProgress = 0.0f;
                              setStatus ("EXPORTING " + baseName.toUpperCase() + " ...");
                              juce::Component::SafePointer<ModularSynthEditor> safe (this);
                              exportThread = std::make_unique<ExportThread> (
                                  std::move (take), opt, exportProgress, [safe] (ExportResult result) {
                                      if (safe == nullptr)
                                          return;
                                      if (! result.ok)
                                      {
                                          safe->setStatus ("EXPORT FAILED: " + result.error.toUpperCase());
                                          return;
                                      }
                                      safe->setStatus ("EXPORTED " + juce::String (result.files.size()) + " FILES ("
                                                       + formatTime ((double) result.lengthSamples / safe->proc.getEngineSampleRate())
                                                       + ") TO " + result.files[0].getParentDirectory().getFullPathName());
                                      result.files[0].revealToUser();
                                  });
                              exportThread->startThread();
                          });
}

void ModularSynthEditor::saveProject()
{
    chooser = std::make_unique<juce::FileChooser> ("Save project", lastFolder.getChildFile ("project.msynth"), "*.msynth");
    chooser->launchAsync (juce::FileBrowserComponent::saveMode | juce::FileBrowserComponent::warnAboutOverwriting,
                          [this] (const juce::FileChooser& fc) {
                              auto file = fc.getResult();
                              if (file == juce::File())
                                  return;
                              file = file.withFileExtension ("msynth");
                              setStatus (proc.saveProject (file) ? "SAVED " + file.getFullPathName()
                                                                 : "COULD NOT SAVE " + file.getFullPathName());
                          });
}

void ModularSynthEditor::openProject()
{
    chooser = std::make_unique<juce::FileChooser> ("Open project", lastFolder, "*.msynth");
    chooser->launchAsync (juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles,
                          [this] (const juce::FileChooser& fc) {
                              const auto file = fc.getResult();
                              if (file == juce::File())
                                  return;
                              juce::String error;
                              if (proc.openProject (file, error))
                                  setStatus (error.isNotEmpty() ? error.toUpperCase() : "OPENED " + file.getFileName().toUpperCase());
                              else
                                  setStatus (error.toUpperCase());
                              panel->refresh();
                          });
}

bool ModularSynthEditor::keyPressed (const juce::KeyPress& key)
{
    const auto mods = key.getModifiers();
    const auto c = (juce::juce_wchar) juce::CharacterFunctions::toLowerCase (key.getTextCharacter());

    if (mods.isCommandDown())
    {
        if (key.getKeyCode() == 'S') { saveProject(); return true; }
        if (key.getKeyCode() == 'O') { openProject(); return true; }
        return false;
    }

    if (key == juce::KeyPress::spaceKey) { togglePlay(); return true; }
    if (key == juce::KeyPress::escapeKey) { stop(); return true; }
    if (key == juce::KeyPress::upKey) { selectTrack (selected < 0 ? kNumTracks - 1 : juce::jmax (0, selected - 1)); return true; }
    if (key == juce::KeyPress::downKey) { selectTrack (selected < 0 || selected >= kNumTracks - 1 ? -1 : selected + 1); return true; }
    if (c == 'r') { toggleRecord(); return true; }
    if (c == 'e') { chooseExport(); return true; }

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
    return false;
}

bool ModularSynthEditor::keyStateChanged (bool)
{
    for (int t = 0; t < kNumTracks; ++t)
        if (padKeyHeld[(size_t) t]
            && ! juce::KeyPress::isKeyCurrentlyDown (kPadKeys[t][0])
            && ! juce::KeyPress::isKeyCurrentlyDown (kPadKeys[t][1])
            && ! juce::KeyPress::isKeyCurrentlyDown (juce::CharacterFunctions::toUpperCase ((juce::juce_wchar) kPadKeys[t][1])))
            padKeyHeld[(size_t) t] = false;
    return false;
}

void ModularSynthEditor::timerCallback()
{
    if (! keyboardFocusRequested && isShowing())
    {
        keyboardFocusRequested = true;
        grabKeyboardFocus();
    }

    const auto& state = proc.getUiState();
    const double sr = proc.getEngineSampleRate();
    const bool recording = proc.isRecording();

    if (auto take = proc.takeFinishedRecording())
        setStatus ("TAKE READY " + formatTime ((double) take->length / take->sampleRate) + " / "
                   + juce::String ((int) take->events.size()) + " EVENTS / PRESS E TO EXPORT MASTER + STEMS");

    playButton.setOn (proc.isPlaying());
    recButton.setOn (recording);

    bpmReadout.setText (juce::String (state.bpm, 1));
    swingReadout.setText (look::pad (juce::roundToInt (state.swing * 100.0f), 2) + " %");
    stepReadout.setText (look::pad (proc.currentStep() + 1, 2) + " / " + juce::String (kNumSteps));
    const auto pos = recording ? proc.recordedSamples() : proc.playedSamples();
    posReadout.setText (formatTime ((double) pos / sr) + "  " + juce::String::toHexString ((juce::int64) pos).paddedLeft ('0', 8).toUpperCase());
    posReadout.setColour (recording ? look::red : look::ink);
    rateReadout.setText (juce::String (juce::roundToInt (sr)) + " HZ / 24 BIT");

    if (recording)
    {
        takeReadout.setText ("REC " + formatTime ((double) proc.recordedSamples() / sr) + " / " + juce::String (proc.recordedEvents()) + " EV");
        takeReadout.setColour (look::red);
    }
    else if (const auto& last = proc.getLastTake(); last.has_value())
    {
        takeReadout.setText (formatTime ((double) last->length / last->sampleRate) + " / " + juce::String ((int) last->events.size()) + " EV");
        takeReadout.setColour (look::ink);
    }
    else
    {
        takeReadout.setText ("NONE");
        takeReadout.setColour (look::dim);
    }

    grid->tick();
    panel->refresh();

    if (exportProgress.load() >= 0.0f || ! status.isEmpty())
        repaint (0, 0, getWidth(), kHeaderHeight);
    repaint (0, getHeight() - kStatusHeight, getWidth(), kStatusHeight);
}

void ModularSynthEditor::paint (juce::Graphics& g)
{
    g.fillAll (look::background);

    // title block
    g.setColour (look::ink);
    g.setFont (look::mono (16.0f, true));
    g.drawText ("MODULAR-SYNTH", kMargin, 12, 260, 20, juce::Justification::centredLeft);
    g.setFont (look::mono (9.5f));
    g.setColour (look::dim);
    g.drawText ("DATAMATICS KIT // 8 TRACKS x 16 STEPS // V0.1", kMargin, 32, 360, 14, juce::Justification::centredLeft);

    // header rule, and the export progress as a blue line along it
    g.setColour (look::faint);
    g.fillRect (0, kHeaderHeight - 1, getWidth(), 1);
    const float progress = exportProgress.load();
    if (progress >= 0.0f)
    {
        g.setColour (look::blue);
        g.fillRect (0.0f, (float) kHeaderHeight - 2.0f, (float) getWidth() * progress, 2.0f);
        g.setFont (look::mono (9.5f));
        g.drawText ("EXPORT " + juce::String (juce::roundToInt (progress * 100.0f)) + "%",
                    getWidth() - 160 - kMargin, kHeaderHeight - 16, 160, 12, juce::Justification::centredRight);
    }

    // status line
    const auto statusArea = juce::Rectangle<int> (kMargin, getHeight() - kStatusHeight, getWidth() - 2 * kMargin, kStatusHeight);
    g.setColour (look::faint);
    g.fillRect (0, statusArea.getY(), getWidth(), 1);
    g.setFont (look::mono (10.0f));
    const bool fresh = status.isNotEmpty() && juce::Time::getMillisecondCounter() - statusTime < 12000;
    g.setColour (proc.isRecording() ? look::red : look::ink);
    g.drawText (fresh ? status : juce::String ("READY"), statusArea, juce::Justification::centredLeft, true);
    g.setColour (look::dim);
    g.drawText ("SPACE PLAY  R REC  E EXPORT  1-8 / A-K PADS  CLICK STEP / DRAG = VELOCITY  RIGHT-CLICK NAME = VOICE",
                statusArea, juce::Justification::centredRight, true);
}

void ModularSynthEditor::resized()
{
    auto area = getLocalBounds();
    auto header = area.removeFromTop (kHeaderHeight).reduced (kMargin, 0);

    // buttons along the bottom of the header
    auto buttons = header.removeFromBottom (34).withTrimmedBottom (10);
    for (auto* b : { &playButton, &stopButton, &recButton })
    {
        b->setBounds (buttons.removeFromLeft (72));
        buttons.removeFromLeft (6);
    }
    buttons.removeFromLeft (18);
    for (auto* b : { &exportButton, &saveButton, &openButton })
    {
        b->setBounds (buttons.removeFromLeft (80));
        buttons.removeFromLeft (6);
    }

    // readouts across the top right
    auto readouts = header.withTrimmedTop (8).withTrimmedLeft (380);
    const int widths[] = { 90, 100, 80, 220, 150, 190 };
    DataReadout* order[] = { &bpmReadout, &swingReadout, &stepReadout, &posReadout, &rateReadout, &takeReadout };
    for (int i = 0; i < 6; ++i)
    {
        order[i]->setBounds (readouts.removeFromLeft (widths[i]).withHeight (36));
        readouts.removeFromLeft (8);
    }

    area.removeFromBottom (kStatusHeight);
    panel->setBounds (area.removeFromBottom (kPanelHeight).reduced (kMargin, 0));
    grid->setBounds (area.reduced (kMargin, 6));
}
