#include "VocalView.h"

#include "SignalBox.h"
#include "Standalone.h"

using namespace ms;

namespace
{
constexpr int kHeaderHeight = 72;
constexpr int kPanelHeight = 250;
constexpr int kStatusHeight = 24;
constexpr int kMargin = 16;
// below these the window folds down (see resized)
constexpr int kCompactWidth = 1000;
constexpr int kCompactHeight = 420;
constexpr int kPanelMinHeight = 640;

juce::String barsText (int bars)
{
    return juce::String (bars) + (bars == 1 ? " BAR" : " BARS");
}

// Peaks of a clip, one per column, worked out again only when it changes.
struct ClipPeaks
{
    const VocalClip* clip = nullptr;
    std::int64_t span = 0;
    int columns = 0;
    std::vector<float> values;

    const std::vector<float>& of (const VocalClip* c, std::int64_t samples, int cols)
    {
        if (c != clip || samples != span || cols != columns)
        {
            clip = c;
            span = samples;
            columns = cols;
            values.assign ((size_t) juce::jmax (0, cols), 0.0f);
            if (c != nullptr && cols > 0)
                for (int i = 0; i < cols; ++i)
                    values[(size_t) i] = c->peak (samples * i / cols, samples * (i + 1) / cols);
        }
        return values;
    }
};
} // namespace

// =============================================================================
// The rows: the input, and the recording along the song.

class VocalGrid : public juce::Component
{
public:
    explicit VocalGrid (VocalView& v) : view (v) {}

    // Small windows: just the two signal boxes.
    void setCompact (bool c) { compact = c; }

    void tick() { repaint(); }

    void paint (juce::Graphics& g) override
    {
        if (compact)
        {
            paintCompact (g);
            return;
        }
        const auto geo = geometry();
        const auto& state = view.state();
        paintInput (g, geo, state);
        paintTake (g, geo, state);

        g.setFont (look::mono (9.0f));
        g.setColour (look::dim);
        g.drawText ("SIGNAL // LATTICE", juce::Rectangle<float> (geo.signalX, 0.0f, geo.signalW, (float) kRulerHeight),
                    juce::Justification::centredLeft);
    }

    void mouseDown (const juce::MouseEvent& e) override
    {
        if (compact)
            return;
        const auto geo = geometry();
        if (e.position.y < geo.rowY (kTakeRow) && e.position.x >= geo.toggleX)
            view.toggleMonitor();
    }

private:
    static constexpr int kRulerHeight = 18;
    static constexpr int kInputRow = 0, kTakeRow = 1;

    struct Geometry
    {
        float width = 0;
        std::array<float, 3> rowTop {};
        float nameX = 44, contentX = 190, contentW = 0, signalX = 0, signalW = 0, toggleX = 0, toggleW = 40;

        float rowY (int r) const { return rowTop[(size_t) r]; }
        float rowH (int r) const { return rowTop[(size_t) r + 1] - rowTop[(size_t) r]; }
    };

    Geometry geometry() const
    {
        Geometry g;
        g.width = (float) getWidth();
        const float top = (float) kRulerHeight, h = (float) getHeight() - top;
        g.rowTop = { top, top + h * 0.3f, (float) getHeight() };
        g.toggleX = g.width - g.toggleW;
        g.signalW = juce::jmin ((float) ModularSynthProcessor::kHistoryLength, juce::jmax (120.0f, g.width * 0.24f));
        g.signalX = g.toggleX - 12.0f - g.signalW;
        g.contentW = g.signalX - 24.0f - g.contentX;
        return g;
    }

    static void paintIndex (juce::Graphics& g, juce::Rectangle<float> box, const juce::String& text, bool on, bool red)
    {
        if (red)
        {
            g.setColour (look::red);
            g.fillRect (box);
        }
        g.setColour (on ? look::ink : look::dim);
        g.drawRect (box, 1.0f);
        g.setFont (look::mono (11.0f, true));
        g.setColour (look::ink);
        g.drawText (text, box, juce::Justification::centred);
    }

    void paintName (juce::Graphics& g, const Geometry& geo, int row, const juce::String& name, const juce::String& info)
    {
        const float y0 = geo.rowY (row), mid = y0 + geo.rowH (row) * 0.5f;
        g.setFont (look::mono (13.0f, true));
        g.setColour (look::ink);
        g.drawText (name, juce::Rectangle<float> (geo.nameX + 10.0f, mid - 20.0f, geo.contentX - geo.nameX - 14.0f, 18.0f),
                    juce::Justification::bottomLeft, true);
        g.setFont (look::mono (9.5f));
        g.setColour (look::dim);
        g.drawText (info, juce::Rectangle<float> (geo.nameX + 10.0f, mid + 2.0f, geo.contentX - geo.nameX - 14.0f, 14.0f),
                    juce::Justification::topLeft, true);
    }

    void paintSignal (juce::Graphics& g, juce::Rectangle<float> box, int track)
    {
        auto& proc = view.processor();
        proc.copyTrackScope (view.toolSlot(), track, signal.scope(), SignalBox::kFftSize);
        signal.paint (g, box, proc.getEngineSampleRate(), 0.0f, {}, 1.0f);
    }

    float recentPeak (int track) const
    {
        float peak = 0.0f;
        for (int c = 0; c < 8; ++c)
            peak = juce::jmax (peak, view.processor().trackHistory (view.toolSlot(), track, c));
        return peak;
    }

