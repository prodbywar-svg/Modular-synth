#include "LooperView.h"

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

// Peaks of a loop's samples, one per column, worked out once per loop.
struct Peaks
{
    const LoopAudio* audio = nullptr;
    int length = 0, columns = 0;
    std::vector<float> values;

    const std::vector<float>& of (const LoopAudio* a, int cols)
    {
        if (a != audio || a == nullptr || a->length != length || cols != columns)
        {
            audio = a;
            length = a != nullptr ? a->length : 0;
            columns = cols;
            values.assign ((size_t) juce::jmax (0, cols), 0.0f);
            if (a != nullptr && a->length > 0 && cols > 0)
            {
                for (int c = 0; c < cols; ++c)
                {
                    const int from = (int) ((juce::int64) a->length * c / cols);
                    const int to = juce::jmax (from + 1, (int) ((juce::int64) a->length * (c + 1) / cols));
                    float p = 0.0f;
                    for (int i = from; i < juce::jmin (to, a->length); ++i)
                        p = juce::jmax (p, std::abs (a->left[(size_t) i]), std::abs (a->right[(size_t) i]));
                    values[(size_t) c] = p;
                }
            }
        }
        return values;
    }
};
} // namespace

// =============================================================================
// The rows: the input, the loop, its slice steps.

class LooperGrid : public juce::Component
{
public:
    explicit LooperGrid (LooperView& v) : view (v) {}

    // Small windows: just the two signal boxes.
    void setCompact (bool c) { compact = c; }

    void tick()
    {
        auto& proc = view.processor();
        const int hits = proc.trackHitCount (view.toolSlot(), kLoopTrack);
        flash = hits != lastHits ? 1.0f : flash * 0.82f;
        lastHits = hits;
        repaint();
    }

    void paint (juce::Graphics& g) override
    {
        if (compact)
        {
            paintCompact (g);
            return;
        }
        const auto geo = geometry();
        const auto& state = view.state();
        const int viewed = viewedPattern (state);

        paintInput (g, geo, state);
        paintLoop (g, geo, state, viewed);
        paintSlices (g, geo, state, viewed);

        // rulers along the top
        g.setFont (look::mono (9.0f));
        for (int s = 0; s < kNumSteps; ++s)
        {
            g.setColour (s % 4 == 0 ? look::ink.withAlpha (0.7f) : look::dim);
            g.drawText (look::pad (s + 1, 2), juce::Rectangle<float> (geo.stepX (s), 0.0f, geo.stepW, (float) kRulerHeight),
                        juce::Justification::centred);
        }
        g.setFont (look::mono (9.5f, true));
        g.setColour (state.queuedPattern >= 0 ? look::blue : look::ink);
        g.drawText ("SLOT P" + juce::String (viewed + 1) + (state.queuedPattern >= 0 ? " / QUEUED" : ""),
                    juce::Rectangle<float> (geo.nameX + 10.0f, 0.0f, geo.stepsX - geo.nameX, (float) kRulerHeight),
                    juce::Justification::centredLeft);
        g.setFont (look::mono (9.0f));
        g.setColour (look::dim);
        g.drawText ("SIGNAL // LATTICE", juce::Rectangle<float> (geo.signalX, 0.0f, geo.signalW, (float) kRulerHeight),
                    juce::Justification::centredLeft);
    }

    void mouseDown (const juce::MouseEvent& e) override
    {
        dragStep = -1;
        if (compact)
            return;
        const auto geo = geometry();
        const auto p = e.position;
        if (geo.rowAt (p.y) == kInputRow && p.x >= geo.toggleX)
        {
            view.toggleMonitor();
            return;
        }
        if (geo.rowAt (p.y) != kSliceRow)
            return;
        const int s = geo.stepAt (p.x);
        if (s < 0)
            return;
        if (e.mods.isPopupMenu())
        {
            view.send (CommandType::SetSlice, 0, index (s), (float) s);
            return;
        }
        dragStep = s;
        const int now = slices()[(size_t) s];
        startSlice = now < 0 ? s : now;
        dragged = false;
    }

    void mouseDrag (const juce::MouseEvent& e) override
    {
        if (dragStep < 0)
            return;
        const int dy = e.getDistanceFromDragStartY();
        if (! dragged && std::abs (dy) < 4)
            return;
        dragged = true;
        // up = a later slice, one every 8 pixels
        const int slice = juce::jlimit (0, kNumSteps - 1, startSlice + juce::roundToInt ((float) -dy / 8.0f));
        if (slice != slices()[(size_t) dragStep])
            view.send (CommandType::SetSlice, 0, index (dragStep), (float) slice);
    }

    void mouseUp (const juce::MouseEvent&) override
    {
        if (dragStep >= 0 && ! dragged)
        {
            // a click mutes the step, or brings it back
            const bool muted = slices()[(size_t) dragStep] < 0;
            view.send (CommandType::SetSlice, 0, index (dragStep), muted ? (float) dragStep : (float) kMuteSlice);
        }
        dragStep = -1;
    }

    void mouseWheelMove (const juce::MouseEvent& e, const juce::MouseWheelDetails& wheel) override
    {
        if (compact || juce::exactlyEqual (wheel.deltaY, 0.0f))
            return;
        const auto geo = geometry();
        const int s = geo.stepAt (e.position.x);
        if (geo.rowAt (e.position.y) != kSliceRow || s < 0)
            return;
        const int now = slices()[(size_t) s];
        const int slice = juce::jlimit (0, kNumSteps - 1, (now < 0 ? s : now) + (wheel.deltaY > 0.0f ? 1 : -1));
        view.send (CommandType::SetSlice, 0, index (s), (float) slice);
    }

private:
    static constexpr int kRulerHeight = 18;
    static constexpr int kInputRow = 0, kLoopRow = 1, kSliceRow = 2;

    int index (int step) const { return viewedPattern (view.state()) * kNumSteps + step; }
    const SlicePattern& slices() const
    {
        const auto& state = view.state();
        return state.slots[(size_t) viewedPattern (state)].slices;
    }

    struct Geometry
    {
        float width = 0, top = 0;
        std::array<float, 4> rowTop {};
        float nameX = 44, stepsX = 190, stepW = 40, signalX = 0, signalW = 0, toggleX = 0, toggleW = 40;

