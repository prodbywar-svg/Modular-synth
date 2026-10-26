#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

#include <functional>

// The datamatics look: black field, white marks, tiny monospace numbers,
// red only for the playhead / record / clipping, blue only for boundaries.
namespace look
{
const juce::Colour background { 0xff000000 };
const juce::Colour ink { 0xffffffff };
const juce::Colour dim { 0xff5a5a5a };
const juce::Colour faint { 0xff262626 };
const juce::Colour red { 0xffff2b2b };
const juce::Colour blue { 0xff2f6bff };

juce::Font mono (float height, bool bold = false);
juce::String pad (int value, int digits);
} // namespace look

// A parameter drawn as a thin line with a white filled portion and a numeric
// readout. Drag left/right (shift = fine), scroll, or double-click to reset.
class DataSlider : public juce::Component
{
public:
    DataSlider() = default;

    void setLabel (const juce::String& l) { label = l; repaint(); }
    void setValue (float v);
    float getValue() const { return value; }
    void setDefault (float v) { defaultValue = v; }
    void setBipolar (bool b) { bipolar = b; }

    std::function<void (float)> onChange;
    std::function<juce::String (float)> format;

    void paint (juce::Graphics&) override;
    void mouseDown (const juce::MouseEvent&) override;
    void mouseDrag (const juce::MouseEvent&) override;
    void mouseDoubleClick (const juce::MouseEvent&) override;
    void mouseWheelMove (const juce::MouseEvent&, const juce::MouseWheelDetails&) override;

private:
    void change (float v);

    juce::String label;
    float value = 0.0f, defaultValue = 0.0f, dragStartValue = 0.0f;
    bool bipolar = false;
};

// Text button in brackets: "[ PLAY ]". Inverts when on; red when on if accent.
class DataButton : public juce::Component
{
public:
    explicit DataButton (const juce::String& t) : text (t) {}

    void setOn (bool b)
    {
        if (b != on)
        {
            on = b;
            repaint();
        }
    }
    bool isOn() const { return on; }
    void setAccent (juce::Colour c) { accent = c; hasAccent = true; }
    void setText (const juce::String& t) { text = t; repaint(); }

    std::function<void()> onClick;

    void paint (juce::Graphics&) override;
    void mouseDown (const juce::MouseEvent&) override;
    void mouseEnter (const juce::MouseEvent&) override { repaint(); }
    void mouseExit (const juce::MouseEvent&) override { repaint(); }

private:
    juce::String text;
    bool on = false, hasAccent = false;
    juce::Colour accent;
};

// A number you drag up/down, e.g. "BPM 132.0".
class DataReadout : public juce::Component
{
public:
    void setLabel (const juce::String& l) { label = l; }
    void setText (const juce::String& t)
    {
        if (t != text)
        {
            text = t;
            repaint();
        }
    }
    void setColour (juce::Colour c) { colour = c; repaint(); }

    // Called with the drag distance in pixels since the last call (up = +).
    std::function<void (float pixels, bool fine)> onDrag;
    std::function<void()> onDoubleClick;

    void paint (juce::Graphics&) override;
    void mouseDown (const juce::MouseEvent&) override;
    void mouseDrag (const juce::MouseEvent&) override;
    void mouseDoubleClick (const juce::MouseEvent&) override;

private:
    juce::String label, text;
    juce::Colour colour = look::ink;
    int lastY = 0;
};
