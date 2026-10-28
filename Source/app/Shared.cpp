#include "Shared.h"

using namespace ms;

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

void paintPeakMeter (juce::Graphics& g, float x0, float mid, float meterW, float peak, float& hold)
{
    hold = juce::jmax (peak, hold * 0.97f);
    const float fill = juce::jlimit (0.0f, 1.0f, std::sqrt (peak) / std::sqrt (1.4f)) * meterW;
    const float holdX = juce::jlimit (0.0f, 1.0f, std::sqrt (hold) / std::sqrt (1.4f)) * meterW;
    const float zeroDb = std::sqrt (1.0f) / std::sqrt (1.4f) * meterW;
    g.setColour (look::faint);
    g.fillRect (x0, mid, meterW, 1.0f);
    g.setColour (peak > 1.0f ? look::red : look::ink);
    g.fillRect (x0, mid - 5.0f, fill, 10.0f);
    g.setColour (hold > 1.0f ? look::red : look::ink);
    g.fillRect (x0 + holdX, mid - 9.0f, 1.0f, 18.0f);
    g.setColour (look::red.withAlpha (0.8f));
    g.fillRect (x0 + zeroDb, mid - 12.0f, 1.0f, 24.0f);
    g.setFont (look::mono (9.0f));
    g.setColour (hold > 1.0f ? look::red : look::dim);
    g.drawText ("PEAK " + formatDb (hold), juce::Rectangle<float> (x0, mid - 26.0f, 160.0f, 12.0f), juce::Justification::topLeft);
    g.setColour (look::dim);
    g.drawText ("0 dB", juce::Rectangle<float> (x0 + zeroDb + 4.0f, mid + 10.0f, 50.0f, 12.0f), juce::Justification::topLeft);
}

void copyFxAutomation (const std::function<void (CommandType, int, int, float)>& send, int rack,
                       const FxAutomation& from, int fromPattern, int toPattern)
{
    send (CommandType::ClearFxAuto, rack, toPattern, -1.0f);
    for (int i = 0; i < kNumFxParams; ++i)
        for (int st = 0; st < kNumSteps; ++st)
            if (const float v = from.point (fromPattern, i, st); v >= 0.0f)
                send (CommandType::SetFxAuto, rack, fxAutoIndex (toPattern, i, st), v);
}