    // The input: its level over the last moments as a barcode, its lattice,
    // and the monitor switch.
    void paintInput (juce::Graphics& g, const Geometry& geo, const VocalState& state)
    {
        auto& proc = view.processor();
        const float y0 = geo.rowY (kInputRow), h = geo.rowH (kInputRow), mid = y0 + h * 0.5f;
        g.setColour (look::faint);
        g.fillRect (0.0f, y0, geo.width, 1.0f);
        paintIndex (g, { 4.0f, y0 + 6.0f, 30.0f, h - 12.0f }, "IN", state.monitor, false);
        paintName (g, geo, kInputRow, "INPUT", juce::String (loopInputName (state.input)) + " // " + formatDb (levelToGain (state.inputGain)));

        const float x0 = geo.contentX, w = geo.contentW;
        const int columns = juce::jmin (ModularSynthProcessor::kHistoryLength, (int) (w / 2.0f));
        for (int c = 0; c < columns; ++c)
        {
            const float v = juce::jlimit (0.0f, 1.0f, std::sqrt (proc.trackHistory (view.toolSlot(), kVocalInputTrack, c)) * 1.2f);
            if (v < 0.02f)
                continue;
            const float lineH = juce::jmax (1.0f, v * (h - 16.0f));
            g.setColour ((v > 0.95f ? look::red : look::ink).withAlpha (0.25f + 0.75f * v));
            g.fillRect (x0 + w - 2.0f * (float) (c + 1), mid - lineH * 0.5f, 1.0f, lineH);
        }
        g.setColour (look::faint.brighter (0.2f));
        g.fillRect (x0, mid, w, 1.0f);

        paintSignal (g, { geo.signalX - 1.0f, y0 + 5.0f, geo.signalW + 2.0f, h - 10.0f }, kVocalInputTrack);
        const float peak = recentPeak (kVocalInputTrack);
        g.setFont (look::mono (9.0f));
        g.setColour (peak > 1.0f ? look::red : look::dim);
        g.drawText (formatDb (peak), juce::Rectangle<float> (geo.signalX - 66.0f, y0 + 4.0f, 60.0f, 12.0f), juce::Justification::topRight);

        const juce::Rectangle<float> box (geo.toggleX, mid - 9.0f, geo.toggleW, 18.0f);
        if (state.monitor)
        {
            g.setColour (look::ink);
            g.fillRect (box);
            g.setColour (look::background);
        }
        else
        {
            g.setColour (look::dim);
            g.drawRect (box, 1.0f);
        }
        g.setFont (look::mono (10.0f, true));
        g.drawText ("MON", box, juce::Justification::centred);
    }

