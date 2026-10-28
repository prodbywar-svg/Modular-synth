#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

#include <array>
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
    // Discrete positions (0 = continuous), e.g. the HR-16 sound number.
    void setSteps (int n) { steps = n; }
    // A two-way switch: click flips it, the labels show which side is on.
    void setToggle (const juce::String& offText, const juce::String& onText)
    {
        toggleOff = offText;
        toggleOn = onText;
        repaint();
    }
    void clearToggle() { toggleOff = toggleOn = {}; }
    bool isToggle() const { return toggleOn.isNotEmpty(); }

    std::function<void (float)> onChange;
    // true when a drag starts, false when it ends (automation records while held)
    std::function<void (bool)> onGesture;
    std::function<void()> onRightClick;
    std::function<juce::String (float)> format;

    // Effect automation, drawn in blue: the recorded steps under the line,
    // and the value it is playing now (-1 = none) in place of the set one.
    void setAutomation (const std::array<float, 16>& points, float live);
    void clearAutomation()
    {
        std::array<float, 16> none;
        none.fill (-1.0f);
        setAutomation (none, -1.0f);
    }
    // Applied to dragged / scrolled values unless Alt is held (snap to notes).
    std::function<float (float)> snap;

    void paint (juce::Graphics&) override;
    void mouseDown (const juce::MouseEvent&) override;
    void mouseDrag (const juce::MouseEvent&) override;
    void mouseUp (const juce::MouseEvent&) override;
    void mouseDoubleClick (const juce::MouseEvent&) override;
    void mouseWheelMove (const juce::MouseEvent&, const juce::MouseWheelDetails&) override;

private:
    std::array<float, 16> lane {};
    bool hasLane = false, dragging = false;
    float liveValue = -1.0f;
    void change (float v);
    void changeByUser (float v, const juce::ModifierKeys&);

    juce::String label, toggleOff, toggleOn;
    float value = 0.0f, defaultValue = 0.0f, dragStartValue = 0.0f;
    bool bipolar = false;
    int steps = 0;
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
    // A blue outline: "waiting to happen", e.g. a queued pattern.
    void setMarked (bool b)
    {
        if (b != marked)
        {
            marked = b;
            repaint();
        }
    }

    std::function<void()> onClick;

    void paint (juce::Graphics&) override;
    void mouseDown (const juce::MouseEvent&) override;
    void mouseEnter (const juce::MouseEvent&) override { repaint(); }
    void mouseExit (const juce::MouseEvent&) override { repaint(); }

private:
    juce::String text;
    bool on = false, hasAccent = false, marked = false;
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
