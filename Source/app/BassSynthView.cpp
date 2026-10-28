#include "BassSynthView.h"

#include "SignalBox.h"

using namespace ms;

namespace
{
constexpr int kHeaderHeight = 72;
constexpr int kPanelHeight = 250;
constexpr int kStatusHeight = 24;
constexpr int kMargin = 16;

// The computer keyboard as a piano, chromatic from C: Z S X D C V G B H N J M
// is one octave, , L . ; / carry on into the next.
constexpr const char* kPianoKeys = "zsxdcvgbhnjm,l.;/";

juce::String noteText (int note)
{
    char buf[8];
    noteName (note, buf, (int) sizeof (buf));
    return buf;
}

juce::String bassParamText (BassSound sound, int index, float v)
{
    char buf[32];
    formatBassParam (sound, index, v, buf, (int) sizeof (buf));
    return buf;
}

// Cut group letters: A for the first group with more than one row, B...
std::array<char, kBassRows> groupLetters (const BassState& b)
{
    const auto groups = cutGroups (b);
    std::array<int, kBassRows> size {};
    for (int g : groups)
        ++size[(size_t) g];
    std::array<char, kBassRows> letters {};
    std::array<char, kBassRows> letterOfGroup {};
    char next = 'A';
    for (int r = 0; r < kBassRows; ++r)
    {
        const int g = groups[(size_t) r];
        if (size[(size_t) g] < 2)
            continue;
        if (letterOfGroup[(size_t) g] == 0)
            letterOfGroup[(size_t) g] = next++;
        letters[(size_t) r] = letterOfGroup[(size_t) g];
    }
    return letters;
}
} // namespace

// =============================================================================
// The rows: notes as tags on the steps, the patch bay, the signal panels.

class BassGrid : public juce::Component
{
public:
    explicit BassGrid (BassSynthView& v) : view (v) {}

    void tick()
    {
        auto& proc = view.processor();
        for (int r = 0; r < kBassRows; ++r)
        {
            const int hits = proc.trackHitCount (view.toolSlot(), r);
            if (hits != lastHits[(size_t) r])
            {
                lastHits[(size_t) r] = hits;
                flash[(size_t) r] = 1.0f;
            }
            else
            {
                flash[(size_t) r] *= 0.82f;
            }
        }
        repaint();
    }