    // The recording along the song: a numbered bar ruler, the recording as a
    // barcode of its samples, each take it was pieced from underneath, the
    // playhead, and a recording as it goes.
    void paintTake (juce::Graphics& g, const Geometry& geo, const VocalState& state)
    {
        auto& proc = view.processor();
        const auto readout = proc.vocalReadout (view.toolSlot());
        const double sr = proc.getEngineSampleRate();
        const double barLen = sr * 60.0 / (double) state.bpm * 4.0;
        const auto* clip = state.clip != nullptr && ! state.clip->empty() ? state.clip : nullptr;
        const bool recording = readout.status == VocalEngine::Status::Recording;
        const bool busy = readout.status != VocalEngine::Status::Idle;
        const bool playing = proc.isPlaying();

        const float y0 = geo.rowY (kTakeRow), h = geo.rowH (kTakeRow);
        g.setColour (look::faint);
        g.fillRect (0.0f, y0, geo.width, 1.0f);
        paintIndex (g, { 4.0f, y0 + 6.0f, 30.0f, h - 12.0f }, "VX", clip != nullptr, recording);

        juce::String info = "EMPTY // PRESS REC VOCAL";
        if (clip != nullptr)
            info = formatTime ((double) clip->end() / sr) + " // " + juce::String ((int) clip->sources.size())
                   + ((int) clip->sources.size() == 1 ? " TAKE" : " TAKES");
        paintName (g, geo, kTakeRow, "VOCAL", info);
        if (state.masterFxAuto.any (0))
            paintTag (g, "AUTO", geo.contentX - 46.0f, y0 + 6.0f, look::blue);

        // how much of the song to show: everything there is, in fours of bars
        std::int64_t extent = playing ? readout.position : 0;
        if (clip != nullptr)
            extent = juce::jmax (extent, clip->end());
        if (recording)
            extent = juce::jmax (extent, readout.recordStart + readout.recorded);
        const int bars = juce::jmax (8, 4 * (int) std::ceil (((double) extent / barLen + 1.0) / 4.0));
        const auto span = (std::int64_t) (barLen * bars);

        const auto area = juce::Rectangle<float> (geo.contentX, y0 + 30.0f, geo.contentW, h - 60.0f);
        auto xAt = [&] (double samples) { return area.getX() + (float) (samples / (double) span) * area.getWidth(); };

        // the ruler: a tick on every bar, numbered as often as there is room
        const float barW = area.getWidth() / (float) bars;
        int every = 1;
        while (barW * (float) every < 30.0f)
            every *= 2;
        g.setFont (look::mono (8.5f));
        for (int b = 0; b <= bars; ++b)
        {
            const float x = area.getX() + barW * (float) b;
            const bool numbered = b % every == 0;
            g.setColour (numbered ? look::dim : look::faint.brighter (0.15f));
            g.fillRect (x, area.getY() - (numbered ? 10.0f : 5.0f), 1.0f, area.getHeight() + (numbered ? 14.0f : 5.0f));
            if (numbered && b < bars)
                g.drawText (juce::String (b + 1), juce::Rectangle<float> (x + 3.0f, area.getY() - 22.0f, 40.0f, 12.0f),
                            juce::Justification::centredLeft);
            if (barW >= 40.0f && b < bars)
                for (int beat = 1; beat < 4; ++beat)
                    g.fillRect (x + barW * (float) beat / 4.0f, area.getY() - 3.0f, 1.0f, 3.0f);
        }
        g.setColour (look::faint.brighter (0.2f));
        g.fillRect (area.getX(), area.getCentreY(), area.getWidth(), 1.0f);

        // the recording, one line a column
        if (clip != nullptr)
        {
            const int columns = (int) area.getWidth();
            const auto& values = peaks.of (clip, span, columns);
            for (int c = 0; c < columns; ++c)
            {
                const float v = juce::jlimit (0.0f, 1.0f, std::sqrt (values[(size_t) c]));
                if (v < 0.01f)
                    continue;
                const float lineH = juce::jmax (1.0f, v * area.getHeight());
                g.setColour ((v > 0.99f ? look::red : look::ink).withAlpha (0.3f + 0.7f * v));
                g.fillRect (area.getX() + (float) c, area.getCentreY() - lineH * 0.5f, 1.0f, lineH);
            }

            // the takes it is pieced from: a line each underneath, numbered
            // in the order they were sung, a blue mark where each starts
            g.setFont (look::mono (8.0f, true));
            for (const auto& s : clip->segments)
            {
                int number = 1;
                for (size_t i = 0; i < clip->sources.size(); ++i)
                    if (clip->sources[i].get() == s.source)
                        number = (int) i + 1;
                const float x0 = xAt ((double) s.start), x1 = xAt ((double) s.end());
                g.setColour (look::ink.withAlpha (number % 2 == 1 ? 0.8f : 0.45f));
                g.fillRect (x0, area.getBottom() + 8.0f, juce::jmax (1.0f, x1 - x0 - 1.0f), 2.0f);
                g.setColour (look::blue);
                g.fillRect (x0, area.getBottom() + 4.0f, 1.0f, 10.0f);
                if (x1 - x0 > 24.0f)
                {
                    g.setColour (look::dim);
                    g.drawText ("T" + juce::String (number), juce::Rectangle<float> (x0 + 3.0f, area.getBottom() + 11.0f, 30.0f, 10.0f),
                                juce::Justification::centredLeft);
                }
            }
        }

        // a recording as it goes: red over the part being replaced, the
        // input's level as it was sung
        if (recording)
        {
            const auto from = readout.recordStart, to = readout.recordStart + readout.recorded;
            const auto r = juce::Rectangle<float> (xAt ((double) from), area.getY(), juce::jmax (1.0f, xAt ((double) to) - xAt ((double) from)),
                                                   area.getHeight());
            g.setColour (look::background);
            g.fillRect (r);
            g.setColour (look::red.withAlpha (0.18f));
            g.fillRect (r);
            const auto& live = view.liveRecording();
            const double perEntry = (double) ModularSynthProcessor::kHistoryWindow;
            float lastX = -1.0f;
            for (size_t i = 0; i < live.size(); ++i)
            {
                const float x = std::floor (xAt ((double) from + perEntry * (double) i));
                if (x <= lastX)
                    continue;
                lastX = x;
                float p = live[i];
                for (size_t j = i + 1; j < live.size() && std::floor (xAt ((double) from + perEntry * (double) j)) <= x; ++j)
                    p = juce::jmax (p, live[j]);
                const float v = juce::jlimit (0.0f, 1.0f, std::sqrt (p));
                const float lineH = juce::jmax (1.0f, v * area.getHeight());
                g.setColour (look::red.withAlpha (0.4f + 0.6f * v));
                g.fillRect (x, area.getCentreY() - lineH * 0.5f, 1.0f, lineH);
            }
            g.setColour (look::red);
            g.drawRect (r.expanded (0.0f, 3.0f), 1.0f);
        }

        // the playhead, with where it is above it, in red
        if (playing)
        {
            const float x = xAt ((double) readout.position);
            g.setColour (look::red);
            g.fillRect (x - 0.5f, area.getY() - 12.0f, 1.0f, area.getHeight() + 16.0f);
            const double bar = (double) readout.position / barLen;
            const juce::String where = juce::String ((int) bar + 1) + "." + juce::String ((int) ((bar - std::floor (bar)) * 4.0) + 1);
            paintTag (g, where, x + 3.0f, area.getY() - 12.0f, look::red);
        }

        if (busy)
        {
            const bool blink = recording || (juce::Time::getMillisecondCounter() / 250) % 2 == 0;
            const juce::String what = readout.status == VocalEngine::Status::Waiting ? juce::String ("REC // NEXT BAR")
                                      : readout.status == VocalEngine::Status::CountIn ? "REC // COUNT-IN " + juce::String (readout.countBeats)
                                                                                         : "REC " + formatTime ((double) readout.recorded / sr);
            if (blink)
                paintTag (g, what, area.getX() + 4.0f, area.getBottom() + 18.0f, look::red);
        }

        paintSignal (g, { geo.signalX - 1.0f, y0 + 5.0f, geo.signalW + 2.0f, h - 10.0f }, kVocalTrack);
        const float peak = recentPeak (kVocalTrack);
        g.setFont (look::mono (9.0f));
        g.setColour (peak > 1.0f ? look::red : look::dim);
        g.drawText (formatDb (peak), juce::Rectangle<float> (geo.signalX - 66.0f, y0 + 4.0f, 60.0f, 12.0f), juce::Justification::topRight);
        g.drawText (formatDb (levelToGain (state.masterLevel)),
                    juce::Rectangle<float> (geo.toggleX, y0 + h * 0.5f - 6.0f, geo.toggleW, 12.0f), juce::Justification::centredRight);
        g.setColour (look::dim);
        g.drawText (barsText (bars) + " SHOWN", juce::Rectangle<float> (area.getRight() - 200.0f, area.getBottom() + 14.0f, 200.0f, 12.0f),
                    juce::Justification::centredRight);
    }

