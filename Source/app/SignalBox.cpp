#include "SignalBox.h"

using namespace ms;

namespace
{
// In-place radix-2 FFT; n must be a power of two.
void fft (float* re, float* im, int n)
{
    for (int i = 1, j = 0; i < n; ++i)
    {
        int bit = n >> 1;
        for (; (j & bit) != 0; bit >>= 1)
            j ^= bit;
        j ^= bit;
        if (i < j)
        {
            std::swap (re[i], re[j]);
            std::swap (im[i], im[j]);
        }
    }
    for (int len = 2; len <= n; len <<= 1)
    {
        const double angle = -2.0 * juce::MathConstants<double>::pi / len;
        const float wr = (float) std::cos (angle), wi = (float) std::sin (angle);
        for (int i = 0; i < n; i += len)
        {
            float cr = 1.0f, ci = 0.0f;
            for (int k = 0; k < len / 2; ++k)
            {
                const int a = i + k, b = i + k + len / 2;
                const float vr = re[b] * cr - im[b] * ci, vi = re[b] * ci + im[b] * cr;
                re[b] = re[a] - vr;
                im[b] = im[a] - vi;
                re[a] += vr;
                im[a] += vi;
                const float next = cr * wr - ci * wi;
                ci = cr * wi + ci * wr;
                cr = next;
            }
        }
    }
}

template <size_t N>
std::array<float, N> makeHannWindow()
{
    std::array<float, N> w {};
    for (size_t i = 0; i < N; ++i)
        w[i] = 0.5f - 0.5f * std::cos (2.0f * juce::MathConstants<float>::pi * (float) i / (float) N);
    return w;
}
} // namespace

// A white (or coloured) label with black text, like the word boxes in the
// Datamatics installation. Returns its width.
float paintTag (juce::Graphics& g, const juce::String& text, float x, float y, juce::Colour fill)
{
    const auto font = look::mono (8.0f, true);
    const float w = juce::GlyphArrangement::getStringWidth (font, text) + 5.0f;
    g.setColour (fill);
    g.fillRect (x, y, w, 9.0f);
    g.setColour (look::background);
    g.setFont (font);
    g.drawText (text, juce::Rectangle<float> (x, y, w, 9.0f), juce::Justification::centred, false);
    return w;
}

SignalBox::SignalBox() : fftWindow (makeHannWindow<(size_t) kFftSize>()) {}