        float rowY (int r) const { return rowTop[(size_t) r]; }
        float rowH (int r) const { return rowTop[(size_t) r + 1] - rowTop[(size_t) r]; }
        float stepX (int s) const { return stepsX + stepW * (float) s; }
        int rowAt (float y) const
        {
            for (int r = 0; r < 3; ++r)
                if (y >= rowTop[(size_t) r] && y < rowTop[(size_t) r + 1])
                    return r;
            return -1;
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
        const float h = (float) getHeight() - g.top;
        g.rowTop = { g.top, g.top + h * 0.26f, g.top + h * 0.64f, (float) getHeight() };
        g.stepW = juce::jlimit (22.0f, 48.0f, (g.width - g.stepsX - 320.0f) / (float) kNumSteps);
        g.toggleX = g.width - g.toggleW;
        g.signalX = g.stepX (kNumSteps) + 24.0f;
        g.signalW = juce::jmin ((float) ModularSynthProcessor::kHistoryLength, g.toggleX - 12.0f - g.signalX);
        g.signalX = g.toggleX - 12.0f - g.signalW;
        return g;
    }

    static void paintIndex (juce::Graphics& g, juce::Rectangle<float> box, const juce::String& text, float flashAmount, bool on)
    {
        if (flashAmount > 0.02f)
        {
            g.setColour (look::ink.withAlpha (flashAmount));
            g.fillRect (box);
        }
        g.setColour (on ? look::ink : look::dim);
        g.drawRect (box, 1.0f);
        g.setFont (look::mono (11.0f, true));
        g.setColour (flashAmount > 0.5f ? look::background : look::ink);
        g.drawText (text, box, juce::Justification::centred);
    }

    void paintName (juce::Graphics& g, const Geometry& geo, int row, const juce::String& name, const juce::String& info)
    {
        const float y0 = geo.rowY (row), mid = y0 + geo.rowH (row) * 0.5f;
        g.setFont (look::mono (13.0f, true));
        g.setColour (look::ink);
        g.drawText (name, juce::Rectangle<float> (geo.nameX + 10.0f, mid - 20.0f, geo.stepsX - geo.nameX - 14.0f, 18.0f),
                    juce::Justification::bottomLeft, true);
        g.setFont (look::mono (9.5f));
        g.setColour (look::dim);
        g.drawText (info, juce::Rectangle<float> (geo.nameX + 10.0f, mid + 2.0f, geo.stepsX - geo.nameX - 14.0f, 14.0f),
                    juce::Justification::topLeft, true);
    }

    void paintSignal (juce::Graphics& g, juce::Rectangle<float> box, int track, float alpha)
    {
        auto& proc = view.processor();
        proc.copyTrackScope (view.toolSlot(), track, signal.scope(), SignalBox::kFftSize);
        signal.paint (g, box, proc.getEngineSampleRate(), 0.0f, {}, alpha);
    }