    void paint (juce::Graphics& g) override
    {
        auto& proc = view.processor();
        const auto& state = view.state();
        const auto geo = geometry();
        const int viewed = viewedPattern (state);
        const bool playing = proc.isPlaying() && proc.currentPattern (view.toolSlot()) == viewed;
        const int playStep = proc.currentStep();
        const auto letters = groupLetters (state);

        for (int r = 0; r < kBassRows; ++r)
            paintRow (g, geo, r, state, viewed, playing ? playStep : -1, letters[(size_t) r]);
        paintBus (g, geo, state);
        paintCables (g, geo, state);

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
        g.drawText ("PATTERN P" + juce::String (viewed + 1) + (state.queuedPattern >= 0 ? " / QUEUED" : "") + " // "
                        + keyName (state.key) + " " + scaleName (state.scale),
                    juce::Rectangle<float> (geo.nameX + 10.0f, 0.0f, geo.stepsX - geo.nameX + 120.0f, (float) kRulerHeight),
                    juce::Justification::centredLeft);
        g.setFont (look::mono (9.0f));
        g.setColour (look::dim);
        g.drawText ("CUT", juce::Rectangle<float> (geo.jackX, 0.0f, geo.jackW, (float) kRulerHeight), juce::Justification::centredLeft);
        g.drawText ("SIGNAL // NOTE LATTICE", juce::Rectangle<float> (geo.barcodeX, 0.0f, geo.barcodeW, (float) kRulerHeight),
                    juce::Justification::centredLeft);

        if (playing)
        {
            const float x = geo.stepX (playStep) + geo.stepW * 0.5f;
            g.setColour (look::red);
            g.fillRect (x - 0.5f, (float) kRulerHeight - 4.0f, 1.0f, geo.rowY (kBassRows) - (float) kRulerHeight + 4.0f);
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
        if (row == kBassRows)
        {
            view.selectRow (-1);
            return;
        }

        const auto& state = view.state();
        const auto& br = state.rows[(size_t) row];
        const int pattern = viewedPattern (state);

        // the index box plays the row's note, like a pad
        if (p.x < geo.nameX)
        {
            view.selectRow (row);
            view.auditionNote (row, view.noteForNewStep (row, 0), juce::jlimit (0.3f, 1.0f, 1.15f - (p.y - geo.rowY (row)) / geo.rowH * 0.8f));
            auditioning = row;
            return;
        }

        if (p.x < geo.stepsX - 4.0f)
        {
            view.selectRow (row);
            if (e.mods.isPopupMenu())
                view.showSoundMenu (row);
            return;
        }

        if (const int s = geo.stepAt (p.x); s >= 0)
        {
            view.selectRow (row);
            const bool lit = br.velocity[(size_t) pattern][(size_t) s] > 0.0f;
            if (e.mods.isPopupMenu())
            {
                if (lit)
                    view.send (CommandType::SetStep, row, index (s), 0.0f);
                return;
            }

            dragRow = row;
            dragStep = s;
            if (lit)
            {
                dragMode = DragMode::PendingOff;
                startNote = br.notes[(size_t) pattern][(size_t) s];
            }
            else
            {
                // a new note: the row's last note, one step long, full velocity
                dragMode = DragMode::PendingEdit;
                startNote = view.noteForNewStep (row, s);
                view.send (CommandType::SetNote, row, index (s), (float) startNote);
                view.send (CommandType::SetLength, row, index (s), 1.0f);
                view.send (CommandType::SetStep, row, index (s), 1.0f);
            }
            startLength = view.state().rows[(size_t) row].lengths[(size_t) pattern][(size_t) s];
            startVelocity = view.state().rows[(size_t) row].velocity[(size_t) pattern][(size_t) s];
            shownNote = startNote;
            if (! view.processor().isPlaying())
            {
                view.auditionNote (row, startNote);
                auditioning = row;
            }
            return;
        }

        if (const int jack = geo.jackAt (p); jack >= 0)
        {
            view.selectRow (jack);
            if (e.mods.isPopupMenu())
            {
                unplugAll (jack);
                return;
            }
            dragMode = DragMode::Cable;
            dragRow = jack;
            cableEnd = p;
            return;
        }

        if (p.x >= geo.muteX && p.x < geo.muteX + geo.toggleW)
            view.send (CommandType::SetMute, row, 0, br.mute ? 0.0f : 1.0f);
        else if (p.x >= geo.soloX && p.x < geo.soloX + geo.toggleW)
            view.send (CommandType::SetSolo, row, 0, br.solo ? 0.0f : 1.0f);
        else
            view.selectRow (row);
    }

    void mouseDrag (const juce::MouseEvent& e) override
    {
        const auto geo = geometry();
        if (dragMode == DragMode::Cable)
        {
            cableEnd = e.position;
            repaint();
            return;
        }
        if (dragMode != DragMode::PendingOff && dragMode != DragMode::PendingEdit && dragMode != DragMode::Pitch
            && dragMode != DragMode::Velocity && dragMode != DragMode::Length)
            return;

        const int dx = e.getDistanceFromDragStartX(), dy = e.getDistanceFromDragStartY();
        if (dragMode == DragMode::PendingOff || dragMode == DragMode::PendingEdit)
        {
            if (std::abs (dx) > 8 && std::abs (dx) > std::abs (dy))
                dragMode = DragMode::Length;
            else if (std::abs (dy) > 3)
                dragMode = e.mods.isShiftDown() ? DragMode::Velocity : DragMode::Pitch;
            else
                return;
        }

        const auto& state = view.state();
        if (dragMode == DragMode::Pitch)
        {
            // up = higher, one scale step every 7 pixels
            const int note = stepInScale (startNote, state.key, state.scale, juce::roundToInt ((float) -dy / 7.0f));
            if (note != shownNote)
            {
                shownNote = note;
                view.send (CommandType::SetNote, dragRow, index (dragStep), (float) note);
                if (! view.processor().isPlaying())
                {
                    view.auditionNote (dragRow, note);
                    auditioning = dragRow;
                }
            }
        }
        else if (dragMode == DragMode::Velocity)
        {
            const float v = juce::jlimit (0.05f, 1.0f, startVelocity - (float) dy / 90.0f);
            view.send (CommandType::SetStep, dragRow, index (dragStep), v);
        }
        else if (dragMode == DragMode::Length)
        {
            const int length = juce::jlimit (1, kMaxStepLength, startLength + juce::roundToInt ((float) dx / geo.stepW));
            view.send (CommandType::SetLength, dragRow, index (dragStep), (float) length);
        }
    }

    void mouseUp (const juce::MouseEvent& e) override
    {
        if (auditioning >= 0)
        {
            view.releaseNote (auditioning);
            auditioning = -1;
        }
        if (dragMode == DragMode::PendingOff)
            view.send (CommandType::SetStep, dragRow, index (dragStep), 0.0f);
        if (dragMode == DragMode::Cable)
        {
            const int target = geometry().jackAt (e.position, 16.0f);
            plug (dragRow, target);
            repaint();
        }
        dragMode = DragMode::None;
    }

    void mouseWheelMove (const juce::MouseEvent& e, const juce::MouseWheelDetails& wheel) override
    {
        // the wheel over a note moves it through the scale
        const auto geo = geometry();
        const int row = geo.rowAt (e.position.y), s = geo.stepAt (e.position.x);
        if (row < 0 || row >= kBassRows || s < 0)
            return;
        const auto& state = view.state();
        const int pattern = viewedPattern (state);
        const auto& br = state.rows[(size_t) row];
        if (br.velocity[(size_t) pattern][(size_t) s] <= 0.0f || juce::exactlyEqual (wheel.deltaY, 0.0f))
            return;
        const int note = stepInScale (br.notes[(size_t) pattern][(size_t) s], state.key, state.scale, wheel.deltaY > 0.0f ? 1 : -1);
        view.send (CommandType::SetNote, row, index (s), (float) note);
    }

    void mouseMove (const juce::MouseEvent& e) override
    {
        const int jack = geometry().jackAt (e.position);
        if (jack != hoverJack)
        {
            hoverJack = jack;
            repaint();
        }
    }

    void mouseExit (const juce::MouseEvent&) override
    {
        hoverJack = -1;
        repaint();
    }

    void mouseDoubleClick (const juce::MouseEvent& e) override
    {
        const auto geo = geometry();
        const int row = geo.rowAt (e.position.y);
        if (row >= 0 && row < kBassRows && e.position.x >= geo.nameX && e.position.x < geo.stepsX - 4.0f)
            view.showSoundMenu (row);
    }

private:
    static constexpr int kRulerHeight = 18;

    int index (int step) const { return viewedPattern (view.state()) * kNumSteps + step; }

    // A cable from one row's jack to another's. Dropped on its own jack or on
    // nothing, the row's cable comes out.
    void plug (int from, int to)
    {
        const auto& state = view.state();
        if (to == from || to < 0)
        {
            if (state.rows[(size_t) from].cable >= 0)
            {
                view.send (CommandType::SetCable, from, -1, 0.0f);
                view.setStatus ("UNPLUGGED " + rowLabel (from) + " // IT RINGS ON ITS OWN UNLESS ANOTHER CABLE REACHES IT");
            }
            return;
        }
        if (state.rows[(size_t) to].cable == from)
        {
            view.setStatus (rowLabel (from) + " AND " + rowLabel (to) + " ARE ALREADY JOINED");
            return;
        }
        view.send (CommandType::SetCable, from, to, 0.0f);
        view.setStatus ("PATCHED " + rowLabel (from) + " TO " + rowLabel (to) + " // A NOTE ON ONE NOW CUTS THE OTHER");
    }

    void unplugAll (int row)
    {
        const auto& state = view.state();
        bool any = false;
        for (int r = 0; r < kBassRows; ++r)
            if (r == row ? state.rows[(size_t) r].cable >= 0 : state.rows[(size_t) r].cable == row)
            {
                view.send (CommandType::SetCable, r, -1, 0.0f);
                any = true;
            }
        if (any)
            view.setStatus ("UNPLUGGED EVERY CABLE AT " + rowLabel (row));
    }

    juce::String rowLabel (int row) const
    {
        return look::pad (row + 1, 2) + " " + bassSoundName (view.state().rows[(size_t) row].sound);
    }

    struct Geometry
    {
        float width = 0, rowH = 0, top = 0;
        float nameX = 44, stepsX = 190, stepW = 40, jackX = 0, jackW = 64, barcodeX = 0, barcodeW = 0;
        float muteX = 0, soloX = 0, toggleW = 26;

        float rowY (int r) const { return top + rowH * (float) r; }
        float stepX (int s) const { return stepsX + stepW * (float) s; }
        juce::Point<float> jack (int r) const { return { jackX + 20.0f, rowY (r) + rowH * 0.5f }; }
        int rowAt (float y) const
        {
            const int r = (int) std::floor ((y - top) / rowH);
            return y >= top && r >= 0 && r <= kBassRows ? r : -1;
        }
        int stepAt (float x) const
        {
            const int s = (int) std::floor ((x - stepsX) / stepW);
            return x >= stepsX && s >= 0 && s < kNumSteps ? s : -1;
        }
        int jackAt (juce::Point<float> p, float radius = 11.0f) const
        {
            for (int r = 0; r < kBassRows; ++r)
                if (p.getDistanceFrom (jack (r)) <= radius)
                    return r;
            return -1;
        }
    };

    Geometry geometry() const
    {
        Geometry g;
        g.width = (float) getWidth();
        g.top = (float) kRulerHeight;
        g.rowH = ((float) getHeight() - g.top) / (float) (kBassRows + 1);
        g.stepW = juce::jlimit (22.0f, 44.0f, (g.width - g.stepsX - 300.0f - g.jackW) / (float) kNumSteps);
        g.jackX = g.stepX (kNumSteps) + 14.0f;
        g.soloX = g.width - g.toggleW;
        g.muteX = g.soloX - g.toggleW - 6.0f;
        const float signalX = g.jackX + g.jackW + 14.0f;
        g.barcodeW = juce::jmin ((float) ModularSynthProcessor::kHistoryLength, g.muteX - 16.0f - signalX);
        g.barcodeX = g.muteX - 16.0f - g.barcodeW;
        return g;
    }

    static void paintIndex (juce::Graphics& g, juce::Rectangle<float> box, const juce::String& text, float flashAmount, bool selected)
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

    static void paintToggle (juce::Graphics& g, float x, float y, float w, const char* text, bool on, bool rowSelected)
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

    // A lit step: a white tag holding its note name, a line on to where the
    // note lets go, and its velocity (0-127) underneath.
    static void paintNote (juce::Graphics& g, juce::Rectangle<float> cell, float endX, int note, float velocity, bool now, float alpha,
                           bool showVelocity)
    {
        const float fontH = juce::jlimit (8.0f, 11.0f, cell.getWidth() / 2.2f);
        const float tagH = fontH + 7.0f;
        const auto tag = juce::Rectangle<float> (cell.getX(), cell.getCentreY() - tagH * 0.5f - (showVelocity ? 4.0f : 0.0f),
                                                 cell.getWidth(), tagH);

        // the held part of the note
        if (endX > tag.getRight() + 1.0f)
        {
            g.setColour (look::ink.withAlpha (alpha * 0.8f));
            g.fillRect (tag.getRight(), tag.getCentreY() - 0.5f, endX - tag.getRight(), 1.0f);
            g.fillRect (endX - 1.0f, tag.getCentreY() - 3.0f, 1.0f, 6.0f);
        }

        g.setColour (look::ink.withAlpha (alpha));
        if (now)
            g.drawRect (tag, 1.0f);
        else
            g.fillRect (tag);
        g.setColour (now ? look::ink.withAlpha (alpha) : look::background);
        g.setFont (look::mono (fontH, true));
        g.drawText (noteText (note), tag, juce::Justification::centred, false);

        if (showVelocity)
        {
            g.setFont (look::mono (8.5f));
            g.setColour (look::dim.withAlpha (alpha));
            g.drawText (look::pad (juce::jlimit (1, 127, juce::roundToInt (velocity * 127.0f)), 3),
                        juce::Rectangle<float> (tag.getX(), tag.getBottom() + 1.0f, tag.getWidth(), 10.0f), juce::Justification::centred);
        }
    }

    void paintRow (juce::Graphics& g, const Geometry& geo, int r, const BassState& state, int pattern, int playStep, char groupLetter)
    {
        auto& proc = view.processor();
        const auto& br = state.rows[(size_t) r];
        const float y0 = geo.rowY (r), h = geo.rowH, mid = y0 + h * 0.5f;
        const bool selected = view.selectedRow() == r;
        const float alpha = br.mute ? 0.35f : 1.0f;

        g.setColour (look::faint);
        g.fillRect (0.0f, y0, geo.width, 1.0f);
        paintIndex (g, { 4.0f, y0 + 6.0f, 30.0f, h - 12.0f }, look::pad (r + 1, 2), flash[(size_t) r], selected);

        if (selected)
        {
            g.setColour (look::ink);
            g.fillRect (geo.nameX, y0 + 8.0f, 2.0f, h - 16.0f);
        }
        g.setFont (look::mono (13.0f, true));
        g.setColour (look::ink.withAlpha (alpha * (selected ? 1.0f : 0.8f)));
        g.drawText (bassSoundName (br.sound), juce::Rectangle<float> (geo.nameX + 10.0f, y0 + 4.0f, geo.stepsX - geo.nameX - 14.0f, h * 0.5f - 2.0f),
                    juce::Justification::bottomLeft, true);
        g.setFont (look::mono (9.5f));
        g.setColour (look::dim);
        g.drawText (juce::String (bassParamName (br.sound, bpTone)) + " " + bassParamText (br.sound, bpTone, br.params[bpTone]),
                    juce::Rectangle<float> (geo.nameX + 10.0f, mid + 2.0f, geo.stepsX - geo.nameX - 14.0f, h * 0.5f - 6.0f),
                    juce::Justification::topLeft, true);

        // steps: the data line, then the notes, then each note's held line
        const float stepsEnd = geo.stepX (kNumSteps);
        g.setColour (look::faint.brighter (0.2f));
        g.fillRect (geo.stepsX, mid, stepsEnd - geo.stepsX, 1.0f);
        for (int s = 0; s < kNumSteps; ++s)
        {
            const float x = geo.stepX (s);
            g.setColour (s % 4 == 0 ? look::dim : look::faint);
            g.fillRect (x, y0 + h - 7.0f, 1.0f, 4.0f);
        }
        const bool showVelocity = h >= 44.0f;
        for (int s = 0; s < kNumSteps; ++s)
        {
            const float v = br.velocity[(size_t) pattern][(size_t) s];
            if (v <= 0.0f)
                continue;
            const int length = br.lengths[(size_t) pattern][(size_t) s];
            const float endX = geo.stepX (juce::jmin (kNumSteps, s + length)) - 3.0f;
            paintNote (g, { geo.stepX (s) + 2.0f, y0 + 5.0f, geo.stepW - 4.0f, h - 14.0f }, endX, br.notes[(size_t) pattern][(size_t) s], v,
                       s == playStep, alpha, showVelocity);
        }

        // the jack, with its cut group's letter
        const auto jack = geo.jack (r);
        const bool patched = br.cable >= 0 || groupLetter != 0;
        g.setColour (hoverJack == r ? look::ink : look::dim);
        g.drawEllipse (juce::Rectangle<float> (13.0f, 13.0f).withCentre (jack), 1.0f);
        g.setColour (patched ? look::ink : look::faint.brighter (0.3f));
        g.fillEllipse (juce::Rectangle<float> (patched ? 6.0f : 4.0f, patched ? 6.0f : 4.0f).withCentre (jack));
        g.setFont (look::mono (9.5f, true));
        g.setColour (look::ink);
        if (groupLetter != 0)
            g.drawText (juce::String::charToString ((juce::juce_wchar) groupLetter),
                        juce::Rectangle<float> (geo.jackX, mid - 7.0f, 10.0f, 14.0f), juce::Justification::centredLeft);

        // signal panel: the lattice of the row's newest samples, on its note
        const auto bx = geo.barcodeX, bw = geo.barcodeW;
        proc.copyTrackScope (view.toolSlot(), r, signal.scope(), SignalBox::kFftSize);
        const int note = juce::roundToInt (proc.trackNote (view.toolSlot(), r));
        signal.paint (g, juce::Rectangle<float> (bx - 1.0f, y0 + 5.0f, bw + 2.0f, h - 10.0f), proc.getEngineSampleRate(),
                      noteHz ((float) note), noteText (note), alpha);
        float lastPeak = 0.0f;
        for (int c = 0; c < 8; ++c)
            lastPeak = juce::jmax (lastPeak, proc.trackHistory (view.toolSlot(), r, c));
        g.setFont (look::mono (9.0f));
        g.setColour (lastPeak > 1.0f ? look::red : look::dim);
        g.drawText (formatDb (lastPeak), juce::Rectangle<float> (bx - 66.0f, y0 + 4.0f, 60.0f, 12.0f), juce::Justification::topRight);

        paintToggle (g, geo.muteX, mid - 9.0f, geo.toggleW, "M", br.mute, selected);
        paintToggle (g, geo.soloX, mid - 9.0f, geo.toggleW, "S", br.solo, selected);
    }

    // Patch cables: thin white lines that bow out to the right of the jacks,
    // further the more rows they span. The one being dragged is blue.
    void paintCables (juce::Graphics& g, const Geometry& geo, const BassState& state)
    {
        auto cable = [&geo] (juce::Point<float> a, juce::Point<float> b) {
            const float bow = 10.0f + 4.0f * std::abs (b.y - a.y) / geo.rowH;
            juce::Path p;
            p.startNewSubPath (a);
            p.cubicTo (a.translated (bow, 0.0f), b.translated (bow, 0.0f), b);
            return p;
        };
        for (int r = 0; r < kBassRows; ++r)
        {
            const int to = state.rows[(size_t) r].cable;
            if (to < 0 || to >= kBassRows || (dragMode == DragMode::Cable && dragRow == r))
                continue;
            g.setColour (look::ink.withAlpha (0.9f));
            g.strokePath (cable (geo.jack (r), geo.jack (to)), juce::PathStrokeType (1.2f));
            for (int end : { r, to })
                g.fillRect (juce::Rectangle<float> (4.0f, 4.0f).withCentre (geo.jack (end)));
        }

        if (dragMode == DragMode::Cable)
        {
            const auto from = geo.jack (dragRow);
            const int target = geo.jackAt (cableEnd, 16.0f);
            const auto end = target >= 0 && target != dragRow ? geo.jack (target) : cableEnd;
            juce::Path p;
            p.startNewSubPath (from);
            const float bow = 10.0f + 4.0f * std::abs (end.y - from.y) / geo.rowH;
            p.cubicTo (from.translated (bow, 0.0f), end.translated (bow, 0.0f), end);
            g.setColour (look::blue);
            g.strokePath (p, juce::PathStrokeType (1.2f));
            g.fillRect (juce::Rectangle<float> (5.0f, 5.0f).withCentre (end));
        }
    }

    void paintBus (juce::Graphics& g, const Geometry& geo, const BassState& state)
    {
        auto& proc = view.processor();
        const float y0 = geo.rowY (kBassRows), h = geo.rowH, mid = y0 + h * 0.5f;
        const bool selected = view.selectedRow() < 0;

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
        g.drawText ("BASS BUS", juce::Rectangle<float> (geo.nameX + 10.0f, y0 + 4.0f, 120.0f, h * 0.5f - 2.0f), juce::Justification::bottomLeft);
        g.setFont (look::mono (9.5f));
        g.setColour (look::dim);
        g.drawText ("SUM + RACK " + formatDb (levelToGain (state.masterLevel)),
                    juce::Rectangle<float> (geo.nameX + 10.0f, mid + 2.0f, geo.stepsX - geo.nameX - 14.0f, h * 0.5f - 6.0f),
                    juce::Justification::topLeft, true);

        juce::String data;
        for (int i : { fxCrushBits, fxDistDrive, fxDistMix, fxCutoff, fxDelayTime, fxDelayMix, fxGrainTime, fxGrainMix,
                       fxReverbTime, fxReverbMix, fxChorusMix, fxPsyStages, fxPsyMix })
        {
            char buf[32];
            formatFxParam (state.masterFx, i, buf, (int) sizeof (buf));
            data << fxParamName (i) << " " << buf << "   ";
        }
        g.drawFittedText (data, juce::Rectangle<float> (geo.stepsX, y0 + 6.0f, geo.stepX (kNumSteps) - geo.stepsX, h - 12.0f).toNearestInt(),
                          juce::Justification::centredLeft, 3, 1.0f);

        paintPeakMeter (g, geo.barcodeX, mid, geo.barcodeW, proc.busPeak (view.toolSlot()), busHold);
    }

    enum class DragMode
    {
        None,
        PendingOff,  // clicked a lit step: off unless it is dragged
        PendingEdit, // lit a step: stays lit, can be dragged
        Pitch,
        Velocity,
        Length,
        Cable
    };

    BassSynthView& view;
    std::array<int, kBassRows> lastHits {};
    std::array<float, kBassRows> flash {};
    SignalBox signal;
    float busHold = 0.0f;
    DragMode dragMode = DragMode::None;
    int dragRow = 0, dragStep = 0, startNote = 36, startLength = 1, shownNote = 36;
    float startVelocity = 1.0f;
    int auditioning = -1, hoverJack = -1;
    juce::Point<float> cableEnd;
};

// =============================================================================
// The selected row's controls (or the bus's): sound and filter, envelope and
// mix, then the effects rack.

class BassPanel : public juce::Component
{
public:
    explicit BassPanel (BassSynthView& v) : view (v)
    {
        addAndMakeVisible (typeButton);
        typeButton.onClick = [this] {
            if (view.selectedRow() >= 0)
                view.showSoundMenu (view.selectedRow());
        };
        for (auto* b : { &copyButton, &copyPatternButton, &pasteButton, &randomButton })
            addAndMakeVisible (*b);
        copyButton.onClick = [this] { view.copySequence (false); };
        copyPatternButton.onClick = [this] { view.copySequence (true); };
        pasteButton.onClick = [this] { view.pasteSequence(); };
        randomButton.onClick = [this] { view.randomizeSequence (view.selectedRow()); };

        for (int i = 0; i < kNumBassParams; ++i)
        {
            auto& s = params[(size_t) i];
            addAndMakeVisible (s);
            s.onChange = [this, i] (float value) { view.send (CommandType::SetVoiceParam, view.selectedRow(), i, value); };
        }

        addAndMakeVisible (level);
        level.setLabel ("LEVEL");
        level.format = [] (float value) { return formatDb (levelToGain (value)); };
        level.onChange = [this] (float value) {
            if (view.selectedRow() < 0)
                view.send (CommandType::SetMasterLevel, 0, 0, value);
            else
                view.send (CommandType::SetLevel, view.selectedRow(), 0, value);
        };

        addAndMakeVisible (pan);
        pan.setLabel ("PAN");
        pan.setDefault (0.5f);
        pan.setBipolar (true);
        pan.format = [] (float value) { return formatPan (value); };
        pan.onChange = [this] (float value) { view.send (CommandType::SetPan, view.selectedRow(), 0, value); };

        fx.attach (*this, [this] { return shownFx(); },
                   [this] (int i, float value) { view.send (CommandType::SetFxParam, view.selectedRow(), i, value); });
    }

