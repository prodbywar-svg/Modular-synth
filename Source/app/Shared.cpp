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
        s.onChange = [onChange, i] (float v) { onChange (i, v); };
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