    // The input: its level over the last moments as a barcode across the
    // steps, its lattice, and the monitor switch.
    void paintInput (juce::Graphics& g, const Geometry& geo, const LooperState& state)
    {
        auto& proc = view.processor();
        const float y0 = geo.rowY (kInputRow), h = geo.rowH (kInputRow), mid = y0 + h * 0.5f;
        g.setColour (look::faint);
        g.fillRect (0.0f, y0, geo.width, 1.0f);
        paintIndex (g, { 4.0f, y0 + 6.0f, 30.0f, h - 12.0f }, "IN", 0.0f, state.monitor);
        paintName (g, geo, kInputRow, "INPUT", juce::String (loopInputName (state.input)) + " // " + formatDb (levelToGain (state.inputGain)));

        const float x0 = geo.stepsX, w = geo.stepX (kNumSteps) - x0;
        const int columns = juce::jmin (ModularSynthProcessor::kHistoryLength, (int) (w / 2.0f));
        for (int c = 0; c < columns; ++c)
        {
            const float v = juce::jlimit (0.0f, 1.0f, std::sqrt (proc.trackHistory (view.toolSlot(), kInputTrack, c)) * 1.2f);
            if (v < 0.02f)
                continue;
            const float lineH = juce::jmax (1.0f, v * (h - 16.0f));
            g.setColour ((v > 0.95f ? look::red : look::ink).withAlpha (0.25f + 0.75f * v));
            g.fillRect (x0 + w - 2.0f * (float) (c + 1), mid - lineH * 0.5f, 1.0f, lineH);
        }
        g.setColour (look::faint.brighter (0.2f));
        g.fillRect (x0, mid, w, 1.0f);

        paintSignal (g, { geo.signalX - 1.0f, y0 + 5.0f, geo.signalW + 2.0f, h - 10.0f }, kInputTrack, 1.0f);
        float peak = 0.0f;
        for (int c = 0; c < 8; ++c)
            peak = juce::jmax (peak, proc.trackHistory (view.toolSlot(), kInputTrack, c));
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

    // The whole loop as a barcode of its samples, a bar between each pair of
    // rulers, the playhead, and the recording as it goes.
    void paintLoop (juce::Graphics& g, const Geometry& geo, const LooperState& state, int viewed)
    {
        auto& proc = view.processor();
        const auto& slot = state.slots[(size_t) viewed];
        const auto* audio = slot.audio;
        const auto readout = proc.looperReadout (view.toolSlot());
        const float y0 = geo.rowY (kLoopRow), h = geo.rowH (kLoopRow), mid = y0 + h * 0.5f;
        g.setColour (look::faint);
        g.fillRect (0.0f, y0, geo.width, 1.0f);
        paintIndex (g, { 4.0f, y0 + 6.0f, 30.0f, h - 12.0f }, "LP", flash, audio != nullptr);

        juce::String info = "EMPTY // PRESS REC LOOP";
        if (audio != nullptr)
            info = barsText (audio->bars) + " // " + juce::String (audio->bpm, 1) + " BPM";
        paintName (g, geo, kLoopRow, "LOOP P" + juce::String (viewed + 1), info);
        if (state.masterFxAuto.any (viewed))
            paintTag (g, "AUTO", geo.stepsX - 46.0f, y0 + 6.0f, look::blue);

        const auto area = juce::Rectangle<float> (geo.stepsX, y0 + 8.0f, geo.stepX (kNumSteps) - geo.stepsX, h - 22.0f);
        g.setColour (look::faint.brighter (0.2f));
        g.fillRect (area.getX(), area.getCentreY(), area.getWidth(), 1.0f);

        const int bars = audio != nullptr ? audio->bars : slot.bars;
        if (audio != nullptr)
        {
            const int columns = (int) area.getWidth();
            const auto& peaks = loopPeaks.of (audio, columns);
            for (int c = 0; c < columns; ++c)
            {
                const float v = juce::jlimit (0.0f, 1.0f, std::sqrt (peaks[(size_t) c]));
                if (v < 0.01f)
                    continue;
                const float lineH = juce::jmax (1.0f, v * area.getHeight());
                g.setColour (look::ink.withAlpha (0.3f + 0.7f * v));
                g.fillRect (area.getX() + (float) c, area.getCentreY() - lineH * 0.5f, 1.0f, lineH);
            }
        }

        // bars, numbered underneath
        g.setFont (look::mono (8.5f));
        for (int b = 0; b <= bars; ++b)
        {
            const float x = area.getX() + area.getWidth() * (float) b / (float) bars;
            g.setColour (look::dim);
            g.fillRect (x, area.getY() - 2.0f, 1.0f, area.getHeight() + 6.0f);
            if (b < bars)
                g.drawText (juce::String (b + 1) + ".1", juce::Rectangle<float> (x + 3.0f, area.getBottom() + 1.0f, 40.0f, 12.0f),
                            juce::Justification::centredLeft);
        }

        // recording into this slot: red over what is done so far
        const bool recordingHere = readout.status != LooperEngine::Status::Idle && readout.slot == viewed;
        if (recordingHere)
        {
            const bool waiting = readout.status == LooperEngine::Status::Waiting;
            const bool counting = readout.status == LooperEngine::Status::CountIn;
            g.setColour (look::red.withAlpha (0.25f));
            g.fillRect (area.withWidth (area.getWidth() * (float) readout.progress));
            g.setColour (look::red);
            g.drawRect (area.expanded (2.0f), 1.0f);
            const juce::String what = readout.overdub ? "OVERDUB" : "REC";
            paintTag (g, waiting    ? what + " // NEXT BAR"
                         : counting ? what + " // COUNT-IN " + juce::String (readout.countBeats)
                                    : what + " " + juce::String (juce::roundToInt (readout.progress * 100.0)) + "%",
                      area.getX() + 4.0f, area.getY() + 2.0f, look::red);
        }

        // where the loop playing is
        if (readout.position >= 0.0 && proc.currentPattern (view.toolSlot()) == viewed)
        {
            const float x = area.getX() + area.getWidth() * (float) readout.position;
            g.setColour (look::red);
            g.fillRect (x - 0.5f, area.getY() - 4.0f, 1.0f, area.getHeight() + 8.0f);
        }

        paintSignal (g, { geo.signalX - 1.0f, y0 + 5.0f, geo.signalW + 2.0f, h - 10.0f }, kLoopTrack, 1.0f);
        float peak = 0.0f;
        for (int c = 0; c < 8; ++c)
            peak = juce::jmax (peak, proc.trackHistory (view.toolSlot(), kLoopTrack, c));
        g.setFont (look::mono (9.0f));
        g.setColour (peak > 1.0f ? look::red : look::dim);
        g.drawText (formatDb (peak), juce::Rectangle<float> (geo.signalX - 66.0f, y0 + 4.0f, 60.0f, 12.0f), juce::Justification::topRight);
        g.drawText (formatDb (levelToGain (state.masterLevel)), juce::Rectangle<float> (geo.toggleX, mid - 6.0f, geo.toggleW, 12.0f),
                    juce::Justification::centredRight);
    }

    // The slice steps: each a tag with the slice it plays (dim while it plays
    // its own), over a sliver of that slice's sound; and on the right where
    // every step's slice comes from.
    void paintSlices (juce::Graphics& g, const Geometry& geo, const LooperState& state, int viewed)
    {
        auto& proc = view.processor();
        const auto& slot = state.slots[(size_t) viewed];
        const auto* audio = slot.audio;
        const float y0 = geo.rowY (kSliceRow), h = geo.rowH (kSliceRow);
        g.setColour (look::faint);
        g.fillRect (0.0f, y0, geo.width, 1.0f);
        paintIndex (g, { 4.0f, y0 + 6.0f, 30.0f, h - 12.0f }, "SL", 0.0f, ! straightSlices (slot.slices));

        int moved = 0, muted = 0;
        for (int s = 0; s < kNumSteps; ++s)
        {
            muted += slot.slices[(size_t) s] < 0 ? 1 : 0;
            moved += slot.slices[(size_t) s] >= 0 && slot.slices[(size_t) s] != s ? 1 : 0;
        }
        paintName (g, geo, kSliceRow, "SLICES",
                   straightSlices (slot.slices) ? juce::String ("STRAIGHT")
                                                : look::pad (moved, 2) + " MOVED // " + look::pad (muted, 2) + " MUTED");

        // 16 slices of the loop's first bar, a few columns each
        const int perSlice = 10;
        const auto* peaks = audio != nullptr ? &slicePeaks.of (audio, perSlice * kNumSteps * audio->bars) : nullptr;
        const bool playing = proc.isPlaying() && proc.currentPattern (view.toolSlot()) == viewed;
        const int playStep = playing ? proc.currentStep() : -1;
        const float fontH = juce::jlimit (8.0f, 11.0f, geo.stepW / 2.6f);
        for (int s = 0; s < kNumSteps; ++s)
        {
            const int slice = slot.slices[(size_t) s];
            const auto cell = juce::Rectangle<float> (geo.stepX (s) + 2.0f, y0 + 8.0f, geo.stepW - 4.0f, h - 20.0f);
            const auto tag = cell.withHeight (fontH + 7.0f);

            if (peaks != nullptr && slice >= 0)
            {
                const auto wave = cell.withTrimmedTop (tag.getHeight() + 4.0f);
                const float colW = wave.getWidth() / (float) perSlice;
                for (int c = 0; c < perSlice; ++c)
                {
                    const float v = juce::jlimit (0.0f, 1.0f, std::sqrt ((*peaks)[(size_t) (slice * perSlice + c)]));
                    const float lineH = juce::jmax (1.0f, v * wave.getHeight());
                    g.setColour (look::ink.withAlpha (0.25f + 0.6f * v));
                    g.fillRect (wave.getX() + colW * (float) c, wave.getCentreY() - lineH * 0.5f, juce::jmax (1.0f, colW - 1.0f), lineH);
                }
            }

            g.setFont (look::mono (fontH, true));
            if (slice < 0)
            {
                g.setColour (look::faint.brighter (0.3f));
                g.drawRect (tag, 1.0f);
                g.setColour (look::dim);
                g.drawText ("--", tag, juce::Justification::centred);
            }
            else if (slice == s)
            {
                g.setColour (s == playStep ? look::ink : look::dim);
                g.drawRect (tag, 1.0f);
                g.drawText (look::pad (slice + 1, 2), tag, juce::Justification::centred);
            }
            else
            {
                g.setColour (look::ink);
                g.fillRect (tag);
                g.setColour (look::background);
                g.drawText (look::pad (slice + 1, 2), tag, juce::Justification::centred);
                if (s == playStep)
                {
                    g.setColour (look::ink);
                    g.drawRect (tag.expanded (2.0f), 1.0f);
                }
            }
            g.setColour (s % 4 == 0 ? look::dim : look::faint);
            g.fillRect (geo.stepX (s), y0 + h - 7.0f, 1.0f, 4.0f);
        }

        if (playStep >= 0)
        {
            const float x = geo.stepX (playStep) + geo.stepW * 0.5f;
            g.setColour (look::red);
            g.fillRect (x - 0.5f, y0 + 2.0f, 1.0f, h - 4.0f);
        }

        // where each step's slice comes from: steps along the top, slices
        // along the bottom, a line from each step to its slice
        const auto map = juce::Rectangle<float> (geo.signalX, y0 + 10.0f, geo.signalW, h - 24.0f);
        g.setColour (look::faint);
        g.drawRect (map.expanded (1.0f), 1.0f);
        g.setFont (look::mono (8.5f));
        g.setColour (look::dim);
        g.drawText ("STEP", map.withHeight (10.0f).translated (0.0f, -10.0f), juce::Justification::centredRight);
        g.drawText ("SLICE", map.withY (map.getBottom()).withHeight (10.0f), juce::Justification::centredRight);
        auto xAt = [&map] (int i) { return map.getX() + 6.0f + (map.getWidth() - 12.0f) * ((float) i + 0.5f) / (float) kNumSteps; };
        for (int s = 0; s < kNumSteps; ++s)
        {
            const int slice = slot.slices[(size_t) s];
            const float x0 = xAt (s);
            g.setColour (s == playStep ? look::red : look::ink);
            g.fillRect (x0 - 1.0f, map.getY() + 3.0f, 2.0f, 2.0f);
            if (slice < 0)
                continue;
            const float x1 = xAt (slice);
            g.setColour ((s == playStep ? look::red : look::ink).withAlpha (slice == s ? 0.35f : 0.9f));
            g.drawLine (x0, map.getY() + 5.0f, x1, map.getBottom() - 5.0f, 1.0f);
            g.fillRect (x1 - 1.0f, map.getBottom() - 5.0f, 2.0f, 2.0f);
        }
    }

    void paintCompact (juce::Graphics& g)
    {
        const float rowH = (float) getHeight() / 2.0f, w = (float) getWidth();
        const auto& state = view.state();
        for (int r = 0; r < 2; ++r)
        {
            const float y0 = rowH * (float) r;
            const int track = r == 0 ? kInputTrack : kLoopTrack;
            g.setColour (look::faint);
            g.fillRect (0.0f, y0, w, 1.0f);
            paintIndex (g, { 2.0f, y0 + 3.0f, 30.0f, rowH - 6.0f }, r == 0 ? "IN" : "LP", r == 0 ? 0.0f : flash,
                        r == 0 ? state.monitor : state.slots[(size_t) state.pattern].audio != nullptr);
            paintSignal (g, { 38.0f, y0 + 2.0f, w - 38.0f, rowH - 4.0f }, track, 1.0f);
        }
    }

    LooperView& view;
    SignalBox signal;
    Peaks loopPeaks, slicePeaks;
    int lastHits = 0;
    float flash = 0.0f;
    bool compact = false, dragged = false;
    int dragStep = -1, startSlice = 0;
};

// =============================================================================
// The input and mix controls, then the looper's effects rack.

class LooperPanel : public juce::Component
{
public:
    explicit LooperPanel (LooperView& v) : view (v)
    {
        for (auto* b : { &copyButton, &pasteButton, &randomButton, &straightButton, &clearAutoButton })
            addAndMakeVisible (*b);
        copyButton.onClick = [this] { view.copySlot(); };
        pasteButton.onClick = [this] { view.pasteSlot(); };
        randomButton.onClick = [this] { view.randomizeSlices(); };
        straightButton.onClick = [this] { view.straightenSlices(); };

        addAndMakeVisible (gain);
        gain.setLabel ("IN GAIN");
        gain.setDefault (LooperState().inputGain);
        gain.format = [] (float value) { return formatDb (levelToGain (value)); };
        gain.onChange = [this] (float value) { view.send (CommandType::SetInputGain, 0, 0, value); };

        addAndMakeVisible (level);
        level.setLabel ("LEVEL");
        level.setDefault (LooperState().masterLevel);
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

        auto& proc = view.processor();
        const int tool = view.toolSlot();
        FxRackControls::Automation a;
        a.moves = [this] { return &view.state().masterFxAuto; };
        a.pattern = [this, &proc, tool] { return proc.isPlaying() ? proc.currentPattern (tool) : viewedPattern (view.state()); };
        a.recording = [&proc] { return proc.isAutoArmed() && proc.isPlaying(); };
        a.step = [&proc] { return proc.currentStep(); };
        a.position = [&proc] { return proc.isPlaying() ? proc.stepPosition() : -1.0; };
        a.send = [this] (CommandType type, int index, float value) { view.send (type, -1, index, value); };
        fx.setAutomation (std::move (a));

        clearAutoButton.onClick = [this] {
            const int pattern = viewedPattern (view.state());
            view.send (CommandType::ClearFxAuto, -1, pattern, -1.0f);
            view.setStatus ("CLEARED THE LOOPER'S EFFECT AUTOMATION ON P" + juce::String (pattern + 1));
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
        clearAutoButton.setVisible (state.masterFxAuto.any (viewedPattern (state)));
        repaint (meterArea());
    }

    void paint (juce::Graphics& g) override
    {
        g.setColour (look::dim);
        g.fillRect (0, 0, getWidth(), 1);
        g.setFont (look::mono (11.0f, true));
        g.setColour (look::ink);
        g.drawText ("LOOPER RACK // THE LOOP + THE MONITORED INPUT", 0, 6, columnX (2), 18, juce::Justification::centredLeft);

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
        auto top = juce::Rectangle<int> (columnX (2), 4, getWidth() - columnX (2), 20);
        for (auto* b : { &copyButton, &pasteButton, &randomButton, &straightButton })
        {
            b->setBounds (top.removeFromLeft (b == &straightButton ? 88 : 72));
            top.removeFromLeft (4);
        }
        top.removeFromLeft (16);
        clearAutoButton.setBounds (top.removeFromLeft (100));

        gain.setBounds (columnX (0), y0, colW, rowH);
        level.setBounds (columnX (0), y0 + rowH, colW, rowH);
        pan.setBounds (columnX (0), y0 + 2 * rowH, colW, rowH);
        fx.layout ([this] (int c) { return columnX (c + 1); }, y0, rowH, colW);
    }

private:
    static constexpr int kColumns = 5;
    int columnX (int c) const { return c * (getWidth() + 24) / kColumns; }
    juce::Rectangle<int> meterArea() const { return { columnX (0), 48 + 3 * 20 + 10, columnX (1) - columnX (0) - 24, 36 }; }

    LooperView& view;
    DataButton copyButton { "COPY" }, pasteButton { "PASTE" }, randomButton { "RANDOM" }, straightButton { "STRAIGHT" };
    DataButton clearAutoButton { "CLEAR AUTO" };
    DataSlider gain, level, pan;
    FxRackControls fx;
    float hold = 0.0f;
};

// =============================================================================

LooperView::LooperView (ModularSynthProcessor& p, int t) : proc (p), tool (t)
{
    grid = std::make_unique<LooperGrid> (*this);
    panel = std::make_unique<LooperPanel> (*this);
    addAndMakeVisible (*grid);
    addAndMakeVisible (*panel);

    for (auto* b : { &recButton, &overdubButton, &barsButton, &countButton, &inputButton, &monitorButton, &dryButton, &clearButton,
                     &resetButton, &autoButton })
        addAndMakeVisible (*b);
    recButton.setAccent (look::red);
    overdubButton.setAccent (look::red);
    autoButton.setAccent (look::red);
    recButton.onClick = [this] { recordLoop (false); };
    overdubButton.onClick = [this] { recordLoop (true); };
    barsButton.onClick = [this] { showBarsMenu(); };
    inputButton.onClick = [this] { showInputMenu(); };
    monitorButton.onClick = [this] { toggleMonitor(); };
    dryButton.onClick = [this] { toggleMonitorDry(); };
    countButton.onClick = [this] { showCountInMenu(); };
    clearButton.onClick = [this] { clearSlot(); };
    resetButton.onClick = [this] { resetToDefaults(); };
    autoButton.onClick = [this] {
        proc.setAutoArmed (! proc.isAutoArmed());
        setStatus (proc.isAutoArmed() ? "AUTO ARMED // WHILE PLAYING, EFFECT MOVES ARE RECORDED INTO THE STEPS (RIGHT-CLICK A CONTROL TO CLEAR IT)"
                                      : "AUTO OFF // RECORDED MOVES KEEP PLAYING");
    };
    for (int i = 0; i < kNumPatterns; ++i)
    {
        auto& b = patternButtons[(size_t) i];
        addAndMakeVisible (b);
        b.onClick = [this, i] {
            if (juce::ModifierKeys::getCurrentModifiers().isShiftDown())
                copySlotTo (i);
            else
                selectPattern (i);
        };
    }

    for (auto* r : { &stepReadout, &loopReadout, &latencyReadout })
        addAndMakeVisible (*r);
    stepReadout.setLabel ("SLOT / STEP");
    loopReadout.setLabel ("LOOP");
    latencyReadout.setLabel ("LATENCY / TAKEN OFF");

    for (int s = 0; s < kNumPatterns; ++s)
        seenLayers[(size_t) s] = proc.loopLayers (tool, s);

    setWantsKeyboardFocus (true);
    setSize (1320, 820);
    panel->refresh();
}

LooperView::~LooperView() = default;

const LooperState& LooperView::state() const
{
    return proc.getStudioState().loopers[(size_t) looperIndex (tool)];
}

void LooperView::send (CommandType type, int track, int index, float value)
{
    Command c;
    c.type = type;
    c.tool = tool;
    c.track = track;
    c.index = index;
    c.value = value;
    proc.send (c);
}

void LooperView::setStatus (const juce::String& s)
{
    status = s;
    statusTime = juce::Time::getMillisecondCounter();
    repaint (0, getHeight() - kStatusHeight, getWidth(), kStatusHeight);
}

// ---- recording ----------------------------------------------------------------

void LooperView::recordLoop (bool overdub)
{
    const auto readout = proc.looperReadout (tool);
    if (readout.status != LooperEngine::Status::Idle)
    {
        proc.cancelLoopRecord (tool);
        setStatus ("CANCELLED // P" + juce::String (readout.slot + 1) + " KEEPS WHAT IT HAD");
        return;
    }

    const int slot = viewedPattern (state());
    const auto& ls = state().slots[(size_t) slot];
    const bool onTop = overdub && ls.audio != nullptr;
    proc.recordLoop (tool, slot, overdub);
    const bool wasPlaying = proc.isPlaying();
    if (! wasPlaying)
    {
        Command play;
        play.type = CommandType::Play;
        play.tool = kStudio;
        proc.send (play);
    }

    const int count = state().countIn;
    const juce::String when = (wasPlaying ? juce::String ("FROM THE NEXT BAR") : juce::String ("FROM NOW"))
                              + (count > 0 ? " AFTER " + barsText (count) + " OF COUNT-IN" : juce::String());
    if (onTop)
        setStatus ("OVERDUB P" + juce::String (slot + 1) + " " + when + ", ONCE ROUND " + barsText (ls.audio->bars)
                   + " // CTRL+Z TAKES THE LAYER OFF");
    else
        setStatus ("REC LOOP P" + juce::String (slot + 1) + " " + when + ", " + barsText (ls.bars)
                   + (ls.audio != nullptr ? " // REPLACES THE LOOP THERE (CTRL+Z BRINGS IT BACK)" : " // THEN IT PLAYS")
                   + " / PRESS AGAIN TO CANCEL");
}

void LooperView::toggleMonitor()
{
    const bool on = ! state().monitor;
    send (CommandType::SetLoopMonitor, 0, 0, on ? 1.0f : 0.0f);
    setStatus (on ? (state().monitorDry ? "MONITOR ON // YOU HEAR THE INPUT DRY (FX BUTTON = THROUGH THE RACK) // HEADPHONES AVOID FEEDBACK"
                                        : "MONITOR ON // YOU HEAR THE INPUT THROUGH THE LOOPER'S RACK (DRY BUTTON = DRY) // HEADPHONES AVOID FEEDBACK")
                  : "MONITOR OFF // THE INPUT IS ONLY RECORDED");
}

void LooperView::toggleMonitorDry()
{
    // Only what you hear changes: recordings are always dry, the loop then
    // plays through the rack.
    const bool dry = ! state().monitorDry;
    send (CommandType::SetMonitorDry, 0, 0, dry ? 1.0f : 0.0f);
    if (! state().monitor)
        send (CommandType::SetLoopMonitor, 0, 0, 1.0f);
    setStatus (dry ? "MONITOR DRY // YOU HEAR THE INPUT AS IT COMES IN, WITHOUT THE RACK (SHIFT+M SWITCHES)"
                   : "MONITOR FX // YOU HEAR THE INPUT THROUGH THE LOOPER'S RACK (SHIFT+M SWITCHES)");
}

void LooperView::showCountInMenu()
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

void LooperView::showInputMenu()
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
                       + (r - 1 == (int) LoopInput::Stereo ? " AS A STEREO PAIR" : " ON BOTH SIDES"));
        }
    });
}

