// FreqFreaks — frequency-unmasking plugin suite
// Copyright (C) 2026 FreqFreaks contributors
// SPDX-License-Identifier: GPL-3.0-or-later
#include "RangeView.h"
#include "../common/BandViews.h"

namespace ff
{

namespace
{
    constexpr float kLowHz = 20.0f, kHighHz = 20000.0f;
    const char* const ids[] = { trackParams::rangeLow, trackParams::rangeHigh, trackParams::bassMono };
    constexpr float defaults[] = { 60.0f, 14000.0f, 150.0f };

    juce::String hzText (float hz)
    {
        return hz >= 1000.0f ? juce::String (hz / 1000.0f, hz >= 10000.0f ? 1 : 2) + " kHz" : juce::String (juce::roundToInt (hz)) + " Hz";
    }
}

RangeView::RangeView (TrackProcessor& p, Kind k) : processor (p), kind (k)
{
    setTooltip (kind == Kind::eq
                    ? theme::u8 ("Range\nЯнтарные края: вне них плагин ничего не трогает. Точки — полосы EQ, светятся там, "
                                 "где работают. Тяни край, колесо — точно, двойной клик — по умолчанию.")
                    : theme::u8 ("Bass Mono\nЛевее бирюзовой границы Space не работает: низ остаётся в моно, там освобождает "
                                 "только EQ. Точки — полосы Space. Бледное окно — Space Amount на 0%, Space выключен."));
    setRepaintsOnMouseActivity (true);
}

juce::Rectangle<float> RangeView::plot() const   { return getLocalBounds().toFloat().reduced (6.0f, 2.0f).withTrimmedBottom (14.0f); }

float RangeView::xFor (float hz) const
{
    const auto r = plot();
    return r.getX() + std::log (juce::jlimit (kLowHz, kHighHz, hz) / kLowHz) / std::log (kHighHz / kLowHz) * r.getWidth();
}

float RangeView::hzFor (float x) const
{
    const auto r = plot();
    return kLowHz * std::pow (kHighHz / kLowHz, juce::jlimit (0.0f, 1.0f, (x - r.getX()) / r.getWidth()));
}

juce::RangedAudioParameter* RangeView::param (Edge e) const
{
    return e == none ? nullptr : processor.state().getParameter (ids[e]);
}

float RangeView::value (Edge e) const
{
    return e == none ? 0.0f : processor.state().getRawParameterValue (ids[e])->load();
}

void RangeView::set (Edge e, float hz)
{
    auto* prm = param (e);
    if (prm == nullptr)
        return;
    // Range low stays at least a third of an octave below Range high, and the other way round.
    if (e == rangeLow)
        hz = juce::jmin (hz, value (rangeHigh) / 1.26f);
    if (e == rangeHigh)
        hz = juce::jmax (hz, value (rangeLow) * 1.26f);
    const auto& range = prm->getNormalisableRange();
    prm->setValueNotifyingHost (prm->convertTo0to1 (juce::jlimit (range.start, range.end, hz)));
    repaint();
}

RangeView::Edge RangeView::edgeAt (float x) const
{
    Edge best = none;
    float bestDist = 9.0f;
    for (auto e : { bassMono, rangeLow, rangeHigh })
    {
        if ((kind == Kind::eq) == (e == bassMono))
            continue;   // each window drags only its own edges
        const float d = std::abs (xFor (value (e)) - x);
        if (d < bestDist)
            bestDist = d, best = e;
    }
    return best;
}

void RangeView::paint (juce::Graphics& g)
{
    const auto r = plot();
    g.setColour (theme::panelDeep);
    g.fillRoundedRectangle (getLocalBounds().toFloat(), 6.0f);

    drawFrequencyGrid (g, r, false);
    g.setFont (theme::font (10.0f));
    g.setColour (theme::textDim);
    for (float hz : { 50.0f, 200.0f, 1000.0f, 5000.0f, 15000.0f })
        g.drawText (shortHz (hz), juce::Rectangle<float> (xFor (hz) - 18.0f, r.getBottom() + 1.0f, 36.0f, 12.0f), juce::Justification::centred);

    const float lo = value (rangeLow), hi = value (rangeHigh), bm = value (bassMono);
    const float xLo = xFor (lo), xHi = xFor (hi), xBm = xFor (bm);

    // Outside Range: dimmed (nothing happens there).
    g.setColour (juce::Colours::black.withAlpha (0.45f));
    g.fillRect (r.withRight (xLo));
    g.fillRect (r.withLeft (xHi));
    if (kind == Kind::eq)
    {
        g.setColour (theme::eq.withAlpha (0.07f));
        g.fillRect (juce::Rectangle<float> (xLo, r.getY(), xHi - xLo, r.getHeight()));
    }
    else
    {
        // Below Bass Mono: mono, EQ only (grey); above it, up to Range high: Space works (teal).
        g.setColour (juce::Colours::white.withAlpha (0.05f));
        g.fillRect (juce::Rectangle<float> (xLo, r.getY(), juce::jmax (0.0f, juce::jmin (xBm, xHi) - xLo), r.getHeight()));
        const float from = juce::jmax (xBm, xLo);
        g.setColour (theme::space.withAlpha (0.07f));
        g.fillRect (juce::Rectangle<float> (from, r.getY(), juce::jmax (0.0f, xHi - from), r.getHeight()));
    }

    // The plug-in's bells: lit when they may work (EQ inside Range; Space also above Bass Mono).
    const auto& sb = processor.state();
    const int step = sharpStepFor (sb.getRawParameterValue (trackParams::sharp)->load() / 100.0f);
    const auto& bank = processor.bellBank (step);
    for (int k = 0; k < bank.size(); ++k)
    {
        const float hz = bank.centreHz (k);
        const bool eqOn = hz >= lo * 1.1f && hz <= hi / 1.1f;
        const bool spaceOn = eqOn && hz >= juce::jmax (bm, lo) * 1.25f;
        const float x = xFor (hz), y = r.getCentreY();
        if (kind == Kind::eq)
            g.setColour (eqOn ? theme::eq : theme::idle);
        else
            g.setColour (spaceOn ? theme::space : theme::idle);
        g.fillEllipse (x - 2.5f, y - 2.5f, 5.0f, 5.0f);
    }

    auto edge = [&] (Edge e, float x, juce::Colour c, const juce::String& label, bool labelRight) {
        const bool active = hover == e || dragging == e;
        g.setColour (c.withAlpha (active ? 1.0f : 0.85f));
        g.fillRect (x - (active ? 1.5f : 1.0f), r.getY(), active ? 3.0f : 2.0f, r.getHeight());
        g.fillRoundedRectangle (x - 4.0f, r.getY() + 2.0f, 8.0f, 12.0f, 3.0f);
        g.setFont (theme::font (10.5f, active));
        const auto text = label + " " + hzText (value (e));
        const float w = juce::GlyphArrangement::getStringWidth (theme::font (10.5f, active), text) + 6.0f;
        auto box = juce::Rectangle<float> (labelRight ? x + 6.0f : x - 6.0f - w, r.getBottom() - 15.0f, w, 13.0f);
        box = box.withX (juce::jlimit (r.getX(), r.getRight() - w, box.getX()));
        g.setColour (theme::panelDeep.withAlpha (0.85f));
        g.fillRoundedRectangle (box, 3.0f);
        g.setColour (c);
        g.drawText (text, box, juce::Justification::centred);
    };
    if (kind == Kind::eq)
    {
        edge (rangeLow, xLo, theme::eq, "Range", false);
        edge (rangeHigh, xHi, theme::eq, "Range", false);
    }
    else
    {
        edge (bassMono, xBm, theme::space, "Bass Mono", true);
    }

    g.setColour (theme::border);
    g.drawRoundedRectangle (getLocalBounds().toFloat().reduced (0.5f), 6.0f, 1.0f);
}

void RangeView::mouseMove (const juce::MouseEvent& e)
{
    const auto h = edgeAt ((float) e.x);
    if (h != hover)
    {
        hover = h;
        setMouseCursor (hover == none ? juce::MouseCursor::NormalCursor : juce::MouseCursor::LeftRightResizeCursor);
        repaint();
    }
}

void RangeView::mouseExit (const juce::MouseEvent&)
{
    hover = none;
    repaint();
}

void RangeView::mouseDown (const juce::MouseEvent& e)
{
    dragging = edgeAt ((float) e.x);
    if (auto* prm = param (dragging))
        prm->beginChangeGesture();
}

void RangeView::mouseDrag (const juce::MouseEvent& e)
{
    if (dragging != none)
        set (dragging, hzFor ((float) e.x));
}

void RangeView::mouseUp (const juce::MouseEvent&)
{
    if (auto* prm = param (dragging))
        prm->endChangeGesture();
    dragging = none;
    repaint();
}

void RangeView::mouseDoubleClick (const juce::MouseEvent& e)
{
    const auto h = edgeAt ((float) e.x);
    if (auto* prm = param (h))
    {
        prm->beginChangeGesture();
        set (h, defaults[h]);
        prm->endChangeGesture();
    }
}

void RangeView::mouseWheelMove (const juce::MouseEvent& e, const juce::MouseWheelDetails& wheel)
{
    const auto h = edgeAt ((float) e.x);
    if (auto* prm = param (h))
    {
        // 1/24 octave per notch.
        prm->beginChangeGesture();
        set (h, value (h) * std::pow (2.0f, (wheel.deltaY > 0.0f ? 1.0f : -1.0f) / 24.0f));
        prm->endChangeGesture();
    }
}

} // namespace ff