    void refresh()
    {
        const int r = view.selectedRow();
        const auto& state = view.state();
        const bool bus = r < 0;
        const auto sound = bus ? BassSound::Count : state.rows[(size_t) r].sound;

        if (r != shownRow || sound != shownSound)
        {
            shownRow = r;
            shownSound = sound;
            for (juce::Component* c : { (juce::Component*) &typeButton, (juce::Component*) &copyButton,
                                        (juce::Component*) &randomButton, (juce::Component*) &pan })
                c->setVisible (! bus);
            level.setDefault (bus ? BassState().masterLevel : BassRow().level);

            const auto defaults = bus ? BassParams {} : defaultBassParams (sound);
            for (int i = 0; i < kNumBassParams; ++i)
            {
                auto& s = params[(size_t) i];
                s.setVisible (! bus);
                if (bus)
                    continue;
                s.setLabel (bassParamName (sound, i));
                s.setDefault (defaults[(size_t) i]);
                s.format = [sound, i] (float value) { return bassParamText (sound, i, value); };
                s.repaint();
            }
            if (! bus)
                typeButton.setText ("SOUND: " + juce::String (bassSoundName (sound)));
            resized();
            repaint();
        }

        if (bus)
        {
            level.setValue (state.masterLevel);
            fx.setValues (state.masterFx);
            return;
        }
        const auto& br = state.rows[(size_t) r];
        for (int i = 0; i < kNumBassParams; ++i)
            params[(size_t) i].setValue (br.params[(size_t) i]);
        level.setValue (br.level);
        pan.setValue (br.pan);
        fx.setValues (br.fx);
        repaint (envelopeArea());
    }