void LooperView::showBarsMenu()
{
    const int slot = viewedPattern (state());
    juce::PopupMenu m;
    m.addSectionHeader ("P" + juce::String (slot + 1) + " // LENGTH OF THE NEXT RECORDING");
    for (int bars : kLoopLengths)
        m.addItem (bars, barsText (bars), true, state().slots[(size_t) slot].bars == bars);
    m.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (&barsButton), [this, slot] (int r) {
        if (r <= 0)
            return;
        send (CommandType::SetLoopBars, 0, slot, (float) r);
        const auto* audio = state().slots[(size_t) slot].audio;
        setStatus ("P" + juce::String (slot + 1) + " RECORDS " + barsText (r)
                   + (audio != nullptr && audio->bars != r ? " NEXT TIME // THE LOOP THERE STAYS " + barsText (audio->bars) : juce::String()));
    });
}

// ---- slots ----------------------------------------------------------------------

void LooperView::selectPattern (int pattern)
{
    const bool queue = proc.isPlaying();
    send (CommandType::SelectPattern, 0, pattern, queue ? 1.0f : 0.0f);
    const auto& l = state();
    if (queue && l.queuedPattern >= 0)
        setStatus ("P" + juce::String (pattern + 1) + " QUEUED / STARTS AT THE NEXT BAR");
}