namespace
{
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
        setColour (juce::DocumentWindow::textColourId, look::ink);
        setColour (juce::TextButton::buttonColourId, look::background);
        setColour (juce::ComboBox::backgroundColourId, look::background);
        setColour (juce::ComboBox::outlineColourId, look::dim);
    }

    juce::Font getPopupMenuFont() override { return look::mono (13.0f); }

    // Window title bars: black, the title in the panel lettering on the left,
    // the window's size as a dim data readout, a rule underneath, and square
    // marks for minimise / close. The same on the studio and every tool.
    void drawDocumentWindowTitleBar (juce::DocumentWindow& window, juce::Graphics& g, int w, int h, int titleSpaceX,
                                     int titleSpaceW, const juce::Image*, bool) override
    {
        g.fillAll (look::background);
        // the studio window has its Options button on the left
        int x = juce::jmax (12, titleSpaceX);
        for (auto* child : window.getChildren())
            if (auto* b = dynamic_cast<juce::TextButton*> (child); b != nullptr && b->isVisible())
                x = juce::jmax (x, b->getRight() + 14);

        const bool active = window.isActiveWindow();
        g.setColour (active ? look::ink : look::ink.withAlpha (0.6f));
        g.fillRect ((float) x, (float) h * 0.5f - 4.0f, 8.0f, 8.0f);
        g.setFont (look::mono (11.0f, true));
        const auto title = window.getName().toUpperCase();
        const int titleW = juce::jmin (titleSpaceX + titleSpaceW - x - 16, juce::GlyphArrangement::getStringWidthInt (look::mono (11.0f, true), title) + 4);
        g.drawText (title, x + 16, 0, titleW, h, juce::Justification::centredLeft, true);

        g.setFont (look::mono (9.0f));
        g.setColour (look::dim);
        const auto size = juce::String (window.getWidth()) + " x " + juce::String (window.getHeight());
        g.drawText (size, x + 16 + titleW + 16, 0, juce::jmax (0, titleSpaceX + titleSpaceW - (x + 32 + titleW)), h,
                    juce::Justification::centredLeft, true);

        g.setColour (look::faint);
        g.fillRect (0, h - 1, w, 1);
    }

    juce::Button* createDocumentWindowButton (int buttonType) override { return new WindowButton (buttonType); }

    void drawResizableWindowBorder (juce::Graphics& g, int w, int h, const juce::BorderSize<int>&, juce::ResizableWindow&) override
    {
        g.setColour (look::faint);
        g.drawRect (0, 0, w, h, 1);
    }

    // Minimise / maximise / close as small outlined squares; inverted on hover.
    class WindowButton : public juce::Button
    {
    public:
        explicit WindowButton (int t) : Button (t == juce::DocumentWindow::closeButton ? "CLOSE" : "MINIMISE"), type (t) {}

        void paintButton (juce::Graphics& g, bool over, bool down) override
        {
            const auto box = getLocalBounds().toFloat().withSizeKeepingCentre (14.0f, 14.0f);
            const bool lit = over || down;
            if (lit)
            {
                g.setColour (look::ink);
                g.fillRect (box);
            }
            else
            {
                g.setColour (look::dim);
                g.drawRect (box, 1.0f);
            }
            g.setColour (lit ? look::background : look::ink);
            const auto c = box.getCentre();
            if (type == juce::DocumentWindow::closeButton)
            {
                g.drawLine (c.x - 3.5f, c.y - 3.5f, c.x + 3.5f, c.y + 3.5f, 1.2f);
                g.drawLine (c.x - 3.5f, c.y + 3.5f, c.x + 3.5f, c.y - 3.5f, 1.2f);
            }
            else if (type == juce::DocumentWindow::maximiseButton)
            {
                g.drawRect (box.reduced (3.5f), 1.0f);
            }
            else
            {
                g.fillRect (c.x - 4.0f, c.y + 2.0f, 8.0f, 1.2f);
            }
        }

    private:
        int type;
    };
    juce::Font getTextButtonFont (juce::TextButton&, int) override { return look::mono (12.0f, true); }
    juce::Font getLabelFont (juce::Label&) override { return look::mono (12.0f); }
    juce::Font getComboBoxFont (juce::ComboBox&) override { return look::mono (12.0f); }
};
} // namespace

juce::LookAndFeel& sharedLookAndFeel()
{
    static DatamaticsLookAndFeel laf;
    return laf;
}

// ---- FxRackControls -----------------------------------------------------------

const char* FxRackControls::groupName (int column)
{
    const char* groups[] = { "CRUSH > DISTORT > FILTER", "DELAY > REVERB", "GRAIN DELAY", "CHORUS // PSYCHOSIS OTT x 6" };
    return groups[juce::jlimit (0, kColumns - 1, column)];
}