    void paint (juce::Graphics& g) override
    {
        g.setColour (look::dim);
        g.fillRect (0, 0, getWidth(), 1);

        const int r = view.selectedRow();
        const auto& state = view.state();
        const auto title = r < 0 ? juce::String ("BASS BUS // ALL 8 ROWS + EFFECTS RACK")
                                 : look::pad (r + 1, 2) + " " + bassSoundName (state.rows[(size_t) r].sound) + " // PARAMETERS";
        g.setFont (look::mono (11.0f, true));
        g.setColour (look::ink);
        g.drawText (title, 0, 6, columnX (2), 18, juce::Justification::centredLeft);

        g.setFont (look::mono (9.5f));
        g.setColour (look::dim);
        for (int c = 0; c < kColumns; ++c)
        {
            const char* name = c == 0 ? (r < 0 ? "LEVEL" : "SOUND + FILTER") : c == 1 ? (r < 0 ? "" : "ENVELOPE + MIX")
                                                                                     : FxRackControls::groupName (c - 2);
            g.drawText (name, columnX (c), 30, columnX (1) - 24, 14, juce::Justification::centredLeft);
        }
        if (r >= 0)
            paintEnvelope (g, state.rows[(size_t) r]);
    }

    void resized() override
    {
        const int rowH = 20, y0 = 48;
        const int colW = columnX (1) - columnX (0) - 24;

        auto top = juce::Rectangle<int> (columnX (2), 4, getWidth() - columnX (2), 20);
        if (view.selectedRow() >= 0)
        {
            typeButton.setBounds (top.removeFromLeft (150));
            top.removeFromLeft (20);
            copyButton.setBounds (top.removeFromLeft (84));
            top.removeFromLeft (4);
        }
        copyPatternButton.setBounds (top.removeFromLeft (112));
        top.removeFromLeft (4);
        pasteButton.setBounds (top.removeFromLeft (64));
        top.removeFromLeft (4);
        randomButton.setBounds (top.removeFromLeft (76));

        // column 0: TONE + filter + drive; column 1: glide, ADSR, level, pan
        for (int i = 0; i <= bpDrive; ++i)
            params[(size_t) i].setBounds (columnX (0), y0 + i * rowH, colW, rowH);
        for (int i = bpGlide; i < kNumBassParams; ++i)
            params[(size_t) i].setBounds (columnX (1), y0 + (i - bpGlide) * rowH, colW, rowH);
        if (view.selectedRow() < 0)
            level.setBounds (columnX (0), y0, colW, rowH);
        else
            level.setBounds (columnX (1), y0 + 5 * rowH + 8, colW, rowH);
        pan.setBounds (columnX (1), y0 + 6 * rowH + 8, colW, rowH);

        fx.layout ([this] (int c) { return columnX (c + 2); }, y0, rowH, colW);
    }

private:
    static constexpr int kColumns = 6;
    int columnX (int c) const { return c * (getWidth() + 24) / kColumns; }