LooperView::SlotCopy LooperView::slotCopy (int slot) const
{
    const auto& ls = state().slots[(size_t) slot];
    return { proc.holdLoop (ls.audio), ls.bars, ls.slices };
}

void LooperView::writeSlot (int slot, const SlotCopy& copy)
{
    const auto& now = state().slots[(size_t) slot];
    if (now.audio != copy.audio.get())
    {
        Command c;
        c.type = CommandType::SetLoopAudio;
        c.tool = tool;
        c.index = slot;
        c.loop = const_cast<LoopAudio*> (copy.audio.get()); // never written to through this
        proc.send (c);
    }
    if (now.bars != copy.bars)
        send (CommandType::SetLoopBars, 0, slot, (float) copy.bars);
    for (int s = 0; s < kNumSteps; ++s)
        if (state().slots[(size_t) slot].slices[(size_t) s] != copy.slices[(size_t) s])
            send (CommandType::SetSlice, 0, slot * kNumSteps + s, (float) copy.slices[(size_t) s]);
}

void LooperView::copySlotTo (int pattern)
{
    const int from = viewedPattern (state());
    if (from == pattern)
        return;
    remember();
    undoText = "P" + juce::String (pattern + 1);
    writeSlot (pattern, slotCopy (from));
    copyFxAutomation ([this] (CommandType type, int rack, int index, float value) { send (type, rack, index, value); }, -1,
                      state().masterFxAuto, from, pattern);
    setStatus ("COPIED P" + juce::String (from + 1) + " TO P" + juce::String (pattern + 1) + " / CTRL+Z TO UNDO");
}