void FxRackControls::attach (juce::Component& parent, std::function<FxParams()> shown,
                             std::function<void (int, float)> onChange)
{
    const auto defaults = defaultFxParams();
    for (int i = 0; i < kNumFxParams; ++i)
    {
        auto& s = sliders[(size_t) i];
        parent.addAndMakeVisible (s);
        s.setLabel (fxParamName (i));
        s.setDefault (defaults[(size_t) i]);
        s.setSteps (fxParamSteps (i));
        if (i == fxChorusMode)
            s.setToggle ("ENSEMBLE", "DOUBLER");
        else if (fxParamIsToggle (i))
            s.setToggle ("MS", "NOTE");
        s.format = [shown, i] (float v) {
            // time readouts depend on their sync switch, so format the whole set
            auto params = shown();
            params[(size_t) i] = v;
            char buf[32];
            formatFxParam (params, i, buf, (int) sizeof (buf));
            return juce::String (buf);
        };
        s.onChange = [this, onChange, i] (float v) {
            onChange (i, v);
            record (i, v);
        };
        s.onGesture = [this, i] (bool down) {
            held[(size_t) i] = down;
            lastStep[(size_t) i] = -1;
        };
        s.onRightClick = [this, i] {
            if (automation.moves == nullptr)
                return;
            const auto* moves = automation.moves();
            const int pattern = automation.pattern();
            if (moves == nullptr || ! moves->has (pattern, i))
                return;
            juce::PopupMenu m;
            m.addItem (1, "CLEAR " + juce::String (fxParamName (i)) + " AUTOMATION (THIS PATTERN)");
            m.showMenuAsync (juce::PopupMenu::Options().withMousePosition(), [this, i, pattern] (int r) {
                if (r == 1)
                    automation.send (CommandType::ClearFxAuto, pattern, (float) i);
            });
        };
    }
}

void FxRackControls::setAutomation (Automation a)
{
    automation = std::move (a);
}

void FxRackControls::record (int index, float value)
{
    if (automation.recording == nullptr || ! automation.recording() || fxParamIsToggle (index))
        return;
    const int step = automation.step();
    automation.send (CommandType::SetFxAuto, fxAutoIndex (automation.pattern(), index, step), value);
    lastStep[(size_t) index] = step;
}

void FxRackControls::tickAutomation (const FxParams& set)
{
    if (automation.moves == nullptr)
        return;
    // a control held still keeps writing its value into each new step
    if (automation.recording())
    {
        const int step = automation.step();
        for (int i = 0; i < kNumFxParams; ++i)
            if (held[(size_t) i] && lastStep[(size_t) i] != step)
                record (i, set[(size_t) i]);
    }

    const auto* moves = automation.moves();
    const int pattern = automation.pattern();
    const double position = automation.position();
    for (int i = 0; i < kNumFxParams; ++i)
    {
        auto& s = sliders[(size_t) i];
        if (moves == nullptr || ! moves->has (pattern, i))
        {
            s.clearAutomation();
            continue;
        }
        std::array<float, 16> points {};
        for (int st = 0; st < kNumSteps; ++st)
            points[(size_t) st] = moves->point (pattern, i, st);
        s.setAutomation (points, position >= 0.0 ? moves->valueAt (pattern, i, position) : -1.0f);
    }
}

void FxRackControls::setValues (const FxParams& fx)
{
    for (int i = 0; i < kNumFxParams; ++i)
        sliders[(size_t) i].setValue (fx[(size_t) i]);
}

void FxRackControls::layout (const std::function<int (int)>& columnX, int y0, int rowH, int colW)
{
    auto column = [&] (int c, std::initializer_list<int> params) {
        int y = y0;
        for (int p : params)
        {
            if (p < 0) // a small gap between effects
            {
                y += 6;
                continue;
            }
            sliders[(size_t) p].setBounds (columnX (c), y, colW, rowH);
            y += rowH;
        }
    };
    column (0, { fxCrushBits, fxCrushRate, -1, fxDistDrive, fxDistType, fxDistTone, fxDistMix, -1, fxCutoff, fxResonance });
    column (1, { fxDelayTime, fxDelaySync, fxDelayFeedback, fxDelayMix, -1, fxReverbTime, fxReverbSync, fxReverbDamp, fxReverbMix });
    column (2, { fxGrainTime, fxGrainSize, fxGrainSync, fxGrainPitch, fxGrainSpray, fxGrainFeedback, fxGrainMix });
    column (3, { fxChorusMode, fxChorusRate, fxChorusDepth, fxChorusMix, -1, fxPsyDepth, fxPsyStages, fxPsyTime, fxPsySync, fxPsyMix });
}