    juce::Rectangle<int> envelopeArea() const
    {
        return { columnX (0), 48 + 6 * 20 + 14, columnX (1) - columnX (0) - 24, 52 };
    }

    // The amplitude envelope drawn as a data line: attack, decay, a held
    // sustain, release, each segment labelled with its time.
    void paintEnvelope (juce::Graphics& g, const BassRow& br)
    {
        const auto area = envelopeArea().toFloat();
        const float a = bassParamValue (br.sound, bpAttack, br.params[bpAttack]);
        const float d = bassParamValue (br.sound, bpDecay, br.params[bpDecay]);
        const float s = bassParamValue (br.sound, bpSustain, br.params[bpSustain]);
        const float rel = bassParamValue (br.sound, bpRelease, br.params[bpRelease]);
        // squash the times so a 2 ms attack and a 4 s release both read
        auto w = [] (float ms) { return std::sqrt (ms); };
        const float hold = w (250.0f);
        const float total = w (a) + w (d) + hold + w (rel);
        const float scale = area.getWidth() / total;
        const float x0 = area.getX(), top = area.getY() + 4.0f, bottom = area.getBottom() - 12.0f;
        const float x1 = x0 + w (a) * scale, x2 = x1 + w (d) * scale, x3 = x2 + hold * scale, x4 = x3 + w (rel) * scale;
        const float sy = bottom - (bottom - top) * s;

        g.setColour (look::faint);
        g.fillRect (area.getX(), bottom, area.getWidth(), 1.0f);
        for (float x : { x1, x2, x3 })
            g.fillRect (x, top, 1.0f, bottom - top);

        juce::Path p;
        p.startNewSubPath (x0, bottom);
        p.lineTo (x1, top);
        p.lineTo (x2, sy);
        p.lineTo (x3, sy);
        p.lineTo (x4, bottom);
        g.setColour (look::ink);
        g.strokePath (p, juce::PathStrokeType (1.0f));

        g.setFont (look::mono (8.5f));
        g.setColour (look::dim);
        const char* labels[] = { "A", "D", "S", "R" };
        const float xs[] = { x0, x1, x2, x3, x4 };
        for (int i = 0; i < 4; ++i)
            g.drawText (labels[i], juce::Rectangle<float> (xs[i], bottom + 1.0f, juce::jmax (8.0f, xs[i + 1] - xs[i]), 10.0f),
                        juce::Justification::centred);
    }

    FxParams shownFx() const
    {
        const auto& state = view.state();
        const int r = view.selectedRow();
        return r < 0 ? state.masterFx : state.rows[(size_t) r].fx;
    }