void LooperView::copySlot()
{
    const int slot = viewedPattern (state());
    clipboard = slotCopy (slot);
    clipboardSource = "P" + juce::String (slot + 1);
    setStatus ("COPIED " + clipboardSource + " (LOOP AND SLICES) / PICK A SLOT (P1-P4) AND PASTE (CTRL+V)");
}

void LooperView::pasteSlot()
{
    if (! clipboard.has_value())
    {
        setStatus ("NOTHING COPIED YET / PRESS COPY (CTRL+C) ON A SLOT");
        return;
    }
    const int slot = viewedPattern (state());
    remember();
    undoText = "P" + juce::String (slot + 1);
    writeSlot (slot, *clipboard);
    setStatus ("PASTED " + clipboardSource + " INTO P" + juce::String (slot + 1) + " / CTRL+Z TO UNDO");
}

void LooperView::randomizeSlices()
{
    // Mostly the loop as it is, with stutters (a slice again), jumps back to
    // a beat, the odd silence: the same bar, rearranged.
    const int slot = viewedPattern (state());
    remember();
    undoText = "P" + juce::String (slot + 1) + " SLICES";
    auto& rng = juce::Random::getSystemRandom();
    SlicePattern slices = LoopSlot::straightSlices();
    for (int s = 1; s < kNumSteps; ++s)
    {
        const float r = rng.nextFloat();
        if (r < 0.18f)
            slices[(size_t) s] = slices[(size_t) (s - 1)] >= 0 ? slices[(size_t) (s - 1)] : (std::int8_t) s; // stutter
        else if (r < 0.30f)
            slices[(size_t) s] = (std::int8_t) (4 * rng.nextInt (4)); // back to a beat
        else if (r < 0.38f)
            slices[(size_t) s] = (std::int8_t) rng.nextInt (kNumSteps);
        else if (r < 0.46f && s % 4 != 0)
            slices[(size_t) s] = kMuteSlice;
    }
    for (int s = 0; s < kNumSteps; ++s)
        if (state().slots[(size_t) slot].slices[(size_t) s] != slices[(size_t) s])
            send (CommandType::SetSlice, 0, slot * kNumSteps + s, (float) slices[(size_t) s]);
    setStatus ("RANDOM SLICES ON P" + juce::String (slot + 1) + " / PRESS AGAIN FOR ANOTHER / CTRL+Z TO UNDO");
}