// Signal panel: an annotated harmonic lattice. A log frequency ruler
// (20 Hz - 20 kHz, a tick at every C) with the live spectrum, the
// harmonic series of the sound's pitch as dotted lines that light up when
// the sound has energy there, and the numbers as tags on top: note, pitch
// and measured phase on the fundamental (with a phase portrait under it),
// the spectral centroid, and peak / RMS / crest factor. The current
// sample's 16 bits run along the top.
void SignalBox::paint (juce::Graphics& g, juce::Rectangle<float> box, double sr, float f0, const juce::String& pitchText, float alpha)
{
    // levels over the newest 256 samples
    float peak = 0.0f, sumSq = 0.0f;
    for (int i = kFftSize - 256; i < kFftSize; ++i)
    {
        peak = juce::jmax (peak, std::abs (scopeBuf[(size_t) i]));
        sumSq += scopeBuf[(size_t) i] * scopeBuf[(size_t) i];
    }
    const float rms = std::sqrt (sumSq / 256.0f);
    const bool live = rms > 0.002f;

    // spectrum
    for (int i = 0; i < kFftSize; ++i)
    {
        fftRe[(size_t) i] = scopeBuf[(size_t) i] * fftWindow[(size_t) i];
        fftIm[(size_t) i] = 0.0f;
    }
    fft (fftRe.data(), fftIm.data(), kFftSize);
    float magSum = 0.0f, freqSum = 0.0f;
    for (int b = 1; b < kFftSize / 2; ++b)
    {
        const float m = std::hypot (fftRe[(size_t) b], fftIm[(size_t) b]) / (float) (kFftSize / 4);
        mags[(size_t) b] = m;
        magSum += m;
        freqSum += m * (float) b * (float) sr / (float) kFftSize;
    }
    const float centroid = magSum > 1.0e-4f ? freqSum / magSum : 0.0f;

    const auto area = box.reduced (1.0f).withTrimmedTop (5.0f);
    const float x0 = area.getX(), w = area.getWidth(), top = area.getY() + 1.0f, base = area.getBottom() - 8.0f;
    const auto xOf = [&] (double hz) { return x0 + (float) (std::log (hz / 20.0) / std::log (1000.0)) * w; };
    // spectrum level 0..1 (-72 dB .. 0 dB) for the pixels around a frequency
    const auto levelAt = [&] (double hzLo, double hzHi) {
        const int lo = juce::jlimit (1, kFftSize / 2 - 1, (int) std::floor (hzLo * kFftSize / sr));
        const int hi = juce::jlimit (lo, kFftSize / 2 - 1, (int) std::ceil (hzHi * kFftSize / sr));
        float m = 0.0f;
        for (int b = lo; b <= hi; ++b)
            m = juce::jmax (m, mags[(size_t) b]);
        return juce::jlimit (0.0f, 1.0f, (20.0f * std::log10 (m + 1.0e-9f) + 72.0f) / 72.0f);
    };
    const auto hzAt = [&] (float x) { return 20.0 * std::pow (1000.0, (double) ((x - x0) / w)); };

    // octave ruler
    g.setFont (look::mono (7.0f));
    for (int octave = 1; octave <= 9; ++octave)
    {
        const float x = xOf (16.352 * std::pow (2.0, octave));
        g.setColour (look::faint);
        g.fillRect (x, top, 1.0f, area.getBottom() - top);
        g.setColour (look::dim);
        g.drawText ("C" + juce::String (octave), juce::Rectangle<float> (x + 2.0f, base, 20.0f, 8.0f), juce::Justification::centredLeft, false);
    }

    // live spectrum
    if (live)
    {
        for (float x = x0; x < x0 + w; x += 1.0f)
        {
            const float v = levelAt (hzAt (x), hzAt (x + 1.0f));
            if (v < 0.15f)
                continue;
            g.setColour (look::ink.withAlpha (alpha * (0.2f + v * 0.55f)));
            const float barH = v * (base - top - 6.0f);
            g.fillRect (x, base - barH, 1.0f, barH);
        }
    }

    // harmonic series of the sound's pitch
    if (f0 > 0.0f)
    {
        for (int n = 1; n <= 32 && f0 * (float) n < 20000.0f; ++n)
        {
            const double f = f0 * (double) n;
            const float x = xOf (f);
            const bool lit = live && levelAt (f * 0.985, f * 1.015) > 0.55f;
            g.setColour (n == 1 ? look::red.withAlpha (alpha) : lit ? look::ink.withAlpha (alpha) : look::faint.brighter (0.25f));
            for (float y = top; y < base; y += 2.0f)
                g.fillRect (x, y, 1.0f, 1.0f);
        }
    }

    if (live && centroid > 20.0f)
    {
        g.setColour (look::blue);
        g.fillRect (xOf (centroid), top, 1.0f, base - top);
    }

    // the current sample's 16 bits along the top
    {
        const int bits = juce::roundToInt (juce::jlimit (-1.0f, 1.0f, scopeBuf[(size_t) kFftSize - 1]) * 32767.0f) & 0xffff;
        const float cellW = box.getWidth() / 16.0f;
        for (int b = 0; b < 16; ++b)
        {
            const bool on = ((bits >> (15 - b)) & 1) != 0;
            g.setColour (on ? (b == 0 ? look::red : look::ink.withAlpha (alpha)) : look::faint);
            g.fillRect (box.getX() + (float) b * cellW, box.getY() + 3.0f, cellW - 1.0f, 2.0f);
        }
    }

    // tags
    if (f0 > 0.0f)
    {
        // phase of the fundamental as measured in the spectrum
        const int bin = juce::jlimit (1, kFftSize / 2 - 1, juce::roundToInt (f0 * kFftSize / sr));
        const auto phase = juce::String (live ? juce::roundToInt ((std::atan2 (fftIm[(size_t) bin], fftRe[(size_t) bin]) + juce::MathConstants<float>::pi) * 180.0f / juce::MathConstants<float>::pi) : 0)
                           + juce::String (juce::CharPointer_UTF8 ("\xc2\xb0"));
        const auto text = pitchText + "  " + (live ? phase : juce::String ("--"));
        const float fx = xOf (f0);
        const float tagW = juce::GlyphArrangement::getStringWidth (look::mono (8.0f, true), text) + 5.0f;
        const float tx = fx + 2.0f + tagW < area.getRight() - 50.0f ? fx + 2.0f : fx - 2.0f - tagW;
        paintTag (g, text, tx, top + 1.0f, look::red.withAlpha (alpha));
        if (area.getBottom() - top >= 34.0f)
            paintPhasePortrait (g, { tx, top + 12.0f, 22.0f, 22.0f }, f0, sr);
    }
    // small boxes (folded-down windows) keep only what fits
    const bool roomy = area.getBottom() - top >= 34.0f;
    if (live && centroid > 20.0f && roomy)
        paintTag (g, "CENT " + juce::String (juce::roundToInt (centroid)) + " Hz", juce::jmin (area.getRight() - 112.0f, xOf (centroid) + 2.0f), top + 23.0f, look::blue);

    const auto dbText = [] (float v) { return v < 1.0e-5f ? juce::String ("-INF") : juce::String (gainToDb (v), 1); };
    const juce::String readouts[] = { "PK " + dbText (peak), "RMS " + dbText (rms), "CRST " + (rms > 1.0e-4f ? juce::String (peak / rms, 1) : juce::String ("--")) };
    float ry = top + 1.0f;
    const int readoutsShown = juce::jlimit (1, 3, (int) ((area.getBottom() - top) / 11.0f));
    for (int i = 0; i < readoutsShown; ++i)
    {
        const float tw = juce::GlyphArrangement::getStringWidth (look::mono (8.0f, true), readouts[i]) + 5.0f;
        paintTag (g, readouts[i], area.getRight() - tw - 1.0f, ry, i == 0 && peak > 1.0f ? look::red : look::ink.withAlpha (alpha));
        ry += 11.0f;
    }

    g.setColour (look::dim);
    g.drawRect (box, 1.0f);
    g.setColour (look::ink.withAlpha (alpha));
    g.fillRect (box.getX(), box.getY(), box.getWidth(), 3.0f);
}