    BassSynthView& view;
    DataButton typeButton { "SOUND" };
    DataButton copyButton { "COPY ROW" }, copyPatternButton { "COPY PATTERN" }, pasteButton { "PASTE" }, randomButton { "RANDOM" };
    std::array<DataSlider, kNumBassParams> params;
    DataSlider level, pan;
    FxRackControls fx;
    int shownRow = -2;
    BassSound shownSound = BassSound::Count;
};

// =============================================================================

BassSynthView::BassSynthView (ModularSynthProcessor& p, int t) : proc (p), tool (t)
{
    grid = std::make_unique<BassGrid> (*this);
    panel = std::make_unique<BassPanel> (*this);
    addAndMakeVisible (*grid);
    addAndMakeVisible (*panel);

    for (auto* b : { &clearButton, &resetButton, &keyButton, &scaleButton })
        addAndMakeVisible (*b);
    clearButton.onClick = [this] { clearPattern(); };
    resetButton.onClick = [this] { resetToDefaults(); };
    keyButton.onClick = [this] { showKeyMenu(); };
    scaleButton.onClick = [this] { showScaleMenu(); };
    for (int i = 0; i < kNumPatterns; ++i)
    {
        auto& b = patternButtons[(size_t) i];
        addAndMakeVisible (b);
        b.onClick = [this, i] {
            if (juce::ModifierKeys::getCurrentModifiers().isShiftDown())
                copyPatternTo (i);
            else
                selectPattern (i);
        };
    }

    for (auto* r : { &stepReadout, &octaveReadout, &busReadout })
        addAndMakeVisible (*r);
    stepReadout.setLabel ("PATTERN / STEP");
    octaveReadout.setLabel ("KEYS Z-M / [ ]");
    octaveReadout.onDrag = [this] (float px, bool) {
        if (std::abs (px) >= 1.0f)
            octave = juce::jlimit (1, 4, octave + (px > 0.0f ? 1 : -1));
    };
    busReadout.setLabel ("BASS BUS");

    setWantsKeyboardFocus (true);
    setSize (1320, 820);
    panel->refresh();
}

BassSynthView::~BassSynthView() = default;

const BassState& BassSynthView::state() const
{
    return proc.getStudioState().bass[(size_t) bassIndex (tool)];
}

void BassSynthView::send (CommandType type, int track, int index, float value)
{
    Command c;
    c.type = type;
    c.tool = tool;
    c.track = track;
    c.index = index;
    c.value = value;
    proc.send (c);
}

void BassSynthView::selectRow (int row)
{
    if (selected != row)
    {
        selected = row;
        panel->refresh();
        panel->repaint();
    }
}

void BassSynthView::setStatus (const juce::String& s)
{
    status = s;
    statusTime = juce::Time::getMillisecondCounter();
    repaint (0, getHeight() - kStatusHeight, getWidth(), kStatusHeight);
}

void BassSynthView::auditionNote (int row, int note, float velocity)
{
    send (CommandType::Trigger, row, note, velocity);
}

void BassSynthView::releaseNote (int row)
{
    send (CommandType::NoteOff, row, 0, 0.0f);
}

int BassSynthView::noteForNewStep (int row, int step) const
{
    const auto& b = state();
    const int pattern = viewedPattern (b);
    const auto& br = b.rows[(size_t) row];
    // the nearest lit step before it (wrapping round the bar), else the key's root
    for (int back = 1; back <= kNumSteps; ++back)
    {
        const int s = (step - back + kNumSteps) % kNumSteps;
        if (br.velocity[(size_t) pattern][(size_t) s] > 0.0f)
            return br.notes[(size_t) pattern][(size_t) s];
    }
    return snapToScale (36 + b.key, b.key, b.scale);
}

void BassSynthView::showSoundMenu (int row)
{
    juce::PopupMenu m;
    m.addSectionHeader (look::pad (row + 1, 2) + " SOUND");
    const auto& br = state().rows[(size_t) row];
    for (int s = 0; s < kNumBassSounds; ++s)
        m.addItem (1 + s, bassSoundName ((BassSound) s), true, br.sound == (BassSound) s);
    m.addSectionHeader ("SEQUENCE");
    m.addItem (300, "COPY THIS ROW  (CTRL+C)");
    m.addItem (303, "COPY WHOLE PATTERN  (CTRL+SHIFT+C)");
    m.addItem (301, "PASTE  (CTRL+V)", clipboard.has_value());
    m.addItem (302, "RANDOM IN THE SCALE  (CTRL+R)");

    m.showMenuAsync (juce::PopupMenu::Options().withMousePosition(), [this, row] (int r) {
        if (r >= 300)
            selectRow (row);
        if (r == 300)
            copySequence (false);
        else if (r == 303)
            copySequence (true);
        else if (r == 301)
            pasteSequence();
        else if (r == 302)
            randomizeSequence (row);
        else if (r > 0)
            send (CommandType::SetVoiceType, row, r - 1, 0.0f);
        panel->refresh();
    });
}

void BassSynthView::showKeyMenu()
{
    juce::PopupMenu m;
    m.addSectionHeader ("KEY // EVERY NOTE MOVES INTO IT");
    for (int k = 0; k < 12; ++k)
        m.addItem (1 + k, keyName (k), true, state().key == k);
    m.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (&keyButton), [this] (int r) {
        if (r <= 0)
            return;
        remember();
        undoText = "THE KEY AND NOTES";
        send (CommandType::SetKey, 0, r - 1, 0.0f);
        setStatus ("KEY " + juce::String (keyName (r - 1)) + " " + scaleName (state().scale) + " // NOTES SNAPPED INTO IT / CTRL+Z TO UNDO");
    });
}

void BassSynthView::showScaleMenu()
{
    juce::PopupMenu m;
    m.addSectionHeader ("SCALE // EVERY NOTE SNAPS INTO IT");
    for (int s = 0; s < kNumScales; ++s)
        m.addItem (1 + s, scaleName (s), true, state().scale == s);
    m.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (&scaleButton), [this] (int r) {
        if (r <= 0)
            return;
        remember();
        undoText = "THE SCALE AND NOTES";
        send (CommandType::SetScale, 0, r - 1, 0.0f);
        setStatus ("SCALE " + juce::String (keyName (state().key)) + " " + scaleName (r - 1)
                   + (r == 1 ? " // ANY NOTE GOES" : " // NOTES SNAPPED INTO IT") + " / CTRL+Z TO UNDO");
    });
}

void BassSynthView::selectPattern (int pattern)
{
    const bool queue = proc.isPlaying();
    send (CommandType::SelectPattern, 0, pattern, queue ? 1.0f : 0.0f);
    const auto& b = state();
    if (queue && b.queuedPattern >= 0)
        setStatus ("P" + juce::String (pattern + 1) + " QUEUED / STARTS WHEN P" + juce::String (b.pattern + 1) + " ENDS");
}

BassSynthView::RowSequence BassSynthView::rowSequence (int row, int pattern) const
{
    const auto& br = state().rows[(size_t) row];
    return { br.velocity[(size_t) pattern], br.notes[(size_t) pattern], br.lengths[(size_t) pattern] };
}

void BassSynthView::writeRowSequence (int row, int pattern, const RowSequence& seq)
{
    // the note and length before the velocity, so a step lights with its note
    const auto now = rowSequence (row, pattern);
    for (int s = 0; s < kNumSteps; ++s)
    {
        const int index = pattern * kNumSteps + s;
        if (now.notes[(size_t) s] != seq.notes[(size_t) s])
            send (CommandType::SetNote, row, index, (float) seq.notes[(size_t) s]);
        if (now.lengths[(size_t) s] != seq.lengths[(size_t) s])
            send (CommandType::SetLength, row, index, (float) seq.lengths[(size_t) s]);
        if (! juce::exactlyEqual (now.velocity[(size_t) s], seq.velocity[(size_t) s]))
            send (CommandType::SetStep, row, index, seq.velocity[(size_t) s]);
    }
}

void BassSynthView::copyPatternTo (int pattern)
{
    const int from = viewedPattern (state());
    if (from == pattern)
        return;
    remember();
    undoText = "P" + juce::String (pattern + 1);
    for (int r = 0; r < kBassRows; ++r)
        writeRowSequence (r, pattern, rowSequence (r, from));
    setStatus ("COPIED P" + juce::String (from + 1) + " TO P" + juce::String (pattern + 1) + " / CTRL+Z TO UNDO");
}

void BassSynthView::remember()
{
    lastUndo = state();
}

