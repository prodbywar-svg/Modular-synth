#include "Widgets.h"

#include <array>

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
    if (steps > 1)
        v = std::round (v * (float) (steps - 1)) / (float) (steps - 1);
    if (juce::exactlyEqual (v, value))
        return;
    value = v;
    repaint();
    if (onChange)
        onChange (value);
}

void DataSlider::changeByUser (float v, const juce::ModifierKeys& mods)
{
    v = juce::jlimit (0.0f, 1.0f, v);
    if (snap && ! mods.isAltDown())
        v = snap (v);
    change (v);
}

void DataSlider::setAutomation (const std::array<float, 16>& points, float live)
{
    bool any = false;
    for (float p : points)
        any = any || p >= 0.0f;
    if (any == hasLane && points == lane && juce::exactlyEqual (live, liveValue))
        return;
    lane = points;
    hasLane = any;
    liveValue = live;
    repaint();
}

void DataSlider::paint (juce::Graphics& g)
{
    const auto b = getLocalBounds().toFloat();
    const float labelW = 72.0f, valueW = 76.0f;

    if (isToggle())
    {
        g.setFont (look::mono (11.0f));
        g.setColour (isMouseOverOrDragging() ? look::ink : look::dim);
        g.drawText (label, b.withWidth (labelW).toNearestInt(), juce::Justification::centredLeft);

        auto area = b.withTrimmedLeft (labelW).withSizeKeepingCentre (b.getWidth() - labelW, juce::jmin (15.0f, b.getHeight() - 2.0f));
        area.setWidth (juce::jmin (area.getWidth(), 120.0f));
        const bool onSide = value > 0.5f;
        auto halves = std::array<juce::Rectangle<float>, 2> { area.withWidth (area.getWidth() * 0.5f),
                                                               area.withTrimmedLeft (area.getWidth() * 0.5f) };
        const juce::String texts[2] = { toggleOff, toggleOn };
        g.setFont (look::mono (10.0f, true));
        for (int i = 0; i < 2; ++i)
        {
            const bool active = (i == 1) == onSide;
            if (active)
            {
                g.setColour (look::ink);
                g.fillRect (halves[(size_t) i]);
                g.setColour (look::background);
            }
            else
            {
                g.setColour (look::faint);
                g.drawRect (halves[(size_t) i], 1.0f);
                g.setColour (look::dim);
            }
            g.drawText (texts[i], halves[(size_t) i], juce::Justification::centred);
        }
        return;
    }
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

    // automation: the recorded steps as blue dots under the line, and the
    // value playing now, which the line and readout show instead
    const bool live = hasLane && liveValue >= 0.0f && ! dragging;
    if (hasLane)
    {
        g.setColour (look::blue);
        const float cell = line.getWidth() / 16.0f;
        for (int s = 0; s < 16; ++s)
            if (lane[(size_t) s] >= 0.0f)
                g.fillRect (line.getX() + cell * (float) s, y + 5.0f, juce::jmax (1.5f, cell - 1.0f), 2.0f);
    }
    const float shown = live ? liveValue : value;

    g.setColour (live ? look::blue : look::ink);
    const float x = line.getX() + line.getWidth() * shown;
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

    g.setColour (live ? look::blue.brighter (0.4f) : look::ink);
    g.drawText (format ? format (shown) : juce::String (shown, 3),
                b.withTrimmedLeft (b.getWidth() - valueW).toNearestInt(), juce::Justification::centredRight);
}

void DataSlider::mouseDown (const juce::MouseEvent& e)
{
    if (e.mods.isPopupMenu())
    {
        if (onRightClick)
            onRightClick();
        return;
    }
    dragStartValue = value;
    dragging = true;
    if (onGesture)
        onGesture (true);
    if (isToggle())
        change (value > 0.5f ? 0.0f : 1.0f);
}

void DataSlider::mouseDrag (const juce::MouseEvent& e)
{
    if (isToggle() || ! dragging)
        return;
    const float width = juce::jmax (40.0f, (float) getWidth() - 156.0f);
    const float scale = e.mods.isShiftDown() ? 0.1f : 1.0f;
    // stepped controls need a longer throw per position to stay usable
    const float throwScale = steps > 1 ? juce::jmin (1.0f, (float) steps / 24.0f) : 1.0f;
    changeByUser (dragStartValue + (float) e.getDistanceFromDragStartX() / width * scale * throwScale, e.mods);
}

void DataSlider::mouseUp (const juce::MouseEvent& e)
{
    if (e.mods.isPopupMenu() || ! dragging)
        return;
    dragging = false;
    repaint();
    if (onGesture)
        onGesture (false);
}

void DataSlider::mouseDoubleClick (const juce::MouseEvent&)
{
    if (! isToggle())
        change (defaultValue);
}

void DataSlider::mouseWheelMove (const juce::MouseEvent& e, const juce::MouseWheelDetails& w)
{
    if (isToggle() || juce::exactlyEqual (w.deltaY, 0.0f))
        return;
    if (steps > 1)
    {
        change (value + (w.deltaY > 0.0f ? 1.0f : -1.0f) / (float) (steps - 1));
        return;
    }
    if (snap && ! e.mods.isAltDown())
    {
        // step to the next note in the scroll direction
        const float dir = w.deltaY > 0.0f ? 1.0f : -1.0f;
        for (float probe = value + dir * 0.002f; probe >= 0.0f && probe <= 1.0f; probe += dir * 0.002f)
        {
            const float snapped = snap (probe);
            if (! juce::exactlyEqual (snapped, value) && (snapped - value) * dir > 0.0f)
            {
                change (snapped);
                return;
            }
        }
        return;
    }
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
    else if (marked)
    {
        g.setColour (look::blue);
        g.drawRect (b, 1.5f);
        g.setColour (look::ink);
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