    void paintCompact (juce::Graphics& g)
    {
        const float rowH = (float) getHeight() / 2.0f, w = (float) getWidth();
        const auto& state = view.state();
        const auto readout = view.processor().vocalReadout (view.toolSlot());
        for (int r = 0; r < 2; ++r)
        {
            const float y0 = rowH * (float) r;
            g.setColour (look::faint);
            g.fillRect (0.0f, y0, w, 1.0f);
            if (r == 0)
                paintIndex (g, { 2.0f, y0 + 3.0f, 30.0f, rowH - 6.0f }, "IN", state.monitor, false);
            else
                paintIndex (g, { 2.0f, y0 + 3.0f, 30.0f, rowH - 6.0f }, "VX", state.clip != nullptr,
                            readout.status == VocalEngine::Status::Recording);
            paintSignal (g, { 38.0f, y0 + 2.0f, w - 38.0f, rowH - 4.0f }, r == 0 ? kVocalInputTrack : kVocalTrack);
        }
    }

    VocalView& view;
    SignalBox signal;
    ClipPeaks peaks;
    bool compact = false;
};

// =============================================================================
// The input and mix controls, then the track's effects rack.

class VocalPanel : public juce::Component
{
public:
    explicit VocalPanel (VocalView& v) : view (v)
    {
        addAndMakeVisible (clearAutoButton);

        addAndMakeVisible (gain);
        gain.setLabel ("IN GAIN");
        gain.setDefault (VocalState().inputGain);
        gain.format = [] (float value) { return formatDb (levelToGain (value)); };
        gain.onChange = [this] (float value) { view.send (CommandType::SetInputGain, 0, 0, value); };

        addAndMakeVisible (level);
        level.setLabel ("LEVEL");
        level.setDefault (VocalState().masterLevel);
        level.format = [] (float value) { return formatDb (levelToGain (value)); };
        level.onChange = [this] (float value) { view.send (CommandType::SetMasterLevel, 0, 0, value); };

        addAndMakeVisible (pan);
        pan.setLabel ("PAN");
        pan.setDefault (0.5f);
        pan.setBipolar (true);
        pan.format = [] (float value) { return formatPan (value); };
        pan.onChange = [this] (float value) { view.send (CommandType::SetPan, 0, 0, value); };

        fx.attach (*this, [this] { return view.state().masterFx; },
                   [this] (int i, float value) { view.send (CommandType::SetFxParam, -1, i, value); });

        // the track has no patterns: its moves loop every bar, like the studio master's
        auto& proc = view.processor();
        FxRackControls::Automation a;
        a.moves = [this] { return &view.state().masterFxAuto; };
        a.pattern = [] { return 0; };
        a.recording = [&proc] { return proc.isAutoArmed() && proc.isPlaying(); };
        a.step = [&proc] { return proc.currentStep(); };
        a.position = [&proc] { return proc.isPlaying() ? proc.stepPosition() : -1.0; };
        a.send = [this] (CommandType type, int index, float value) { view.send (type, -1, index, value); };
        fx.setAutomation (std::move (a));

        clearAutoButton.onClick = [this] {
            view.send (CommandType::ClearFxAuto, -1, 0, -1.0f);
            view.setStatus ("CLEARED THE VOCAL TRACK'S EFFECT AUTOMATION");
        };
    }

    void refresh()
    {
        const auto& state = view.state();
        gain.setValue (state.inputGain);
        level.setValue (state.masterLevel);
        pan.setValue (state.pan);
        fx.setValues (state.masterFx);
        fx.tickAutomation (state.masterFx);
        clearAutoButton.setVisible (state.masterFxAuto.any (0));
        repaint (meterArea());
    }

    void paint (juce::Graphics& g) override
    {
        g.setColour (look::dim);
        g.fillRect (0, 0, getWidth(), 1);
        g.setFont (look::mono (11.0f, true));
        g.setColour (look::ink);
        g.drawText ("VOCAL RACK // THE RECORDING + THE MONITORED INPUT", 0, 6, columnX (3), 18, juce::Justification::centredLeft);

        g.setFont (look::mono (9.5f));
        g.setColour (look::dim);
        for (int c = 0; c < kColumns; ++c)
            g.drawText (c == 0 ? "INPUT + MIX" : FxRackControls::groupName (c - 1), columnX (c), 30, columnX (1) - 24, 14,
                        juce::Justification::centredLeft);

        const auto m = meterArea().toFloat();
        paintPeakMeter (g, m.getX(), m.getCentreY(), m.getWidth(), view.processor().busPeak (view.toolSlot()), hold);
        g.setColour (look::dim);
        g.drawFittedText ("RECORDINGS TAKE THE INPUT BEFORE THE RACK, SO THE EFFECTS CAN CHANGE LATER. " + standalone::inputDescription(),
                          juce::Rectangle<int> (columnX (0), m.toNearestInt().getBottom() + 14, columnX (1) - 24, 40),
                          juce::Justification::topLeft, 3);
    }