void LooperView::straightenSlices()
{
    const int slot = viewedPattern (state());
    remember();
    undoText = "P" + juce::String (slot + 1) + " SLICES";
    for (int s = 0; s < kNumSteps; ++s)
        if (state().slots[(size_t) slot].slices[(size_t) s] != s)
            send (CommandType::SetSlice, 0, slot * kNumSteps + s, (float) s);
    setStatus ("P" + juce::String (slot + 1) + " SLICES STRAIGHT AGAIN / CTRL+Z TO UNDO");
}

LooperView::Snapshot LooperView::snapshot() const
{
    Snapshot snap;
    snap.state = state();
    for (int s = 0; s < kNumPatterns; ++s)
        snap.held[(size_t) s] = proc.holdLoop (snap.state.slots[(size_t) s].audio);
    return snap;
}

void LooperView::remember()
{
    lastUndo = snapshot();
}

void LooperView::applyLooper (const Snapshot& target, bool slots)
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
    set (CommandType::SetInputGain, 0, 0, now.inputGain, want.inputGain);
    set (CommandType::SetMasterLevel, 0, 0, now.masterLevel, want.masterLevel);
    set (CommandType::SetPan, 0, 0, now.pan, want.pan);
    for (int i = 0; i < kNumFxParams; ++i)
        set (CommandType::SetFxParam, -1, i, state().masterFx[(size_t) i], want.masterFx[(size_t) i]);
    if (! slots)
        return;
    for (int s = 0; s < kNumPatterns; ++s)
    {
        const auto& ws = want.slots[(size_t) s];
        writeSlot (s, { target.held[(size_t) s], ws.bars, ws.slices });
    }
    if (state().masterFxAuto != want.masterFxAuto)
        for (int p = 0; p < kNumPatterns; ++p)
            copyFxAutomation ([this] (CommandType type, int rack, int index, float value) { send (type, rack, index, value); }, -1,
                              want.masterFxAuto, p, p);
}

void LooperView::clearSlot()
{
    // Empties the slot on screen and puts the looper's rack back to its
    // defaults. The other slots stay.
    const int slot = viewedPattern (state());
    auto target = snapshot();
    target.state.slots[(size_t) slot].audio = nullptr;
    target.held[(size_t) slot].reset();
    target.state.slots[(size_t) slot].slices = LoopSlot::straightSlices();
    target.state.masterFx = defaultFxParams();
    target.state.masterFxAuto.clear (slot);

    remember();
    undoText = "P" + juce::String (slot + 1) + " AND THE EFFECTS";
    applyLooper (target, true);
    setStatus ("CLEARED P" + juce::String (slot + 1) + " + RESET THE EFFECTS / CTRL+Z TO BRING IT BACK");
}

void LooperView::resetToDefaults()
{
    // Monitor, gain, level, pan and the rack back to how a new looper starts.
    // The loops, slices and the input choice stay.
    remember();
    undoText = "THE SETTINGS BEFORE THE RESET";
    Snapshot target;
    target.state = makeDefaultLooper();
    applyLooper (target, false);
    setStatus ("RESET THE LOOPER'S SETTINGS / LOOPS AND SLICES KEPT / CTRL+Z TO UNDO");
}

void LooperView::undo()
{
    const int slot = viewedPattern (state());
    if (lastUndo.has_value())
    {
        applyLooper (*lastUndo, true);
        lastUndo.reset();
        setStatus ("UNDONE / " + undoText + " RESTORED");
        return;
    }
    if (proc.undoLoopLayer (tool, slot))
    {
        seenLayers[(size_t) slot] = proc.loopLayers (tool, slot);
        setStatus ("TOOK OFF THE LAST RECORDING ON P" + juce::String (slot + 1) + " // "
                   + juce::String (proc.loopLayers (tool, slot)) + " MORE TO GO BACK");
        return;
    }
    setStatus ("NOTHING TO UNDO");
}

// ---- keys -------------------------------------------------------------------------

bool LooperView::keyPressed (const juce::KeyPress& key)
{
    const auto mods = key.getModifiers();
    const auto c = (juce::juce_wchar) juce::CharacterFunctions::toLowerCase (key.getTextCharacter());
    auto unhandled = [this, &key] { return onUnhandledKey != nullptr && onUnhandledKey (key); };

    if (mods.isCommandDown())
    {
        if (key.getKeyCode() == 'Z') { undo(); return true; }
        if (key.getKeyCode() == 'C') { copySlot(); return true; }
        if (key.getKeyCode() == 'V') { pasteSlot(); return true; }
        if (key.getKeyCode() == 'R') { randomizeSlices(); return true; }
        return unhandled();
    }

    for (int p = 0; p < kNumPatterns; ++p)
        if (key.getKeyCode() == juce::KeyPress::F1Key + p)
        {
            if (mods.isShiftDown())
                copySlotTo (p);
            else
                selectPattern (p);
            return true;
        }

    if (key == juce::KeyPress::deleteKey || key == juce::KeyPress::backspaceKey) { clearSlot(); return true; }
    if (c == 'l') { recordLoop (false); return true; }
    if (c == 'o') { recordLoop (true); return true; }
    if (c == 'm' && mods.isShiftDown()) { toggleMonitorDry(); return true; }
    if (c == 'm') { toggleMonitor(); return true; }
    return unhandled();
}

// ---- timer, drawing -------------------------------------------------------------

