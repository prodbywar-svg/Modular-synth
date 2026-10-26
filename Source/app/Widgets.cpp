#include "Widgets.h"

namespace look
{
juce::Font mono (float height, bool bold)
{
    return juce::Font (juce::FontOptions (juce::Font::getDefaultMonospacedFontName(), height,
                                          bold ? juce::Font::bold : juce::Font::plain));
}

juce::String pad (int value, int digits)
{
    return juce::String (value).paddedLeft ('0', digits);
}
} // namespace look

// ---- DataSlider ---------------------------------------------------------------

void DataSlider::setValue (float v)
{
    v = juce::jlimit (0.0f, 1.0f, v);
    if (! juce::exactlyEqual (v, value))
    {
        value = v;
        repaint();
    }
}

void DataSlider::change (float v)
{
    v = juce::jlimit (0.0f, 1.0f, v);
    if (juce::exactlyEqual (v, value))
        return;
    value = v;
    repaint();
    if (onChange)
        onChange (value);
}

void DataSlider::paint (juce::Graphics& g)
{
    const auto b = getLocalBounds().toFloat();
    const float labelW = 72.0f, valueW = 76.0f;
    const auto line = b.withTrimmedLeft (labelW).withTrimmedRight (valueW + 8.0f);
    const float y = b.getCentreY();

    g.setFont (look::mono (11.0f));
    g.setColour (isMouseOverOrDragging() ? look::ink : look::dim);
    g.drawText (label, b.withWidth (labelW).toNearestInt(), juce::Justification::centredLeft);

    g.setColour (look::faint);
    g.fillRect (line.getX(), y, line.getWidth(), 1.0f);

    // tick marks at quarters, like a data ruler
    g.setColour (look::dim);
    for (int i = 0; i <= 4; ++i)
        g.fillRect (line.getX() + line.getWidth() * (float) i / 4.0f, y - 3.0f, 1.0f, 3.0f);

    g.setColour (look::ink);
    const float x = line.getX() + line.getWidth() * value;
    if (bipolar)
    {
        const float mid = line.getCentreX();
        g.fillRect (juce::jmin (mid, x), y - 1.0f, std::abs (x - mid), 3.0f);
    }
    else
    {
        g.fillRect (line.getX(), y - 1.0f, x - line.getX(), 3.0f);
    }
    g.fillRect (x - 0.5f, y - 6.0f, 1.0f, 12.0f);

    g.drawText (format ? format (value) : juce::String (value, 3),
                b.withTrimmedLeft (b.getWidth() - valueW).toNearestInt(), juce::Justification::centredRight);
}

void DataSlider::mouseDown (const juce::MouseEvent&)
{
    dragStartValue = value;
}

void DataSlider::mouseDrag (const juce::MouseEvent& e)
{
    const float width = juce::jmax (40.0f, (float) getWidth() - 156.0f);
    const float scale = e.mods.isShiftDown() ? 0.1f : 1.0f;
    change (dragStartValue + (float) e.getDistanceFromDragStartX() / width * scale);
}

void DataSlider::mouseDoubleClick (const juce::MouseEvent&)
{
    change (defaultValue);
}

void DataSlider::mouseWheelMove (const juce::MouseEvent& e, const juce::MouseWheelDetails& w)
{
    change (value + w.deltaY * (e.mods.isShiftDown() ? 0.01f : 0.05f));
}

// ---- DataButton ----------------------------------------------------------------

void DataButton::paint (juce::Graphics& g)
{
    const auto b = getLocalBounds().toFloat().reduced (0.5f);
    const auto colour = hasAccent ? accent : look::ink;

    if (on)
    {
        g.setColour (colour);
        g.fillRect (b);
        g.setColour (look::background);
    }
    else
    {
        g.setColour (isMouseOver() ? look::ink : look::dim);
        g.drawRect (b, 1.0f);
        g.setColour (isMouseOver() ? look::ink : look::ink.withAlpha (0.85f));
    }

    g.setFont (look::mono (11.0f, true));
    g.drawText (text, getLocalBounds(), juce::Justification::centred);
}

void DataButton::mouseDown (const juce::MouseEvent&)
{
    if (onClick)
        onClick();
}

// ---- DataReadout ---------------------------------------------------------------

void DataReadout::paint (juce::Graphics& g)
{
    auto b = getLocalBounds();
    g.setFont (look::mono (9.5f));
    g.setColour (look::dim);
    g.drawText (label, b.removeFromTop (b.getHeight() / 2), juce::Justification::bottomLeft);
    g.setFont (look::mono (13.0f, true));
    g.setColour (colour);
    g.drawText (text, b, juce::Justification::topLeft);

    if (onDrag && isMouseOverOrDragging())
    {
        g.setColour (look::dim);
        g.fillRect (0, getHeight() - 1, getWidth(), 1);
    }
}

void DataReadout::mouseDown (const juce::MouseEvent& e)
{
    lastY = e.getPosition().y;
}

void DataReadout::mouseDrag (const juce::MouseEvent& e)
{
    const int y = e.getPosition().y;
    if (onDrag && y != lastY)
        onDrag ((float) (lastY - y), e.mods.isShiftDown());
    lastY = y;
}

void DataReadout::mouseDoubleClick (const juce::MouseEvent&)
{
    if (onDoubleClick)
        onDoubleClick();
}