    void resized() override
    {
        const int rowH = 20, y0 = 48;
        const int colW = columnX (1) - columnX (0) - 24;
        clearAutoButton.setBounds (getWidth() - 100, 4, 100, 20);
        gain.setBounds (columnX (0), y0, colW, rowH);
        level.setBounds (columnX (0), y0 + rowH, colW, rowH);
        pan.setBounds (columnX (0), y0 + 2 * rowH, colW, rowH);
        fx.layout ([this] (int c) { return columnX (c + 1); }, y0, rowH, colW);
    }

private:
    static constexpr int kColumns = 5;
    int columnX (int c) const { return c * (getWidth() + 24) / kColumns; }
    juce::Rectangle<int> meterArea() const { return { columnX (0), 48 + 3 * 20 + 10, columnX (1) - columnX (0) - 24, 36 }; }

    VocalView& view;
    DataButton clearAutoButton { "CLEAR AUTO" };
    DataSlider gain, level, pan;
    FxRackControls fx;
    float hold = 0.0f;
};

// =============================================================================

VocalView::VocalView (ModularSynthProcessor& p, int t) : proc (p), tool (t)
{
    grid = std::make_unique<VocalGrid> (*this);
    panel = std::make_unique<VocalPanel> (*this);
    addAndMakeVisible (*grid);
    addAndMakeVisible (*panel);

    for (auto* b : { &recButton, &countButton, &clickButton, &inputButton, &monitorButton, &dryButton, &clearButton, &resetButton, &autoButton })
        addAndMakeVisible (*b);
    recButton.setAccent (look::red);
    autoButton.setAccent (look::red);
    recButton.onClick = [this] { toggleRecord(); };
    inputButton.onClick = [this] { showInputMenu(); };
    monitorButton.onClick = [this] { toggleMonitor(); };
    dryButton.onClick = [this] { toggleMonitorDry(); };
    countButton.onClick = [this] { showCountInMenu(); };
    clickButton.onClick = [this] { showClickMenu(); };
    clearButton.onClick = [this] { clearTake(); };
    resetButton.onClick = [this] { resetToDefaults(); };
    autoButton.onClick = [this] {
        proc.setAutoArmed (! proc.isAutoArmed());
        setStatus (proc.isAutoArmed() ? "AUTO ARMED // WHILE PLAYING, EFFECT MOVES ARE RECORDED INTO THE STEPS (RIGHT-CLICK A CONTROL TO CLEAR IT)"
                                      : "AUTO OFF // RECORDED MOVES KEEP PLAYING");
    };

    for (auto* r : { &positionReadout, &takeReadout, &latencyReadout })
        addAndMakeVisible (*r);
    positionReadout.setLabel ("BAR.BEAT / TIME");
    takeReadout.setLabel ("RECORDING");
    latencyReadout.setLabel ("LATENCY / TAKEN OFF");

    seenLayers = proc.vocalLayers (tool);
    lastHistoryPos = proc.historyWritePosition();

    setWantsKeyboardFocus (true);
    setSize (1320, 820);
    panel->refresh();
}

VocalView::~VocalView() = default;

const VocalState& VocalView::state() const
{
    return proc.getStudioState().vocals[(size_t) vocalIndex (tool)];
}

void VocalView::send (CommandType type, int track, int index, float value)
{
    Command c;
    c.type = type;
    c.tool = tool;
    c.track = track;
    c.index = index;
    c.value = value;
    proc.send (c);
}

void VocalView::setStatus (const juce::String& s)
{
    status = s;
    statusTime = juce::Time::getMillisecondCounter();
    repaint (0, getHeight() - kStatusHeight, getWidth(), kStatusHeight);
}

juce::String VocalView::barText (std::int64_t samples) const
{
    const double barLen = proc.getEngineSampleRate() * 60.0 / (double) state().bpm * 4.0;
    const double bar = (double) samples / barLen;
    return juce::String ((int) bar + 1) + "." + juce::String ((int) ((bar - std::floor (bar)) * 4.0) + 1);
}

// ---- recording ----------------------------------------------------------------

void VocalView::toggleRecord()
{
    const auto readout = proc.vocalReadout (tool);
    if (readout.status == VocalEngine::Status::Recording)
    {
        proc.stopVocalRecord (tool);
        setStatus ("RECORDED " + formatTime ((double) readout.recorded / proc.getEngineSampleRate()) + " FROM BAR "
                   + barText (readout.recordStart) + " // CTRL+Z TAKES IT OFF AGAIN");
        return;
    }
    if (readout.status != VocalEngine::Status::Idle)
    {
        proc.stopVocalRecord (tool);
        setStatus ("CANCELLED // THE RECORDING STAYS AS IT WAS");
        return;
    }

    proc.recordVocal (tool);
    const bool wasPlaying = proc.isPlaying();
    if (! wasPlaying)
    {
        Command play;
        play.type = CommandType::Play;
        play.tool = kStudio;
        proc.send (play);
    }
    const int count = state().countIn;
    const juce::String when = (wasPlaying ? juce::String ("FROM THE NEXT BAR") : juce::String ("FROM BAR 1"))
                              + (count > 0 ? " AFTER " + barsText (count) + " OF COUNT-IN" : juce::String());
    setStatus ("REC VOCAL " + when + " // V AGAIN TO STOP"
               + (state().clip != nullptr ? " // ONLY THE PART YOU SING OVER IS REPLACED" : juce::String())
               + (state().monitor ? juce::String() : " // M TO HEAR YOURSELF"));
}