void LooperView::tick()
{
    const auto& l = state();
    const int slot = viewedPattern (l);
    for (int p = 0; p < kNumPatterns; ++p)
    {
        patternButtons[(size_t) p].setOn (l.pattern == p);
        patternButtons[(size_t) p].setMarked (l.queuedPattern == p);
        // a finished recording replaces the undo before it
        const int layers = proc.loopLayers (tool, p);
        if (layers > seenLayers[(size_t) p])
            lastUndo.reset();
        seenLayers[(size_t) p] = layers;
    }

    const auto readout = proc.looperReadout (tool);
    const bool busy = readout.status != LooperEngine::Status::Idle;
    // waiting for the bar: blink
    const bool blink = (readout.status != LooperEngine::Status::Waiting && readout.status != LooperEngine::Status::CountIn)
                       || (juce::Time::getMillisecondCounter() / 250) % 2 == 0;
    recButton.setOn (busy && ! readout.overdub && blink);
    overdubButton.setOn (busy && readout.overdub && blink);
    const auto* audio = l.slots[(size_t) slot].audio;
    barsButton.setText (barsText (l.slots[(size_t) slot].bars));
    inputButton.setText (loopInputName (l.input));
    monitorButton.setOn (l.monitor);
    dryButton.setText (l.monitorDry ? "DRY" : "FX");
    dryButton.setOn (l.monitor);
    countButton.setText (l.countIn == 0 ? juce::String ("NO COUNT") : "COUNT " + juce::String (l.countIn));
    autoButton.setOn (proc.isAutoArmed());

    stepReadout.setText ("P" + juce::String (l.pattern + 1) + (l.queuedPattern >= 0 ? ">P" + juce::String (l.queuedPattern + 1) : juce::String())
                         + "  " + look::pad (proc.currentStep() + 1, 2) + "/" + juce::String (kNumSteps));
    juce::String loopText = audio == nullptr ? "EMPTY" : barsText (audio->bars);
    if (busy)
        loopText = (readout.overdub ? "DUB " : "REC ")
                   + (readout.status == LooperEngine::Status::Waiting   ? juce::String ("WAIT")
                      : readout.status == LooperEngine::Status::CountIn ? "IN " + juce::String (readout.countBeats)
                                                                         : juce::String (juce::roundToInt (readout.progress * 100.0)) + "%")
                   + " P" + juce::String (readout.slot + 1);
    else if (readout.position >= 0.0 && audio != nullptr && proc.currentPattern (tool) == slot)
        loopText = "BAR " + juce::String (juce::jmin (audio->bars, 1 + (int) (readout.position * audio->bars))) + "/" + juce::String (audio->bars);
    loopReadout.setText (loopText);
    loopReadout.setColour (busy ? look::red : look::ink);
    const double sr = proc.getEngineSampleRate();
    latencyReadout.setText (juce::String (1000.0 * proc.getInputLatency() / sr, 1) + " MS");

    grid->tick();
    panel->refresh();
    repaint (0, getHeight() - kStatusHeight, getWidth(), kStatusHeight);
}

void LooperView::paint (juce::Graphics& g)
{
    g.fillAll (look::background);
    if (isCompact())
        return;

    g.setColour (look::ink);
    g.setFont (look::mono (16.0f, true));
    g.drawText (look::pad (tool + 1, 2) + " " + toolTitle (tool), kMargin, 10, 300, 20, juce::Justification::centredLeft);
    g.setFont (look::mono (9.5f));
    g.setColour (look::dim);
    g.drawText ("AUDIO IN // 4 SLOTS x 1-8 BARS // ON THE STUDIO CLOCK // 16 SLICE STEPS A BAR", kMargin, 30, 560, 14,
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
    g.drawText ("L REC LOOP  O OVERDUB  M MONITOR  SHIFT+M DRY/FX  CLICK SLICE = MUTE  DRAG UP/DOWN = ANOTHER SLICE  RIGHT-CLICK = STRAIGHT  CTRL+Z = UNDO / LAST LAYER",
                statusArea, juce::Justification::centredRight, true);
}

bool LooperView::isCompact() const
{
    return getWidth() < kCompactWidth || getHeight() < kCompactHeight;
}

void LooperView::resized()
{
    // Smaller windows fold away the rack, then everything but the pattern
    // buttons and the signal boxes.
    const bool compact = isCompact();
    grid->setCompact (compact);
    panel->setVisible (! compact && getHeight() >= kPanelMinHeight);
    for (juce::Component* c : { (juce::Component*) &recButton, (juce::Component*) &overdubButton, (juce::Component*) &barsButton,
                                (juce::Component*) &inputButton, (juce::Component*) &monitorButton, (juce::Component*) &dryButton,
                                (juce::Component*) &countButton, (juce::Component*) &clearButton,
                                (juce::Component*) &resetButton, (juce::Component*) &autoButton, (juce::Component*) &stepReadout,
                                (juce::Component*) &loopReadout, (juce::Component*) &latencyReadout })
        c->setVisible (! compact);
    if (compact)
    {
        auto area = getLocalBounds();
        auto buttons = area.removeFromTop (34).reduced (8, 6);
        for (auto& b : patternButtons)
        {
            b.setBounds (buttons.removeFromLeft (44));
            buttons.removeFromLeft (4);
        }
        grid->setBounds (area.reduced (6, 2));
        return;
    }

    auto area = getLocalBounds();
    auto header = area.removeFromTop (kHeaderHeight).reduced (kMargin, 0);

    auto buttons = header.removeFromBottom (30).withTrimmedBottom (8);
    for (auto& b : patternButtons)
    {
        b.setBounds (buttons.removeFromLeft (44));
        buttons.removeFromLeft (4);
    }
    buttons.removeFromLeft (8);
    recButton.setBounds (buttons.removeFromLeft (84));
    buttons.removeFromLeft (6);
    overdubButton.setBounds (buttons.removeFromLeft (84));
    buttons.removeFromLeft (6);
    barsButton.setBounds (buttons.removeFromLeft (72));
    buttons.removeFromLeft (6);
    countButton.setBounds (buttons.removeFromLeft (80));
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

    auto readouts = header.withTrimmedTop (8).withTrimmedLeft (juce::jmax (620, header.getWidth() - 390));
    stepReadout.setBounds (readouts.removeFromLeft (110).withHeight (36));
    readouts.removeFromLeft (8);
    loopReadout.setBounds (readouts.removeFromLeft (110).withHeight (36));
    readouts.removeFromLeft (8);
    latencyReadout.setBounds (readouts.removeFromLeft (150).withHeight (36));

    area.removeFromBottom (kStatusHeight);
    if (panel->isVisible())
        panel->setBounds (area.removeFromBottom (kPanelHeight).reduced (kMargin, 0));
    grid->setBounds (area.reduced (kMargin, 6));
}