// Each sample plotted against the sample a quarter cycle earlier: a pure
// tone draws a ring, distortion squares it off, noise is a cloud.
void SignalBox::paintPhasePortrait (juce::Graphics& g, juce::Rectangle<float> r, float f0, double sr)
{
    g.setColour (look::background);
    g.fillRect (r);
    g.setColour (look::faint.brighter (0.3f));
    g.drawRect (r, 1.0f);
    g.fillRect (r.getX(), r.getCentreY(), r.getWidth(), 1.0f);
    g.fillRect (r.getCentreX(), r.getY(), 1.0f, r.getHeight());

    const int tau = juce::jlimit (1, 1000, juce::roundToInt (sr / f0 / 4.0));
    float maxAbs = 1.0e-4f;
    for (int i = kFftSize - 1200; i < kFftSize; ++i)
        maxAbs = juce::jmax (maxAbs, std::abs (scopeBuf[(size_t) i]));
    const float scale = r.getWidth() * 0.45f / maxAbs;
    g.setColour (look::ink);
    for (int i = kFftSize - 400; i < kFftSize; ++i)
    {
        const float a = scopeBuf[(size_t) i], b = scopeBuf[(size_t) (i - tau)];
        if (std::abs (a) + std::abs (b) < 0.003f)
            continue;
        g.fillRect (r.getCentreX() + a * scale, r.getCentreY() - b * scale, 1.0f, 1.0f);
    }
}