void VocalView::toggleMonitor()
{
    const bool on = ! state().monitor;
    send (CommandType::SetLoopMonitor, 0, 0, on ? 1.0f : 0.0f);
    setStatus (on ? (state().monitorDry ? "MONITOR ON // YOU HEAR THE INPUT DRY (FX BUTTON = THROUGH THE RACK) // HEADPHONES AVOID FEEDBACK"
                                        : "MONITOR ON // YOU HEAR THE INPUT THROUGH THE VOCAL RACK (DRY BUTTON = DRY) // HEADPHONES AVOID FEEDBACK")
                  : "MONITOR OFF // THE INPUT IS ONLY RECORDED");
}

void VocalView::toggleMonitorDry()
{
    // Only what you hear changes: recordings are always dry, the recording
    // then plays through the rack.
    const bool dry = ! state().monitorDry;
    send (CommandType::SetMonitorDry, 0, 0, dry ? 1.0f : 0.0f);
    if (! state().monitor)
        send (CommandType::SetLoopMonitor, 0, 0, 1.0f);
    setStatus (dry ? "MONITOR DRY // YOU HEAR THE INPUT AS IT COMES IN, WITHOUT THE RACK (SHIFT+M SWITCHES)"
                   : "MONITOR FX // YOU HEAR THE INPUT THROUGH THE VOCAL RACK (SHIFT+M SWITCHES)");
}

void VocalView::showClickMenu()
{
    juce::PopupMenu m;
    m.addSectionHeader ("METRONOME // A CLICK ON EVERY BEAT, NEVER RECORDED OR EXPORTED");
    m.addItem (1 + kClickOff, "OFF", true, state().metronome == kClickOff);
    m.addItem (1 + kClickPlaying, "ON WHILE PLAYING", true, state().metronome == kClickPlaying);
    m.addItem (1 + kClickRecording, "ONLY WHILE RECORDING", true, state().metronome == kClickRecording);
    m.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (&clickButton), [this] (int r) {
        if (r > 0)
            setClick (r - 1);
    });
}

void VocalView::setClick (int mode)
{
    send (CommandType::SetMetronome, 0, mode, 0.0f);
    setStatus (mode == kClickPlaying     ? "CLICK ON // A CLICK ON EVERY BEAT WHILE PLAYING, HIGHER ON THE 1 (K SWITCHES IT)"
               : mode == kClickRecording ? "CLICK WHILE RECORDING // ONLY WHILE THIS TRACK RECORDS"
                                         : "CLICK OFF // THE COUNT-IN STILL CLICKS");
}

void VocalView::showCountInMenu()
{
    juce::PopupMenu m;
    m.addSectionHeader ("COUNT-IN // CLICKS BEFORE A RECORDING STARTS");
    for (int bars : kCountIns)
        m.addItem (bars + 1, bars == 0 ? juce::String ("OFF") : barsText (bars), true, state().countIn == bars);
    m.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (&countButton), [this] (int r) {
        if (r <= 0)
            return;
        const int bars = r - 1;
        send (CommandType::SetCountIn, 0, 0, (float) bars);
        setStatus (bars == 0 ? "COUNT-IN OFF // RECORDING STARTS ON THE BAR"
                             : "COUNT-IN " + barsText (bars) + " // CLICKS ON THE BEATS, THEN IT RECORDS (THE CLICK IS NEVER RECORDED OR EXPORTED)");
    });
}

void VocalView::showInputMenu()
{
    juce::PopupMenu m;
    m.addSectionHeader ("INPUT // " + standalone::inputDescription());
    for (int i = 0; i < (int) LoopInput::Count; ++i)
        m.addItem (1 + i, loopInputName ((LoopInput) i), true, state().input == (LoopInput) i);
    m.addSeparator();
    m.addItem (100, "AUDIO DEVICE AND INPUTS...");
    m.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (&inputButton), [this] (int r) {
        if (r == 100)
            standalone::showAudioSettings();
        else if (r > 0)
        {
            send (CommandType::SetLoopInput, 0, r - 1, 0.0f);
            setStatus ("RECORDS " + juce::String (loopInputName ((LoopInput) (r - 1)))
                       + (r - 1 == (int) LoopInput::Stereo ? " AS A STEREO PAIR" : " IN THE MIDDLE (PAN MOVES IT)")
                       + " // EVERY LOOPER AND VOCAL TRACK PICKS ITS OWN INPUT");
        }
    });
}

// ---- clear, reset, undo -----------------------------------------------------------

VocalView::Snapshot VocalView::snapshot() const
{
    Snapshot snap;
    snap.state = state();
    snap.held = proc.holdClip (snap.state.clip);
    return snap;
}

void VocalView::remember()
{
    lastUndo = snapshot();
}

