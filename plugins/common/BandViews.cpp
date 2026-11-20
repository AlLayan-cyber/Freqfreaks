// FreqFreaks — frequency-unmasking plugin suite
// Copyright (C) 2026 FreqFreaks contributors
// SPDX-License-Identifier: GPL-3.0-or-later
#include "BandViews.h"

namespace ff
{

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
void SpectrumView::setData (const bus::TrackData& ownData, juce::Colour ownCol,
                            const bus::TrackData* reference, juce::Colour referenceColour,
                            juce::String referenceName, float bassMonoHz)
{
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
    auto bounds = getLocalBounds().toFloat();
    auto plot = bounds.withTrimmedBottom (18.0f);

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
        const float hz = layout.centreHz[(size_t) b];
        const auto text = hz >= 1000.0f ? juce::String (hz / 1000.0f, 1) + "k" : juce::String ((int) hz);
        const float cx = area.getX() + slot * ((float) b + 0.5f);
        g.drawText (text, juce::Rectangle<float> (cx - 24.0f, labels.getY(), 48.0f, labels.getHeight()), juce::Justification::centred);
    }
}

} // namespace ff