void BassSynthView::applyBass (const BassState& target, bool sequences)
{
    const auto& now = state();
    auto set = [this] (CommandType type, int track, int index, float was, float want) {
        if (! juce::exactlyEqual (was, want))
            send (type, track, index, want);
    };

    // a different key or scale: free the notes first, so none are snapped on the way
    const bool rescale = sequences && (now.key != target.key || now.scale != target.scale);
    if (rescale)
        send (CommandType::SetScale, 0, 0, 0.0f);

    for (int r = 0; r < kBassRows; ++r)
    {
        const auto& want = target.rows[(size_t) r];
        const auto& was = state().rows[(size_t) r]; // follows each command sent
        if (was.sound != want.sound)
            send (CommandType::SetVoiceType, r, (int) want.sound, 0.0f); // resets its controls
        for (int i = 0; i < kNumBassParams; ++i)
            set (CommandType::SetVoiceParam, r, i, was.params[(size_t) i], want.params[(size_t) i]);
        set (CommandType::SetLevel, r, 0, was.level, want.level);
        set (CommandType::SetPan, r, 0, was.pan, want.pan);
        set (CommandType::SetMute, r, 0, was.mute ? 1.0f : 0.0f, want.mute ? 1.0f : 0.0f);
        set (CommandType::SetSolo, r, 0, was.solo ? 1.0f : 0.0f, want.solo ? 1.0f : 0.0f);
        for (int i = 0; i < kNumFxParams; ++i)
            set (CommandType::SetFxParam, r, i, was.fx[(size_t) i], want.fx[(size_t) i]);
        if (sequences)
        {
            if (was.cable != want.cable)
                send (CommandType::SetCable, r, want.cable, 0.0f);
            for (int p = 0; p < kNumPatterns; ++p)
                writeRowSequence (r, p, { want.velocity[(size_t) p], want.notes[(size_t) p], want.lengths[(size_t) p] });
        }
    }
    for (int i = 0; i < kNumFxParams; ++i)
        set (CommandType::SetFxParam, -1, i, state().masterFx[(size_t) i], target.masterFx[(size_t) i]);
    set (CommandType::SetMasterLevel, 0, 0, state().masterLevel, target.masterLevel);

    if (rescale)
    {
        send (CommandType::SetKey, 0, target.key, 0.0f);
        send (CommandType::SetScale, 0, target.scale, 0.0f);
    }
}

void BassSynthView::clearPattern()
{
    // Empties the pattern on screen and puts every effects rack (the 8 rows
    // and the bus) back to its defaults. Sounds, notes elsewhere and cables stay.
    const int pattern = viewedPattern (state());
    auto target = state();
    const auto defaults = defaultFxParams();
    for (auto& row : target.rows)
    {
        row.velocity[(size_t) pattern].fill (0.0f);
        row.fx = defaults;
    }
    target.masterFx = defaults;

    remember();
    undoText = "P" + juce::String (pattern + 1) + " AND THE EFFECTS";
    applyBass (target, true);
    setStatus ("CLEARED P" + juce::String (pattern + 1) + " + RESET ALL EFFECTS / CTRL+Z TO BRING IT BACK");
}

void BassSynthView::resetToDefaults()
{
    // Every row's sound, controls, level, pan, mute, solo and effects, and
    // the bass bus, back to how a new bass synth starts. Sequences, the key,
    // the scale and the cables stay.
    remember();
    undoText = "THE SETTINGS BEFORE THE RESET";
    applyBass (makeDefaultBass(), false);
    panel->refresh();
    setStatus ("RESET ALL SETTINGS / SEQUENCES, SCALE AND CABLES KEPT / CTRL+Z TO UNDO");
}

void BassSynthView::undo()
{
    if (! lastUndo.has_value())
    {
        setStatus ("NOTHING TO UNDO");
        return;
    }
    applyBass (*lastUndo, true);
    lastUndo.reset();
    panel->refresh();
    setStatus ("UNDONE / " + undoText + " RESTORED");
}

void BassSynthView::copySequence (bool wholePattern)
{
    const int pattern = viewedPattern (state());
    Clipboard clip;
    clip.wholePattern = wholePattern || selected < 0;
    if (clip.wholePattern)
    {
        for (int r = 0; r < kBassRows; ++r)
            clip.rows[(size_t) r] = rowSequence (r, pattern);
        clip.source = "P" + juce::String (pattern + 1);
    }
    else
    {
        clip.rows[0] = rowSequence (selected, pattern);
        clip.source = look::pad (selected + 1, 2) + " " + bassSoundName (state().rows[(size_t) selected].sound) + " P" + juce::String (pattern + 1);
    }
    clipboard = clip;
    setStatus ("COPIED " + clip.source + (clip.wholePattern ? " (ALL ROWS)" : "") + " / PICK A "
               + (clip.wholePattern ? "PATTERN (P1-P4)" : "ROW OR PATTERN") + " AND PASTE (CTRL+V)");
}

void BassSynthView::pasteSequence()
{
    if (! clipboard.has_value())
    {
        setStatus ("NOTHING COPIED YET / SELECT A ROW AND PRESS COPY (CTRL+C)");
        return;
    }
    const int pattern = viewedPattern (state());
    const auto& clip = *clipboard;
    if (! clip.wholePattern && selected < 0)
    {
        setStatus ("SELECT A ROW TO PASTE ONE SEQUENCE INTO");
        return;
    }
    remember();
    undoText = "P" + juce::String (pattern + 1);

    // notes copied in another scale are snapped into this one
    const auto& b = state();
    auto snapped = [&b] (RowSequence seq) {
        for (auto& n : seq.notes)
            n = (std::int8_t) snapToScale (n, b.key, b.scale);
        return seq;
    };
    if (clip.wholePattern)
    {
        for (int r = 0; r < kBassRows; ++r)
            writeRowSequence (r, pattern, snapped (clip.rows[(size_t) r]));
        setStatus ("PASTED " + clip.source + " INTO P" + juce::String (pattern + 1) + " / CTRL+Z TO UNDO");
    }
    else
    {
        writeRowSequence (selected, pattern, snapped (clip.rows[0]));
        setStatus ("PASTED " + clip.source + " INTO " + look::pad (selected + 1, 2) + " " + bassSoundName (b.rows[(size_t) selected].sound)
                   + " P" + juce::String (pattern + 1) + " / CTRL+Z TO UNDO");
    }
}