void VocalView::applyVocal (const Snapshot& target, bool clip)
{
    const auto& now = state();
    const auto& want = target.state;
    auto set = [this] (CommandType type, int track, int index, float was, float wanted) {
        if (! juce::exactlyEqual (was, wanted))
            send (type, track, index, wanted);
    };
    set (CommandType::SetLoopMonitor, 0, 0, now.monitor ? 1.0f : 0.0f, want.monitor ? 1.0f : 0.0f);
    set (CommandType::SetMonitorDry, 0, 0, now.monitorDry ? 1.0f : 0.0f, want.monitorDry ? 1.0f : 0.0f);
    set (CommandType::SetCountIn, 0, 0, (float) now.countIn, (float) want.countIn);
    if (now.metronome != want.metronome)
        send (CommandType::SetMetronome, 0, want.metronome, 0.0f);
    set (CommandType::SetInputGain, 0, 0, now.inputGain, want.inputGain);
    set (CommandType::SetMasterLevel, 0, 0, now.masterLevel, want.masterLevel);
    set (CommandType::SetPan, 0, 0, now.pan, want.pan);
    for (int i = 0; i < kNumFxParams; ++i)
        set (CommandType::SetFxParam, -1, i, state().masterFx[(size_t) i], want.masterFx[(size_t) i]);
    if (! clip)
        return;
    if (state().clip != target.held.get())
    {
        Command c;
        c.type = CommandType::SetVocalClip;
        c.tool = tool;
        c.clip = target.held.get();
        proc.send (c);
    }
    if (state().masterFxAuto != want.masterFxAuto)
        copyFxAutomation ([this] (CommandType type, int rack, int index, float value) { send (type, rack, index, value); }, -1,
                          want.masterFxAuto, 0, 0);
}

void VocalView::clearTake()
{
    // Empties the recording and puts the track's rack back to its defaults.
    auto target = snapshot();
    target.state.clip = nullptr;
    target.held.reset();
    target.state.masterFx = defaultFxParams();
    target.state.masterFxAuto.clear (0);

    remember();
    undoText = "THE RECORDING AND THE EFFECTS";
    applyVocal (target, true);
    setStatus ("CLEARED THE RECORDING + RESET THE EFFECTS / CTRL+Z TO BRING IT BACK");
}

void VocalView::resetToDefaults()
{
    // Monitor, gain, level, pan and the rack back to how a new track starts.
    // The recording and the input choice stay.
    remember();
    undoText = "THE SETTINGS BEFORE THE RESET";
    Snapshot target;
    target.state = makeDefaultVocal();
    applyVocal (target, false);
    setStatus ("RESET THE VOCAL TRACK'S SETTINGS / RECORDING KEPT / CTRL+Z TO UNDO");
}

void VocalView::undo()
{
    if (lastUndo.has_value())
    {
        applyVocal (*lastUndo, true);
        lastUndo.reset();
        setStatus ("UNDONE / " + undoText + " RESTORED");
        return;
    }
    if (proc.undoVocal (tool))
    {
        seenLayers = proc.vocalLayers (tool);
        setStatus ("TOOK OFF THE LAST RECORDING // " + juce::String (seenLayers) + " MORE TO GO BACK");
        return;
    }
    setStatus ("NOTHING TO UNDO");
}

// ---- keys -------------------------------------------------------------------------

bool VocalView::keyPressed (const juce::KeyPress& key)
{
    const auto mods = key.getModifiers();
    const auto c = (juce::juce_wchar) juce::CharacterFunctions::toLowerCase (key.getTextCharacter());
    auto unhandled = [this, &key] { return onUnhandledKey != nullptr && onUnhandledKey (key); };

    if (mods.isCommandDown())
    {
        if (key.getKeyCode() == 'Z') { undo(); return true; }
        return unhandled();
    }

    if (key == juce::KeyPress::deleteKey || key == juce::KeyPress::backspaceKey) { clearTake(); return true; }
    if (c == 'v') { toggleRecord(); return true; }
    if (c == 'm' && mods.isShiftDown()) { toggleMonitorDry(); return true; }
    if (c == 'm') { toggleMonitor(); return true; }
    if (c == 'k') { setClick (state().metronome == kClickOff ? kClickPlaying : kClickOff); return true; }
    return unhandled();
}

// ---- timer, drawing -------------------------------------------------------------

void VocalView::tick()
{
    const auto& v = state();
    // a finished recording replaces the undo before it
    const int layers = proc.vocalLayers (tool);
    if (layers > seenLayers)
        lastUndo.reset();
    seenLayers = layers;

    const auto readout = proc.vocalReadout (tool);
    const bool recording = readout.status == VocalEngine::Status::Recording;
    const bool busy = readout.status != VocalEngine::Status::Idle;
    const double sr = proc.getEngineSampleRate();

    // the input's level, collected while recording so the take can be drawn
    // as it goes
    const int historyPos = proc.historyWritePosition();
    if (recording && ! wasRecording)
        livePeaks.clear();
    if (recording)
    {
        const int fresh = (historyPos - lastHistoryPos + ModularSynthProcessor::kHistoryLength) % ModularSynthProcessor::kHistoryLength;
        for (int age = fresh - 1; age >= 0; --age)
            livePeaks.push_back (proc.trackHistory (tool, kVocalInputTrack, age));
        // line it up with what the track has recorded
        const auto want = (size_t) (readout.recorded / ModularSynthProcessor::kHistoryWindow);
        if (livePeaks.size() > want)
            livePeaks.erase (livePeaks.begin(), livePeaks.begin() + (std::ptrdiff_t) (livePeaks.size() - want));
        if (livePeaks.size() < want)
            livePeaks.insert (livePeaks.begin(), want - livePeaks.size(), 0.0f);
    }
    else if (! busy)
        livePeaks.clear();
    lastHistoryPos = historyPos;
    wasRecording = recording;

    const bool blink = recording || (juce::Time::getMillisecondCounter() / 250) % 2 == 0;
    recButton.setOn (busy && blink);
    recButton.setText (recording ? "STOP REC" : "REC VOCAL");
    inputButton.setText (loopInputName (v.input));
    monitorButton.setOn (v.monitor);
    dryButton.setText (v.monitorDry ? "DRY" : "FX");
    dryButton.setOn (v.monitor);
    clickButton.setText (v.metronome == kClickRecording ? "CLICK REC" : "CLICK");
    clickButton.setOn (v.metronome != kClickOff);
    countButton.setText (v.countIn == 0 ? juce::String ("NO COUNT") : "COUNT " + juce::String (v.countIn));
    autoButton.setOn (proc.isAutoArmed());

    const auto position = proc.isPlaying() ? readout.position : 0;
    positionReadout.setText (barText (position) + "  " + formatTime ((double) position / sr));
    juce::String takeText = v.clip == nullptr || v.clip->empty() ? juce::String ("EMPTY") : formatTime ((double) v.clip->end() / sr);
    if (readout.status == VocalEngine::Status::Waiting)
        takeText = "REC WAIT";
    else if (readout.status == VocalEngine::Status::CountIn)
        takeText = "REC IN " + juce::String (readout.countBeats);
    else if (recording)
        takeText = "REC " + formatTime ((double) readout.recorded / sr);
    takeReadout.setText (takeText);
    takeReadout.setColour (busy ? look::red : look::ink);
    latencyReadout.setText (juce::String (1000.0 * proc.getInputLatency() / sr, 1) + " MS");

    grid->tick();
    panel->refresh();
    repaint (0, getHeight() - kStatusHeight, getWidth(), kStatusHeight);
}

