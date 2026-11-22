// FreqFreaks — frequency-unmasking plugin suite
// Copyright (C) 2026 FreqFreaks contributors
// SPDX-License-Identifier: GPL-3.0-or-later
#include "BandViews.h"
#include "ff/Biquad.h"

namespace ff
{

juce::String shortHz (float hz)
{
    if (hz >= 10000.0f)
        return juce::String (juce::roundToInt (hz / 1000.0f)) + "k";
    if (hz >= 1000.0f)
        return juce::String (std::round (hz / 100.0f) / 10.0f) + "k";
    if (hz >= 100.0f)
        return juce::String (juce::roundToInt (hz / 10.0f) * 10);
    return juce::String (juce::roundToInt (hz));
}

float frequencyToX (float hz, juce::Rectangle<float> area) noexcept
{
    const float t = std::log10 (juce::jlimit (20.0f, 20000.0f, hz) / 20.0f) / 3.0f;
    return area.getX() + t * area.getWidth();
}

void drawFrequencyGrid (juce::Graphics& g, juce::Rectangle<float> area, bool labels)
{
    static const std::pair<float, const char*> ticks[] = {
        { 50.0f, "50" }, { 100.0f, "100" }, { 200.0f, "200" }, { 500.0f, "500" },
        { 1000.0f, "1k" }, { 2000.0f, "2k" }, { 5000.0f, "5k" }, { 10000.0f, "10k" } };

    g.setFont (theme::font (11.0f));
    for (const auto& [hz, label] : ticks)
    {
        const float x = frequencyToX (hz, area);
        g.setColour (theme::gridLine);
        g.drawVerticalLine ((int) x, area.getY(), area.getBottom());
        if (labels)
        {
            g.setColour (theme::textDim);
            g.drawText (label, juce::Rectangle<float> (x - 20.0f, area.getBottom() + 2.0f, 40.0f, 14.0f),
                        juce::Justification::centred);
        }
    }
}

void drawCard (juce::Graphics& g, juce::Rectangle<float> area, juce::Colour topAccent)
{
    g.setColour (theme::panel);
    g.fillRoundedRectangle (area, 12.0f);
    g.setColour (theme::border);
    g.drawRoundedRectangle (area.reduced (0.5f), 12.0f, 1.0f);
    if (! topAccent.isTransparent())
    {
        juce::Path top;
        top.addRoundedRectangle (area.getX(), area.getY(), area.getWidth(), 3.0f, 2.0f);
        g.setColour (topAccent);
        g.fillPath (top);
    }
}

//==============================================================================
SpectrumView::SpectrumView()
{
    for (int i = 0; i < kWavePoints; ++i)
        waveHz[(size_t) i] = 20.0f * std::pow (1000.0f, (float) i / (float) (kWavePoints - 1));
    peakCut.fill (0.0f);
}

juce::Rectangle<float> SpectrumView::plotArea() const
{
    return getLocalBounds().toFloat().withTrimmedBottom (18.0f).withTrimmedRight (34.0f);
}

void SpectrumView::setData (const bus::TrackData& ownData, juce::Colour ownCol,
                            const bus::TrackData* reference, juce::Colour referenceColour,
                            juce::String referenceName, float bassMonoHz, const BellBank* bank)
{
    hasWave = bank != nullptr;
    if (hasWave)
    {
        BellArray eqGains {}, spaceGains {};
        std::copy (std::begin (ownData.eqBells), std::end (ownData.eqBells), eqGains.begin());
        std::copy (std::begin (ownData.spaceBells), std::end (ownData.spaceBells), spaceGains.begin());
        bank->cutCurve (spaceGains, waveHz.data(), spaceCut.data(), kWavePoints);
        bank->cutCurve (eqGains, waveHz.data(), eqCut.data(), kWavePoints);

        // Narrow Sharp mode: the moving bells belong to the EQ layer.
        for (int k = 0; k < kNumPeaks; ++k)
        {
            if (ownData.peakDb[k] <= 1.0e-3f)
                continue;
            const auto c = BiquadCoeffs::peaking (bank->sampleRate(), juce::jmin ((double) ownData.peakHz[k], bank->sampleRate() * 0.45),
                                                  juce::jmax (0.3f, ownData.peakQ[k]), -ownData.peakDb[k]);
            for (int i = 0; i < kWavePoints; ++i)
                eqCut[(size_t) i] -= (float) c.magnitudeDb (juce::jmin ((double) waveHz[(size_t) i], bank->sampleRate() * 0.49), bank->sampleRate());
        }

        // Peak line: follows rises at once, falls about 4 dB per second (called ~30 times a second).
        for (int i = 0; i < kWavePoints; ++i)
        {
            const float total = std::max (0.0f, spaceCut[(size_t) i]) + std::max (0.0f, eqCut[(size_t) i]);
            peakCut[(size_t) i] = std::max (total, peakCut[(size_t) i] - 0.13f);
        }
    }

    own = ownData;
    ownColour = ownCol;
    hasReference = reference != nullptr;
    if (hasReference)
        ref = *reference;
    refColour = referenceColour;
    refName = std::move (referenceName);
    bassMono = bassMonoHz;
    repaint();
}

juce::Path SpectrumView::curve (const float* levelsDb, juce::Rectangle<float> area, bool closed) const
{
    const auto& layout = BandLayout::get();
    constexpr float topDb = 0.0f, bottomDb = -84.0f;
    auto y = [&] (float db) {
        return area.getY() + (topDb - juce::jlimit (bottomDb, topDb, db)) / (topDb - bottomDb) * area.getHeight();
    };

    juce::Path p;
    juce::Point<float> prev;
    for (int b = 0; b < kNumBands; ++b)
    {
        const juce::Point<float> pt (frequencyToX (layout.centreHz[(size_t) b], area), y (levelsDb[b]));
        if (b == 0)
        {
            if (closed)
            {
                p.startNewSubPath (pt.x, area.getBottom());
                p.lineTo (pt);
            }
            else
            {
                p.startNewSubPath (pt);
            }
        }
        else
        {
            const float midX = 0.5f * (prev.x + pt.x);
            p.cubicTo (midX, prev.y, midX, pt.y, pt.x, pt.y);
        }
        prev = pt;
    }
    if (closed)
    {
        p.lineTo (prev.x, area.getBottom());
        p.closeSubPath();
    }
    return p;
}

void SpectrumView::paint (juce::Graphics& g)
{
    auto plot = plotArea();

    // Bass Mono zone: only EQ works there.
    const float bx = frequencyToX (bassMono, plot);
    g.setColour (juce::Colours::white.withAlpha (0.025f));
    g.fillRect (plot.withRight (bx));
    g.setColour (theme::textDim);
    g.setFont (theme::font (11.0f));
    g.drawText ("Bass Mono", plot.withWidth (juce::jmax (60.0f, bx - plot.getX())).reduced (8.0f, 4.0f),
                juce::Justification::topLeft);

    drawFrequencyGrid (g, plot, true);

    // Before: the track's Mid level. After: Mid minus what Space and EQ took out of it.
    float before[kNumBands], midAfterLine[kNumBands], refLevels[kNumBands];
    for (int b = 0; b < kNumBands; ++b)
    {
        before[b] = own.levelMidDb[b];
        midAfterLine[b] = own.levelMidDb[b] - own.spaceDb[b] - own.eqDb[b];
        refLevels[b] = ref.levelMidDb[b];
    }

    g.setColour (ownColour.withAlpha (0.10f));
    g.fillPath (curve (before, plot, true));
    g.setColour (ownColour.withAlpha (0.35f));
    g.strokePath (curve (before, plot, false), juce::PathStrokeType (1.5f));

    g.setColour (ownColour);
    g.strokePath (curve (midAfterLine, plot, false), juce::PathStrokeType (2.5f));

    if (hasReference)
    {
        juce::Path dashed;
        const float dashes[] = { 6.0f, 5.0f };
        juce::PathStrokeType (2.0f).createDashedStroke (dashed, curve (refLevels, plot, false), dashes, 2);
        g.setColour (refColour);
        g.fillPath (dashed);
    }
    else
    {
        g.setColour (theme::textDim);
        g.setFont (theme::font (13.0f));
        g.drawText (theme::u8 ("Выбери референс сверху — от него трек будет уступать место"),
                    plot, juce::Justification::centred);
    }

    if (hasWave)
        paintWave (g, plot);
}

void SpectrumView::paintWave (juce::Graphics& g, juce::Rectangle<float> plot)
{
    // The wave hangs from a 0 dB line at the top of the plot; 12 dB fills 45 % of its height.
    const float top = plot.getY() + 1.0f;
    const float depth = plot.getHeight() * 0.45f;
    auto yFor = [&] (float cutDb) { return top + juce::jlimit (0.0f, kWaveRangeDb, cutDb) / kWaveRangeDb * depth; };
    auto xFor = [&] (int i) { return frequencyToX (waveHz[(size_t) i], plot); };

    // Space layer: 0 dB -> space cut.
    juce::Path spacePath, eqPath, spaceEdge, eqEdge, peakLine;
    spacePath.startNewSubPath (xFor (0), top);
    eqPath.startNewSubPath (xFor (0), yFor (std::max (0.0f, spaceCut[0])));
    for (int i = 0; i < kWavePoints; ++i)
    {
        const float s = std::max (0.0f, spaceCut[(size_t) i]);
        const float total = s + std::max (0.0f, eqCut[(size_t) i]);
        spacePath.lineTo (xFor (i), yFor (s));
        if (i == 0)
        {
            spaceEdge.startNewSubPath (xFor (i), yFor (s));
            eqEdge.startNewSubPath (xFor (i), yFor (total));
            peakLine.startNewSubPath (xFor (i), yFor (peakCut[(size_t) i]));
        }
        else
        {
            spaceEdge.lineTo (xFor (i), yFor (s));
            eqEdge.lineTo (xFor (i), yFor (total));
            peakLine.lineTo (xFor (i), yFor (peakCut[(size_t) i]));
        }
    }
    spacePath.lineTo (xFor (kWavePoints - 1), top);
    spacePath.closeSubPath();

    // EQ layer: space cut -> space + EQ cut.
    for (int i = 0; i < kWavePoints; ++i)
        eqPath.lineTo (xFor (i), yFor (std::max (0.0f, spaceCut[(size_t) i]) + std::max (0.0f, eqCut[(size_t) i])));
    for (int i = kWavePoints - 1; i >= 0; --i)
        eqPath.lineTo (xFor (i), yFor (std::max (0.0f, spaceCut[(size_t) i])));
    eqPath.closeSubPath();

    g.setColour (theme::space.withAlpha (0.30f));
    g.fillPath (spacePath);
    g.setColour (theme::eq.withAlpha (0.38f));
    g.fillPath (eqPath);
    g.setColour (theme::space.withAlpha (0.85f));
    g.strokePath (spaceEdge, juce::PathStrokeType (1.2f));
    g.setColour (theme::eq);
    g.strokePath (eqEdge, juce::PathStrokeType (1.6f));

    // Red edge where the release is not enough (per analysis band).
    const auto& layout = BandLayout::get();
    g.setColour (theme::conflict);
    for (int b = 0; b < kNumBands; ++b)
    {
        if (own.restDb[b] <= 0.25f)
            continue;
        const float x0 = frequencyToX (layout.edgeHz[(size_t) b], plot);
        const float x1 = frequencyToX (layout.edgeHz[(size_t) b + 1], plot);
        juce::Path edge;
        bool started = false;
        for (int i = 0; i < kWavePoints; ++i)
        {
            const float x = xFor (i);
            if (x < x0 || x > x1)
                continue;
            const float y = yFor (std::max (0.0f, spaceCut[(size_t) i]) + std::max (0.0f, eqCut[(size_t) i]));
            if (! started)
                edge.startNewSubPath (x, y), started = true;
            else
                edge.lineTo (x, y);
        }
        g.strokePath (edge, juce::PathStrokeType (2.5f));
    }

    // Peak line.
    const float dashes[] = { 3.0f, 3.0f };
    juce::Path dashedPeak;
    juce::PathStrokeType (1.0f).createDashedStroke (dashedPeak, peakLine, dashes, 2);
    g.setColour (theme::text.withAlpha (0.45f));
    g.fillPath (dashedPeak);

    // 0 dB line and the dB scale on the right.
    g.setColour (theme::borderLight);
    g.drawHorizontalLine ((int) top, plot.getX(), plot.getRight());
    g.setFont (theme::mono (10.0f));
    for (int db : { 0, 3, 6, 12 })
    {
        const float y = yFor ((float) db);
        g.setColour (theme::gridLine);
        g.drawHorizontalLine ((int) y, plot.getRight() - 6.0f, plot.getRight());
        g.setColour (theme::textDim);
        g.drawText (db == 0 ? juce::String ("0") : juce::String::fromUTF8 ("\xe2\x88\x92") + juce::String (db),
                    juce::Rectangle<float> (plot.getRight() + 4.0f, db == 0 ? y : y - 7.0f, 30.0f, 14.0f), juce::Justification::centredLeft);
    }

    // Hover readout.
    if (hoverX >= plot.getX() && hoverX <= plot.getRight())
    {
        const float t = (hoverX - plot.getX()) / plot.getWidth();
        const float hz = 20.0f * std::pow (1000.0f, t);
        const int i = juce::jlimit (0, kWavePoints - 1, (int) std::lround (t * (float) (kWavePoints - 1)));
        const float s = std::max (0.0f, spaceCut[(size_t) i]), e = std::max (0.0f, eqCut[(size_t) i]);

        g.setColour (theme::text.withAlpha (0.35f));
        g.drawVerticalLine ((int) hoverX, plot.getY(), plot.getBottom());

        // Minus sign and middle dot spelled as UTF-8 bytes: the same on every compiler.
        const auto text = shortHz (hz) + juce::String::fromUTF8 (" Hz  \xc2\xb7  Space \xe2\x88\x92") + juce::String (s, 1)
                          + juce::String::fromUTF8 ("  \xc2\xb7  EQ \xe2\x88\x92") + juce::String (e, 1) + " dB";
        const float w = 250.0f;
        auto box = juce::Rectangle<float> (juce::jmin (hoverX + 8.0f, plot.getRight() - w), yFor (s + e) + 8.0f, w, 22.0f);
        g.setColour (theme::raised.withAlpha (0.95f));
        g.fillRoundedRectangle (box, 6.0f);
        g.setColour (theme::text);
        g.setFont (theme::font (12.0f));
        g.drawText (text, box.reduced (8.0f, 0.0f), juce::Justification::centredLeft);
    }
}

void SpectrumView::mouseMove (const juce::MouseEvent& e)
{
    hoverX = (float) e.x;
    repaint();
}

void SpectrumView::mouseExit (const juce::MouseEvent&)
{
    hoverX = -1.0f;
    repaint();
}

//==============================================================================
void BudgetView::setData (const bus::TrackData& d)
{
    data = d;
    link = (d.flags & bus::flagLinkOff) == 0;

    double sp = 0.0, eq = 0.0;
    unresolved = 0;
    for (int b = 0; b < kNumBands; ++b)
    {
        sp += d.spaceDb[b];
        eq += d.eqDb[b];
        if (d.restDb[b] > 0.25f)
            ++unresolved;
    }
    spacePct = sp + eq > 0.01 ? (int) std::lround (100.0 * sp / (sp + eq)) : 0;
    repaint();
}

void BudgetView::paint (juce::Graphics& g)
{
    auto area = getLocalBounds().toFloat();
    auto labels = area.removeFromBottom (16.0f);

    g.setColour (theme::borderLight);
    g.drawHorizontalLine ((int) area.getY(), area.getX(), area.getRight());

    constexpr float dbScale = 12.0f;   // full height = 12 dB of release
    const float slot = area.getWidth() / (float) kNumBands;
    const float barW = juce::jmax (3.0f, slot * 0.62f);
    const auto restColour = link ? theme::conflict : theme::idle;

    for (int b = 0; b < kNumBands; ++b)
    {
        const float x = area.getX() + slot * ((float) b + 0.5f) - barW * 0.5f;
        float y = area.getY() + 2.0f;
        auto segment = [&] (float db, juce::Colour c) {
            const float h = juce::jmin (area.getBottom() - y, db / dbScale * (area.getHeight() - 2.0f));
            if (h > 0.4f)
            {
                g.setColour (c);
                g.fillRoundedRectangle (x, y, barW, h, 2.0f);
                y += h;
            }
        };
        segment (data.spaceDb[b], theme::space);
        segment (data.eqDb[b], theme::eq);
        segment (data.restDb[b], restColour);
    }

    const auto& layout = BandLayout::get();
    g.setFont (theme::mono (10.5f));
    g.setColour (theme::textDim);
    for (int b : { 0, 6, 12, 18, 24, 31 })
    {
        const float cx = juce::jlimit (area.getX() + 24.0f, area.getRight() - 24.0f, area.getX() + slot * ((float) b + 0.5f));
        g.drawText (shortHz (layout.centreHz[(size_t) b]), juce::Rectangle<float> (cx - 24.0f, labels.getY(), 48.0f, labels.getHeight()),
                    juce::Justification::centred);
    }
}

} // namespace ff