void BassSynthView::randomizeSequence (int row)
{
    if (row < 0 || row >= kBassRows)
        return;
    const auto& b = state();
    const int pattern = viewedPattern (b);
    const auto sound = b.rows[(size_t) row].sound;
    remember();
    undoText = "P" + juce::String (pattern + 1);

    // How busy the line is and how far it wanders, by sound: subs and booms
    // hold the root on the beat, acid and 1-bit run in 16ths and jump octaves.
    float density = 0.35f, wander = 0.35f, octaveJump = 0.0f;
    int maxLength = 2;
    switch (sound)
    {
        case BassSound::Sub:    density = 0.28f; wander = 0.25f; maxLength = 4; break;
        case BassSound::Boom:   density = 0.22f; wander = 0.2f; maxLength = 6; break;
        case BassSound::Reese:  density = 0.25f; wander = 0.3f; maxLength = 6; break;
        case BassSound::Acid:   density = 0.6f; wander = 0.5f; octaveJump = 0.25f; maxLength = 1; break;
        case BassSound::OneBit: density = 0.55f; wander = 0.45f; octaveJump = 0.2f; maxLength = 1; break;
        case BassSound::Fm:     density = 0.4f; wander = 0.4f; octaveJump = 0.1f; maxLength = 2; break;
        case BassSound::Pulse:
        case BassSound::Saw:
        case BassSound::Count:  break;
    }

    auto& rng = juce::Random::getSystemRandom();
    const int root = snapToScale (36 + b.key, b.key, b.scale);
    RowSequence seq = rowSequence (row, pattern);
    int hits = 0;
    while (hits == 0)
    {
        int note = root, held = 0;
        for (int s = 0; s < kNumSteps; ++s)
        {
            seq.velocity[(size_t) s] = 0.0f;
            const float chance = s == 0 ? 0.9f : s % 4 == 0 ? density + 0.25f : s % 2 == 0 ? density : density * 0.7f;
            if (held > 0 || rng.nextFloat() >= chance)
            {
                held = juce::jmax (0, held - 1);
                continue;
            }
            // mostly the root, else a few scale steps away from the last note
            if (rng.nextFloat() < wander)
                note = stepInScale (note, b.key, b.scale, rng.nextInt ({ -3, 4 }));
            else
                note = root;
            int n = note;
            if (octaveJump > 0.0f && rng.nextFloat() < octaveJump)
                n = juce::jlimit (kBassLowNote, kBassHighNote, n + 12);
            const int length = 1 + (maxLength > 1 ? rng.nextInt (maxLength) : 0);
            seq.velocity[(size_t) s] = juce::jlimit (0.3f, 1.0f, (s % 4 == 0 ? 0.8f : 0.55f) + rng.nextFloat() * 0.3f);
            seq.notes[(size_t) s] = (std::int8_t) snapToScale (n, b.key, b.scale);
            seq.lengths[(size_t) s] = (std::uint8_t) length;
            held = length - 1;
            ++hits;
        }
    }
    writeRowSequence (row, pattern, seq);
    setStatus ("RANDOM " + look::pad (row + 1, 2) + " " + bassSoundName (sound) + " P" + juce::String (pattern + 1) + " IN "
               + keyName (b.key) + " " + scaleName (b.scale) + " / PRESS AGAIN FOR ANOTHER / CTRL+Z TO UNDO");
}

// ---- keys ---------------------------------------------------------------------

void BassSynthView::playKey (int semitone)
{
    const int row = selected < 0 ? 0 : selected;
    const int note = juce::jlimit (kBassLowNote, kBassHighNote, 12 * (octave + 1) + semitone);
    if (heldRow >= 0 && heldRow != row)
        releaseNote (heldRow);
    auditionNote (row, note, juce::ModifierKeys::getCurrentModifiers().isShiftDown() ? 0.5f : 0.9f);
    heldKey = semitone;
    heldRow = row;
}

bool BassSynthView::keyStateChanged (bool)
{
    if (heldKey < 0)
        return false;
    auto down = [] (int semitone) {
        const auto c = (int) kPianoKeys[semitone];
        return juce::KeyPress::isKeyCurrentlyDown (c) || juce::KeyPress::isKeyCurrentlyDown ((int) juce::CharacterFunctions::toUpperCase ((juce::juce_wchar) c));
    };
    if (down (heldKey))
        return false;
    // the sounding key let go: slide back to another still held, else stop
    for (int k = 0; kPianoKeys[k] != 0; ++k)
        if (down (k))
        {
            playKey (k);
            return false;
        }
    releaseNote (heldRow);
    heldKey = heldRow = -1;
    return false;
}

bool BassSynthView::keyPressed (const juce::KeyPress& key)
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
        if (key.getKeyCode() == juce::KeyPress::F1Key + p)
        {
            if (mods.isShiftDown())
                copyPatternTo (p);
            else
                selectPattern (p);
            return true;
        }

    if (key == juce::KeyPress::deleteKey || key == juce::KeyPress::backspaceKey) { clearPattern(); return true; }
    if (key == juce::KeyPress::upKey) { selectRow (selected < 0 ? kBassRows - 1 : juce::jmax (0, selected - 1)); return true; }
    if (key == juce::KeyPress::downKey) { selectRow (selected < 0 || selected >= kBassRows - 1 ? -1 : selected + 1); return true; }
    if (c >= '1' && c < '1' + kBassRows) { selectRow ((int) (c - '1')); return true; }
    if (c == '[' || c == ']')
    {
        octave = juce::jlimit (1, 4, octave + (c == ']' ? 1 : -1));
        setStatus ("KEYBOARD OCTAVE " + juce::String (octave) + " // Z PLAYS C" + juce::String (octave));
        return true;
    }

    for (int k = 0; kPianoKeys[k] != 0; ++k)
        if (c == (juce::juce_wchar) kPianoKeys[k])
        {
            // ignore auto-repeat: only the first press of a held key plays
            if (heldKey != k)
                playKey (k);
            return true;
        }
    return unhandled();
}

// ---- timer, drawing -------------------------------------------------------------

void BassSynthView::tick()
{
    const auto& b = state();
    for (int p = 0; p < kNumPatterns; ++p)
    {
        patternButtons[(size_t) p].setOn (b.pattern == p);
        patternButtons[(size_t) p].setMarked (b.queuedPattern == p);
    }
    keyButton.setText ("KEY " + juce::String (keyName (b.key)));
    scaleButton.setText (scaleName (b.scale));

    stepReadout.setText ("P" + juce::String (b.pattern + 1) + (b.queuedPattern >= 0 ? ">P" + juce::String (b.queuedPattern + 1) : juce::String())
                         + "  " + look::pad (proc.currentStep() + 1, 2) + "/" + juce::String (kNumSteps));
    octaveReadout.setText ("C" + juce::String (octave) + " - E" + juce::String (octave + 1));
    busReadout.setText (formatDb (levelToGain (b.masterLevel)));

    grid->tick();
    panel->refresh();
    repaint (0, getHeight() - kStatusHeight, getWidth(), kStatusHeight);
}

void BassSynthView::paint (juce::Graphics& g)
{
    g.fillAll (look::background);

    g.setColour (look::ink);
    g.setFont (look::mono (16.0f, true));
    g.drawText (look::pad (tool + 1, 2) + " " + toolTitle (tool), kMargin, 10, 300, 20, juce::Justification::centredLeft);
    g.setFont (look::mono (9.5f));
    g.setColour (look::dim);
    g.drawText ("MONO // 8 ROWS x 16 STEPS x 4 PATTERNS // SCALE LOCK // PATCH ROWS TOGETHER TO CUT", kMargin, 30, 560, 14,
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
    g.drawText ("CLICK STEP = NOTE  DRAG UP/DOWN = PITCH  DRAG SIDEWAYS = LENGTH  SHIFT+DRAG = VELOCITY  DRAG JACK TO JACK = CUT GROUP  RIGHT-CLICK JACK = UNPLUG  Z-M PLAY  [ ] OCTAVE",
                statusArea, juce::Justification::centredRight, true);
}

void BassSynthView::resized()
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
    buttons.removeFromLeft (18);
    keyButton.setBounds (buttons.removeFromLeft (72));
    buttons.removeFromLeft (6);
    scaleButton.setBounds (buttons.removeFromLeft (120));

    auto readouts = header.withTrimmedTop (8).withTrimmedLeft (juce::jmax (620, header.getWidth() - 390));
    stepReadout.setBounds (readouts.removeFromLeft (120).withHeight (36));
    readouts.removeFromLeft (8);
    octaveReadout.setBounds (readouts.removeFromLeft (130).withHeight (36));
    readouts.removeFromLeft (8);
    busReadout.setBounds (readouts.removeFromLeft (110).withHeight (36));

    area.removeFromBottom (kStatusHeight);
    panel->setBounds (area.removeFromBottom (kPanelHeight).reduced (kMargin, 0));
    grid->setBounds (area.reduced (kMargin, 6));
}