void VocalView::paint (juce::Graphics& g)
{
    g.fillAll (look::background);
    if (isCompact())
        return;

    g.setColour (look::ink);
    g.setFont (look::mono (16.0f, true));
    g.drawText (look::pad (tool + 1, 2) + " " + toolTitle (tool), kMargin, 10, 300, 20, juce::Justification::centredLeft);
    g.setFont (look::mono (9.5f));
    g.setColour (look::dim);
    g.drawText ("AUDIO IN // RECORDED ALONG THE SONG FROM BAR 1 // RECORD AGAIN OVER ANY PART TO REPLACE IT", kMargin, 30, 640, 14,
                juce::Justification::centredLeft);

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
    g.drawText ("V REC / STOP  M MONITOR  SHIFT+M DRY/FX  K CLICK  DEL CLEAR  CTRL+Z UNDO / LAST RECORDING", statusArea,
                juce::Justification::centredRight, true);
}

bool VocalView::isCompact() const
{
    return getWidth() < kCompactWidth || getHeight() < kCompactHeight;
}

void VocalView::resized()
{
    // Smaller windows fold away the rack, then everything but REC, MON and
    // the signal boxes.
    const bool compact = isCompact();
    grid->setCompact (compact);
    panel->setVisible (! compact && getHeight() >= kPanelMinHeight);
    for (juce::Component* c : { (juce::Component*) &countButton, (juce::Component*) &clickButton, (juce::Component*) &inputButton,
                                (juce::Component*) &dryButton, (juce::Component*) &clearButton, (juce::Component*) &resetButton,
                                (juce::Component*) &autoButton, (juce::Component*) &positionReadout, (juce::Component*) &takeReadout,
                                (juce::Component*) &latencyReadout })
        c->setVisible (! compact);
    if (compact)
    {
        auto area = getLocalBounds();
        auto buttons = area.removeFromTop (34).reduced (8, 6);
        recButton.setBounds (buttons.removeFromLeft (84));
        buttons.removeFromLeft (4);
        monitorButton.setBounds (buttons.removeFromLeft (52));
        grid->setBounds (area.reduced (6, 2));
        return;
    }

    auto area = getLocalBounds();
    auto header = area.removeFromTop (kHeaderHeight).reduced (kMargin, 0);

    auto buttons = header.removeFromBottom (30).withTrimmedBottom (8);
    recButton.setBounds (buttons.removeFromLeft (96));
    buttons.removeFromLeft (6);
    countButton.setBounds (buttons.removeFromLeft (80));
    buttons.removeFromLeft (6);
    clickButton.setBounds (buttons.removeFromLeft (80));
    buttons.removeFromLeft (18);
    inputButton.setBounds (buttons.removeFromLeft (72));
    buttons.removeFromLeft (6);
    monitorButton.setBounds (buttons.removeFromLeft (52));
    buttons.removeFromLeft (4);
    dryButton.setBounds (buttons.removeFromLeft (44));
    buttons.removeFromLeft (18);
    clearButton.setBounds (buttons.removeFromLeft (64));
    buttons.removeFromLeft (6);
    resetButton.setBounds (buttons.removeFromLeft (64));
    buttons.removeFromLeft (18);
    autoButton.setBounds (buttons.removeFromLeft (64));

    auto readouts = header.withTrimmedTop (8).withTrimmedLeft (juce::jmax (680, header.getWidth() - 430));
    positionReadout.setBounds (readouts.removeFromLeft (150).withHeight (36));
    readouts.removeFromLeft (8);
    takeReadout.setBounds (readouts.removeFromLeft (110).withHeight (36));
    readouts.removeFromLeft (8);
    latencyReadout.setBounds (readouts.removeFromLeft (150).withHeight (36));

    area.removeFromBottom (kStatusHeight);
    if (panel->isVisible())
        panel->setBounds (area.removeFromBottom (kPanelHeight).reduced (kMargin, 0));
    grid->setBounds (area.reduced (kMargin, 6));
}
