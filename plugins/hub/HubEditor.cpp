// FreqFreaks — frequency-unmasking plugin suite
// Copyright (C) 2026 FreqFreaks contributors
// SPDX-License-Identifier: GPL-3.0-or-later
#include "HubEditor.h"
#include "../common/ScalingEditor.h"
#include "../common/Brand.h"
#include "../common/AboutPanel.h"

#include <map>

namespace ff
{

using theme::u8;

namespace
{
    constexpr int groupCompare = 2001, groupTabs = 2002;

    juce::String hzText (float hz)
    {
        if (hz >= 10000.0f)
            return juce::String (juce::roundToInt (hz / 1000.0f)) + u8 (" кГц");
        if (hz >= 1000.0f)
            return juce::String (std::round (hz / 100.0f) / 10.0f) + u8 (" кГц");
        return juce::String (juce::roundToInt (hz / 10.0f) * 10) + u8 (" Гц");
    }

    juce::String dbText (float db)
    {
        return (db > 0.05f ? u8 ("−") : juce::String()) + juce::String (std::abs (db), 1) + " dB";
    }

    void drawLogo (juce::Graphics& g, juce::Point<float> at)
    {
        const float xs[] = { 0.0f, 7.0f, 14.0f, 21.0f };
        const float ys[] = { 8.0f, 2.0f, 5.0f, 0.0f };
        const float hs[] = { 8.0f, 16.0f, 10.0f, 20.0f };
        for (int i = 0; i < 4; ++i)
        {
            g.setColour (i < 2 ? theme::space : theme::eq);
            g.fillRoundedRectangle (at.x + xs[i], at.y + ys[i], 4.0f, hs[i], 2.0f);
        }
    }
}

//==============================================================================
void HubCanvas::paint (juce::Graphics& g)
{
    auto area = getLocalBounds().toFloat();
    drawCard (g, area);
    area = area.reduced (16.0f);

    if (tracks.empty())
    {
        g.setColour (theme::textDim);
        g.setFont (theme::font (14.0f));
        g.drawFittedText (u8 ("Пока нет ни одного FreqFreaks Track.\nПоставь Track на дорожки — они появятся здесь."),
                          area.toNearestInt(), juce::Justification::centred, 3);
        return;
    }

    switch (view)
    {
        case View::map:    paintMap (g, area); break;
        case View::matrix: paintMatrix (g, area); break;
        case View::stereo: paintStereo (g, area); break;
    }
}

void HubCanvas::paintMap (juce::Graphics& g, juce::Rectangle<float> area)
{
    auto title = area.removeFromTop (24.0f);
    g.setColour (theme::text);
    g.setFont (theme::font (14.0f, true));
    g.drawText (u8 ("Карта маскировки · сейчас"), title.removeFromLeft (260.0f), juce::Justification::centredLeft);
    g.setColour (theme::textDim);
    g.setFont (theme::font (11.5f));
    g.drawText (u8 ("яркость = сколько освобождается · красная рамка = не хватает"), title, juce::Justification::centredRight);
    area.removeFromTop (10.0f);

    auto labels = area.removeFromBottom (16.0f);
    const float labelW = 110.0f;
    const int n = (int) tracks.size();
    const float rowH = juce::jmin (44.0f, area.getHeight() / (float) n);
    const float cellW = (area.getWidth() - labelW) / (float) kNumBands;

    for (int t = 0; t < n; ++t)
    {
        const auto& tr = tracks[(size_t) t];
        const float y = area.getY() + rowH * (float) t;
        g.setColour (tr.colour);
        g.fillEllipse (area.getX(), y + rowH * 0.5f - 4.0f, 8.0f, 8.0f);
        g.setColour (theme::text);
        g.setFont (theme::font (12.0f));
        g.drawText (tr.name, juce::Rectangle<float> (area.getX() + 14.0f, y, labelW - 18.0f, rowH), juce::Justification::centredLeft, true);

        const bool bypassed = (tr.data.flags & bus::flagBypassed) != 0;
        for (int b = 0; b < kNumBands; ++b)
        {
            const auto cell = juce::Rectangle<float> (area.getX() + labelW + cellW * (float) b, y, cellW, rowH).reduced (1.5f);
            const float total = tr.data.totalDb[b];
            const float alpha = bypassed ? 0.04f : juce::jlimit (0.05f, 1.0f, total / 10.0f);
            g.setColour (theme::conflict.withAlpha (alpha));
            g.fillRoundedRectangle (cell, 3.0f);

            if (tr.data.restDb[b] > 0.25f && ! bypassed)
            {
                g.setColour (theme::conflict);
                g.drawRoundedRectangle (cell.reduced (0.5f), 3.0f, 1.5f);
            }
            if (t == focusTrack && b >= focusFrom && b <= focusTo)
            {
                g.setColour (theme::text);
                g.drawRoundedRectangle (cell.reduced (0.5f), 3.0f, 2.0f);
            }
        }
    }

    const auto& layout = BandLayout::get();
    g.setColour (theme::textDim);
    g.setFont (theme::mono (10.5f));
    for (int b : { 0, 6, 12, 18, 24, 31 })
    {
        const float cx = juce::jmin (area.getRight() - 24.0f, area.getX() + labelW + cellW * ((float) b + 0.5f));
        g.drawText (shortHz (layout.centreHz[(size_t) b]),
                    juce::Rectangle<float> (cx - 24.0f, labels.getY(), 48.0f, 16.0f), juce::Justification::centred);
    }
}

void HubCanvas::paintMatrix (juce::Graphics& g, juce::Rectangle<float> area)
{
    // Columns: every track that someone yields to.
    std::vector<std::uint64_t> refs;
    for (const auto& t : tracks)
        if (t.data.references[0] != 0 && std::find (refs.begin(), refs.end(), t.data.references[0]) == refs.end())
            refs.push_back (t.data.references[0]);

    auto nameOf = [this] (std::uint64_t id) -> std::pair<juce::String, juce::Colour> {
        for (const auto& t : tracks)
            if (t.id == id)
                return { t.name, t.colour };
        return { u8 ("нет сигнала"), theme::textDim };
    };

    g.setFont (theme::font (12.0f));
    if (refs.empty())
    {
        g.setColour (theme::textDim);
        g.drawText (u8 ("Ни один трек пока не выбрал референс."), area, juce::Justification::centred);
        return;
    }

    const float labelW = 140.0f;
    const float colW = juce::jmin (230.0f, (area.getWidth() - labelW) / (float) refs.size());
    auto head = area.removeFromTop (28.0f);
    g.setColour (theme::textDim);
    g.setFont (theme::font (11.5f));
    g.drawText (u8 ("уступает ↓  /  кому →"), head.withWidth (labelW), juce::Justification::centredLeft);
    for (size_t c = 0; c < refs.size(); ++c)
    {
        const auto [n, col] = nameOf (refs[c]);
        auto cell = juce::Rectangle<float> (head.getX() + labelW + colW * (float) c, head.getY(), colW, head.getHeight());
        g.setColour (col);
        g.fillEllipse (cell.getX() + 4.0f, cell.getCentreY() - 4.0f, 8.0f, 8.0f);
        g.setColour (theme::text);
        g.setFont (theme::font (13.0f, true));
        g.drawText (n, cell.withTrimmedLeft (18.0f), juce::Justification::centredLeft, true);
    }

    const float rowH = 84.0f;
    float y = area.getY() + 6.0f;
    for (const auto& t : tracks)
    {
        if (t.data.references[0] == 0)
            continue;
        if (y + rowH > area.getBottom())
            break;

        g.setColour (t.colour);
        g.fillEllipse (area.getX(), y + rowH * 0.5f - 4.0f, 8.0f, 8.0f);
        g.setColour (theme::text);
        g.setFont (theme::font (13.0f, true));
        g.drawText (t.name, juce::Rectangle<float> (area.getX() + 14.0f, y, labelW - 18.0f, rowH), juce::Justification::centredLeft, true);

        for (size_t c = 0; c < refs.size(); ++c)
        {
            auto cell = juce::Rectangle<float> (area.getX() + labelW + colW * (float) c, y, colW, rowH).reduced (5.0f);
            g.setColour (theme::panelDeep);
            g.fillRoundedRectangle (cell, 10.0f);
            g.setColour (theme::border);
            g.drawRoundedRectangle (cell, 10.0f, 1.0f);

            if (t.data.references[0] != refs[c])
            {
                g.setColour (theme::idle);
                g.drawText ("+", cell, juce::Justification::centred);
                continue;
            }

            double sp = 0.0, eq = 0.0;
            float peak = 0.0f;
            for (int b = 0; b < kNumBands; ++b)
            {
                sp += t.data.spaceDb[b];
                eq += t.data.eqDb[b];
                peak = juce::jmax (peak, t.data.spaceDb[b] + t.data.eqDb[b]);
            }
            const float share = sp + eq > 0.01 ? (float) (sp / (sp + eq)) : 0.0f;

            auto ring = cell.removeFromLeft (cell.getHeight()).reduced (12.0f);
            juce::Path eqRing, spaceRing;
            eqRing.addPieSegment (ring, 0.0f, juce::MathConstants<float>::twoPi, 0.68f);
            g.setColour (sp + eq > 0.01 ? theme::eq : theme::track);
            g.fillPath (eqRing);
            if (share > 0.001f)
            {
                spaceRing.addPieSegment (ring, 0.0f, juce::MathConstants<float>::twoPi * share, 0.68f);
                g.setColour (theme::space);
                g.fillPath (spaceRing);
            }

            g.setColour (theme::text);
            g.setFont (theme::mono (15.0f));
            g.drawText (dbText (peak), cell.removeFromTop (cell.getHeight() * 0.55f), juce::Justification::bottomLeft);
            g.setColour (theme::textDim);
            g.setFont (theme::font (11.0f));
            g.drawText ("Space " + juce::String (juce::roundToInt (share * 100.0f)) + "% · EQ "
                            + juce::String (sp + eq > 0.01 ? 100 - juce::roundToInt (share * 100.0f) : 0) + "%",
                        cell, juce::Justification::topLeft);
        }
        y += rowH;
    }

    g.setColour (theme::textDim);
    g.setFont (theme::font (11.5f));
    g.drawText (u8 ("Кольцо: бирюзовое — Space, янтарное — EQ. Цифра — освобождение в самой конфликтной полосе."),
                area.removeFromBottom (18.0f), juce::Justification::centredLeft);
}

void HubCanvas::paintStereo (juce::Graphics& g, juce::Rectangle<float> area)
{
    auto title = area.removeFromTop (24.0f);
    g.setColour (theme::text);
    g.setFont (theme::font (14.0f, true));
    g.drawText (u8 ("Стерео · ширина каждого трека по частотам"), title, juce::Justification::centredLeft);
    area.removeFromTop (8.0f);

    auto legend = area.removeFromBottom (22.0f);
    auto axis = area.removeFromBottom (16.0f);
    auto freqLabels = area.removeFromLeft (44.0f);
    auto plot = area;

    g.setColour (theme::panelDeep);
    g.fillRoundedRectangle (plot, 10.0f);

    const auto& layout = BandLayout::get();
    const float rowH = plot.getHeight() / (float) kNumBands;
    auto rowY = [&] (int b) { return plot.getBottom() - rowH * (float) (b + 1); };

    // Bass Mono zone (150 Hz by default on every track).
    const int bassBand = layout.bandForHz (150.0f);
    g.setColour (juce::Colours::white.withAlpha (0.03f));
    g.fillRect (plot.withTop (rowY (bassBand)));
    g.setColour (theme::textDim);
    g.setFont (theme::font (11.0f));
    g.drawText ("Bass Mono", plot.withTop (rowY (bassBand)).reduced (10.0f, 4.0f), juce::Justification::bottomLeft);

    g.setColour (theme::borderLight);
    g.drawVerticalLine ((int) plot.getCentreX(), plot.getY(), plot.getBottom());

    for (const auto& t : tracks)
    {
        if ((t.data.flags & bus::flagBypassed) != 0)
            continue;
        for (int b = 0; b < kNumBands; ++b)
        {
            const float midP = dbToPower (t.data.levelMidDb[b]);
            const float sideP = dbToPower (t.data.levelSideDb[b]);
            const float level = powerToDb (midP + sideP);
            if (level < -66.0f)
                continue;
            const float width = sideP / (midP + sideP + 1.0e-12f);
            const float half = (0.04f + 0.96f * std::sqrt (width)) * plot.getWidth() * 0.5f;
            const float alpha = juce::jlimit (0.06f, 0.45f, (level + 66.0f) / 80.0f);
            auto capsule = juce::Rectangle<float> (plot.getCentreX() - half, rowY (b) + 1.0f, half * 2.0f, rowH - 2.0f);
            g.setColour (t.colour.withAlpha (alpha));
            g.fillRoundedRectangle (capsule, rowH * 0.5f);
        }
    }

    g.setColour (theme::textDim);
    g.setFont (theme::mono (10.5f));
    for (int b : { 0, 6, 12, 18, 24, 31 })
    {
        g.drawText (shortHz (layout.centreHz[(size_t) b]),
                    freqLabels.withY (rowY (b)).withHeight (rowH), juce::Justification::centredRight);
    }
    g.setFont (theme::font (11.0f));
    g.drawText ("L", axis.withX (plot.getX()).withWidth (40.0f), juce::Justification::centredLeft);
    g.drawText (u8 ("центр"), axis.withX (plot.getCentreX() - 30.0f).withWidth (60.0f), juce::Justification::centred);
    g.drawText ("R", axis.withX (plot.getRight() - 40.0f).withWidth (40.0f), juce::Justification::centredRight);

    float x = legend.getX() + 44.0f;
    for (const auto& t : tracks)
    {
        g.setColour (t.colour);
        g.fillEllipse (x, legend.getCentreY() - 4.0f, 8.0f, 8.0f);
        g.setColour (theme::textDim);
        g.drawText (t.name, juce::Rectangle<float> (x + 12.0f, legend.getY(), 110.0f, legend.getHeight()), juce::Justification::centredLeft, true);
        x += 120.0f;
        if (x > legend.getRight() - 100.0f)
            break;
    }
}

//==============================================================================
void IssuePanel::setIssues (std::vector<HubIssue> list)
{
    issues = std::move (list);
    repaint();
}

void IssuePanel::paint (juce::Graphics& g)
{
    auto area = getLocalBounds().reduced (16);
    g.setColour (theme::text);
    g.setFont (theme::font (15.0f, true));
    g.drawText (u8 ("Диагноз"), area.removeFromTop (24), juce::Justification::centredLeft);
    g.setColour (theme::textDim);
    g.setFont (theme::font (11.5f));
    g.drawText (u8 ("Правила, без ИИ. Нажми пункт — покажу на карте."), area.removeFromTop (18), juce::Justification::centredLeft);
    area.removeFromTop (10);

    itemBounds.clear();
    if (issues.empty())
    {
        g.setColour (theme::space);
        g.setFont (theme::font (13.0f, true));
        g.drawText (u8 ("Конфликтов нет"), area.removeFromTop (24), juce::Justification::centredLeft);
        return;
    }

    for (size_t i = 0; i < issues.size(); ++i)
    {
        const auto& is = issues[i];
        // Height from the text itself, so a long advice is not squeezed into a tiny font.
        juce::GlyphArrangement titleLines, hintLines;
        const float textW = (float) (area.getWidth() - 24 - 14);
        titleLines.addJustifiedText (theme::font (13.0f, true), is.title, 0.0f, 0.0f, textW, juce::Justification::topLeft);
        const auto hintText = is.ageSeconds > 0 ? is.hint + u8 (" (было ") + juce::String (is.ageSeconds) + u8 (" с назад)") : is.hint;
        hintLines.addJustifiedText (theme::font (11.5f), hintText, 0.0f, 0.0f, textW, juce::Justification::topLeft);
        const int titleH = juce::roundToInt (titleLines.getBoundingBox (0, -1, true).getHeight()) + 6;
        const int hintH = juce::roundToInt (hintLines.getBoundingBox (0, -1, true).getHeight()) + 2;
        auto box = area.removeFromTop (20 + titleH + hintH);
        area.removeFromTop (8);
        if (box.getBottom() > getHeight() - 8)
            break;
        itemBounds.push_back (box);

        const bool isSelected = is.key == selectedKey;
        const bool past = is.ageSeconds > 0;
        g.setOpacity (1.0f);
        g.setColour (isSelected ? theme::raised : theme::panel);
        g.fillRoundedRectangle (box.toFloat(), 10.0f);
        g.setColour (isSelected ? theme::borderLight : theme::border);
        g.drawRoundedRectangle (box.toFloat().reduced (0.5f), 10.0f, 1.0f);

        auto inner = box.reduced (12, 10);
        auto dot = inner.removeFromLeft (14);
        g.setColour ((is.severe ? theme::conflict : theme::eq).withAlpha (past ? 0.45f : 1.0f));
        g.fillEllipse ((float) dot.getX(), (float) inner.getY() + 5.0f, 8.0f, 8.0f);

        g.setColour (theme::text.withAlpha (past ? 0.55f : 1.0f));
        g.setFont (theme::font (13.0f, true));
        g.drawFittedText (is.title, inner.removeFromTop (titleH), juce::Justification::topLeft, 4, 1.0f);
        g.setColour (theme::textDim.withAlpha (past ? 0.6f : 1.0f));
        g.setFont (theme::font (11.5f));
        g.drawFittedText (hintText, inner, juce::Justification::topLeft, 10, 1.0f);
    }
}

void IssuePanel::mouseDown (const juce::MouseEvent& e)
{
    for (size_t i = 0; i < itemBounds.size(); ++i)
    {
        if (itemBounds[i].contains (e.getPosition()))
        {
            selectedKey = issues[i].key;
            repaint();
            if (onSelect)
                onSelect (issues[i]);
            return;
        }
    }
}

//==============================================================================
TrackRow::TrackRow (HubProcessor& p, std::uint64_t id) : processor (p), trackId (id)
{
    addAndMakeVisible (trim);   // first, so the text box gets our look
    trim.setSliderStyle (juce::Slider::LinearHorizontal);
    trim.setTextBoxStyle (juce::Slider::TextBoxRight, false, 46, 18);
    trim.setRange (0.0, 150.0, 1.0);
    trim.setTextValueSuffix ("%");
    trim.setDoubleClickReturnValue (true, 100.0);
    trim.setColour (juce::Slider::trackColourId, theme::text);
    trim.setValue (processor.trimFor (trackId) * 100.0f, juce::dontSendNotification);
    trim.setTooltip (u8 ("Больше / меньше подавления на этой дорожке (умножает её Amount). Двойной клик — 100%"));
    trim.onValueChange = [this] { processor.setTrim (trackId, (float) trim.getValue() / 100.0f); };

    bypass.setClickingTogglesState (true);
    bypass.setColour (juce::TextButton::buttonOnColourId, theme::conflict.withAlpha (0.18f));
    bypass.setColour (juce::TextButton::textColourOnId, theme::conflict);
    bypass.setColour (juce::ComboBox::outlineColourId, theme::borderLight);
    bypass.setToggleState (processor.isTrackBypassed (trackId), juce::dontSendNotification);
    bypass.setTooltip (u8 ("Выключить FreqFreaks на этой дорожке"));
    bypass.onClick = [this] { processor.setTrackBypassed (trackId, bypass.getToggleState()); };
    addAndMakeVisible (bypass);
}

void TrackRow::setCompact (bool c)
{
    if (compact == c)
        return;
    compact = c;
    resized();
    repaint();
}

void TrackRow::update (const HubTrack& t, bool isProblem)
{
    track = t;
    problem = isProblem;
    if (! trim.isMouseButtonDown())
        trim.setValue (processor.trimFor (trackId) * 100.0f, juce::dontSendNotification);
    bypass.setToggleState (processor.isTrackBypassed (trackId), juce::dontSendNotification);
    repaint();
}

void TrackRow::paint (juce::Graphics& g)
{
    auto r = getLocalBounds().toFloat();
    g.setColour (theme::panel);
    g.fillRoundedRectangle (r, compact ? 6.0f : 9.0f);
    g.setColour (problem ? theme::conflict.withAlpha (0.6f) : theme::border);
    g.drawRoundedRectangle (r.reduced (0.5f), compact ? 6.0f : 9.0f, 1.0f);

    if (compact)
    {
        auto line = getLocalBounds().reduced (8, 0);
        g.setColour (track.colour);
        g.fillEllipse ((float) line.getX(), (float) line.getCentreY() - 4.0f, 8.0f, 8.0f);
        line.removeFromLeft (14);
        g.setColour (theme::text);
        g.setFont (theme::font (12.0f, true));
        g.drawText (track.name, line.withWidth (78), juce::Justification::centredLeft, true);
        return;
    }

    auto top = getLocalBounds().reduced (10, 6).removeFromTop (34);
    g.setColour (track.colour);
    g.fillEllipse ((float) top.getX(), (float) top.getY() + 5.0f, 10.0f, 10.0f);
    top.removeFromLeft (18);
    g.setColour (theme::text);
    g.setFont (theme::font (13.0f, true));
    g.drawText (track.name, top.removeFromTop (18), juce::Justification::centredLeft, true);

    juce::String sub;
    if ((track.data.flags & bus::flagHubBypassed) != 0)
        sub = u8 ("обход");
    else if (track.data.references[0] == 0)
        sub = u8 ("референс");
    else
        sub = u8 ("→ ") + track.referenceName + u8 (" · ") + juce::String (juce::roundToInt (track.data.amount * 100.0f)) + "%";
    g.setColour (problem ? theme::conflict : theme::textDim);
    g.setFont (theme::font (11.0f));
    g.drawText (sub, top, juce::Justification::centredLeft, true);
}

void TrackRow::resized()
{
    if (compact)
    {
        auto r = getLocalBounds().reduced (8, 6);
        r.removeFromLeft (14 + 80);
        bypass.setBounds (r.removeFromRight (52));
        r.removeFromRight (4);
        trim.setBounds (r);
        return;
    }
    auto r = getLocalBounds().reduced (10, 6);
    r.removeFromTop (36);
    bypass.setBounds (r.removeFromRight (58).withHeight (22));
    r.removeFromRight (6);
    trim.setBounds (r.withHeight (22));
}

void TrackRow::mouseDoubleClick (const juce::MouseEvent&)
{
    if (onOpen)
        onOpen (trackId);
}

//==============================================================================
RemotePanel::RemotePanel (HubProcessor& p) : processor (p)
{
    for (auto* s : { &amount, &space })
    {
        addAndMakeVisible (*s);   // first, so the text box gets our look
        s->setSliderStyle (juce::Slider::LinearHorizontal);
        s->setTextBoxStyle (juce::Slider::TextBoxRight, false, 60, 22);
        s->setTextValueSuffix ("%");
    }
    amount.setRange (0.0, 100.0, 1.0);
    space.setRange (0.0, 200.0, 1.0);
    amount.setColour (juce::Slider::trackColourId, theme::text);
    space.setColour (juce::Slider::trackColourId, theme::space);
    amount.onDragEnd = [this] { send (bus::remoteAmount); };
    space.onDragEnd = [this] { send (bus::remoteSpace); };
    amount.onValueChange = [this] { if (! amount.isMouseButtonDown()) send (bus::remoteAmount); };
    space.onValueChange = [this] { if (! space.isMouseButtonDown()) send (bus::remoteSpace); };

    link.setClickingTogglesState (true);
    link.setColour (juce::TextButton::buttonOnColourId, theme::space.withAlpha (0.14f));
    link.setColour (juce::TextButton::textColourOnId, theme::space);
    link.setColour (juce::ComboBox::outlineColourId, theme::borderLight);
    link.onClick = [this] { send (bus::remoteLink); };
    addAndMakeVisible (link);

    styleChip (modeSpread, theme::space, 3001);
    styleChip (modePan, theme::space, 3001);
    styleChip (modeDecor, theme::space, 3001);
    modeSpread.onClick = [this] { setMode (0); };
    modePan.onClick = [this] { setMode (1); };
    modeDecor.onClick = [this] { setMode (2); };
    for (auto* b : { &modeSpread, &modePan, &modeDecor })
        addAndMakeVisible (*b);

    reference.onChange = [this] { send (bus::remoteReference); };
    addAndMakeVisible (reference);

    close.setTooltip (u8 ("Закрыть"));
    close.onClick = [this] { if (onClose) onClose(); };
    addAndMakeVisible (close);
}

void RemotePanel::open (const HubTrack& t, const std::vector<HubTrack>& all)
{
    target = t;
    referenceIds.clear();
    reference.clear (juce::dontSendNotification);
    referenceIds.push_back (0);
    reference.addItem (u8 ("— нет —"), 1);
    for (const auto& other : all)
    {
        if (other.id == t.id)
            continue;
        referenceIds.push_back (other.id);
        reference.addItem (other.name, (int) referenceIds.size());
    }
    update (all);
    setVisible (true);
    toFront (false);
}

void RemotePanel::update (const std::vector<HubTrack>& all)
{
    for (const auto& t : all)
        if (t.id == target.id)
            target = t;

    const auto& d = target.data;
    if (! amount.isMouseButtonDown())
        amount.setValue (d.amount * 100.0f, juce::dontSendNotification);
    if (! space.isMouseButtonDown())
        space.setValue (d.spaceAmount * 100.0f, juce::dontSendNotification);
    link.setToggleState ((d.flags & bus::flagLinkOff) == 0, juce::dontSendNotification);
    mode = (int) d.spaceMode;
    modeSpread.setToggleState (mode == 0, juce::dontSendNotification);
    modePan.setToggleState (mode == 1, juce::dontSendNotification);
    modeDecor.setToggleState (mode == 2, juce::dontSendNotification);
    if (! reference.isPopupActive())
        for (size_t i = 0; i < referenceIds.size(); ++i)
            if (referenceIds[i] == d.references[0])
                reference.setSelectedItemIndex ((int) i, juce::dontSendNotification);
    repaint();
}

void RemotePanel::setMode (int newMode)
{
    mode = newMode;
    send (bus::remoteSpaceMode);
}

void RemotePanel::send (std::uint32_t mask)
{
    bus::RemoteCommand cmd;
    cmd.mask = mask;
    cmd.amount = (float) amount.getValue() / 100.0f;
    cmd.spaceAmount = (float) space.getValue() / 100.0f;
    cmd.link = link.getToggleState() ? 1u : 0u;
    cmd.spaceMode = (std::uint32_t) mode;
    const int idx = reference.getSelectedItemIndex();
    cmd.referenceId = idx >= 0 && idx < (int) referenceIds.size() ? referenceIds[(size_t) idx] : target.data.references[0];
    processor.sharedBus().sendRemote (target.id, cmd);
}

void RemotePanel::paint (juce::Graphics& g)
{
    auto r = getLocalBounds().toFloat();
    g.setColour (theme::raised);
    g.fillRoundedRectangle (r, 12.0f);
    g.setColour (theme::borderLight);
    g.drawRoundedRectangle (r.reduced (0.5f), 12.0f, 1.0f);

    auto a = getLocalBounds().reduced (18, 14);
    auto head = a.removeFromTop (26);
    g.setColour (target.colour);
    g.fillEllipse ((float) head.getX(), (float) head.getCentreY() - 5.0f, 10.0f, 10.0f);
    g.setColour (theme::text);
    g.setFont (theme::font (15.0f, true));
    g.drawText (u8 ("Дорожка: ") + target.name, head.withTrimmedLeft (18), juce::Justification::centredLeft);

    g.setColour (theme::textDim);
    g.setFont (theme::font (11.5f));
    g.drawText (u8 ("Изменения уходят прямо в плагин дорожки: её ручки двигаются, DAW может записать автоматизацию."),
                a.removeFromTop (18), juce::Justification::centredLeft);

    g.setColour (theme::text);
    g.setFont (theme::font (12.0f, true));
    const int x = a.getX();
    g.drawText ("Amount", x, 78, 120, 20, juce::Justification::centredLeft);
    g.drawText ("Space Amount", x, 114, 120, 20, juce::Justification::centredLeft);
    g.drawText (u8 ("Режим Space"), x, 150, 120, 20, juce::Justification::centredLeft);
    g.drawText (u8 ("Уступает"), x, 190, 120, 20, juce::Justification::centredLeft);
}

void RemotePanel::resized()
{
    auto a = getLocalBounds().reduced (18, 14);
    close.setBounds (getWidth() - 46, 12, 30, 26);
    const int x = a.getX() + 120, w = a.getWidth() - 120;
    amount.setBounds (x, 76, w - 80, 24);
    link.setBounds (x + w - 70, 76, 70, 24);
    space.setBounds (x, 112, w - 80, 24);
    const int third = (w - 12) / 3;
    modeSpread.setBounds (x, 148, third, 26);
    modePan.setBounds (x + third + 6, 148, third, 26);
    modeDecor.setBounds (x + 2 * (third + 6), 148, third, 26);
    reference.setBounds (x, 188, juce::jmin (260, w), 26);
}

//==============================================================================
HubEditor::HubEditor (HubProcessor& p)
    : processor (p), remote (p)
{
    setLookAndFeel (&lnf);

    for (auto* s : { &totalSlider, &spaceSlider })
    {
        addAndMakeVisible (*s);   // first, so the text box is created with our look
        s->setSliderStyle (juce::Slider::LinearHorizontal);
        s->setTextBoxStyle (juce::Slider::TextBoxRight, false, 64, 22);
        s->setDoubleClickReturnValue (true, 100.0);
        addAndMakeVisible (*s);
    }
    totalSlider.setColour (juce::Slider::trackColourId, theme::text);
    spaceSlider.setColour (juce::Slider::trackColourId, theme::space);
    spaceSlider.setColour (juce::Slider::textBoxTextColourId, theme::space);
    totalSlider.setTooltip (u8 ("Умножает Amount на всех треках"));
    spaceSlider.setTooltip (u8 ("Умножает Space Amount на всех треках"));
    totalAttach = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment> (processor.state(), hubParams::totalAmount, totalSlider);
    spaceAttach = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment> (processor.state(), hubParams::spaceAmount, spaceSlider);

    for (auto* b : { &compareA, &compareB })
    {
        styleSegment (*b, groupCompare);
        addAndMakeVisible (*b);
    }
    compareA.setTooltip (u8 ("A: вся обработка FreqFreaks выключена"));
    compareB.setTooltip (u8 ("B: обработка включена"));
    bypassAll.setClickingTogglesState (true);
    bypassAll.setColour (juce::TextButton::buttonOnColourId, theme::conflict.withAlpha (0.18f));
    bypassAll.setColour (juce::TextButton::textColourOnId, theme::conflict);
    bypassAll.setColour (juce::ComboBox::outlineColourId, theme::borderLight);
    bypassAll.setTooltip (u8 ("Выключить FreqFreaks на всех дорожках"));
    bypassAllAttach = std::make_unique<juce::AudioProcessorValueTreeState::ButtonAttachment> (processor.state(), hubParams::bypassAll, bypassAll);
    addAndMakeVisible (bypassAll);

    resetTrims.setButtonText (u8 ("Сбросить тримы"));
    resetTrims.setColour (juce::ComboBox::outlineColourId, theme::borderLight);
    resetTrims.setTooltip (u8 ("Вернуть всем дорожкам трим 100% и снять обход"));
    resetTrims.onClick = [this] { processor.resetTrims(); timerCallback(); };
    addAndMakeVisible (resetTrims);

    rowsView.setViewedComponent (&rowsHolder, false);
    rowsView.setScrollBarsShown (true, false);
    rowsView.setScrollBarThickness (8);
    rowsView.setScrollOnDragMode (juce::Viewport::ScrollOnDragMode::never);
    rowsView.getVerticalScrollBar().setColour (juce::ScrollBar::thumbColourId, theme::borderLight);
    rowsView.getVerticalScrollBar().setColour (juce::ScrollBar::trackColourId, theme::panelDeep);
    addAndMakeVisible (rowsView);

    compactButton.setButtonText (u8 ("Компактно"));
    compactButton.setClickingTogglesState (true);
    compactButton.setColour (juce::TextButton::buttonOnColourId, theme::space.withAlpha (0.14f));
    compactButton.setColour (juce::TextButton::textColourOnId, theme::space);
    compactButton.setColour (juce::ComboBox::outlineColourId, theme::borderLight);
    compactButton.setTooltip (u8 ("Строки в одну линию: больше дорожек на экране"));
    compactButton.onClick = [this] {
        compact = compactButton.getToggleState();
        for (auto& r : rows)
            r->setCompact (compact);
        layoutRows();
    };
    addAndMakeVisible (compactButton);

    addChildComponent (remote);
    remote.sendLookAndFeelChange();
    remote.onClose = [this] { remote.setVisible (false); };

    compareA.onClick = [this] { if (auto* prm = processor.state().getParameter (hubParams::compareA)) prm->setValueNotifyingHost (1.0f); };
    compareB.onClick = [this] { if (auto* prm = processor.state().getParameter (hubParams::compareA)) prm->setValueNotifyingHost (0.0f); };

    tabMap.setButtonText (u8 ("Карта микса"));
    tabMatrix.setButtonText (u8 ("Матрица"));
    tabStereo.setButtonText (u8 ("Стерео"));
    for (auto* b : { &tabMap, &tabMatrix, &tabStereo })
    {
        styleSegment (*b, groupTabs);
        addAndMakeVisible (*b);
    }
    tabMap.onClick    = [this] { setView (HubCanvas::View::map); };
    tabMatrix.onClick = [this] { setView (HubCanvas::View::matrix); };
    tabStereo.onClick = [this] { setView (HubCanvas::View::stereo); };
    tabMap.setToggleState (true, juce::dontSendNotification);

    addAndMakeVisible (canvas);
    addAndMakeVisible (issues);
    issues.onSelect = [this] (const HubIssue& is) {
        setView (HubCanvas::View::map);
        tabMap.setToggleState (true, juce::dontSendNotification);
        canvas.setFocus (is.trackIndex, is.bandFrom, is.bandTo);
    };

    setSize (kWidth, kHeight);
    timerCallback();
    startTimerHz (15);
}

HubEditor::~HubEditor()
{
    stopTimer();
    rows.clear();   // the rows live in rowsHolder: delete them while it still exists
    setLookAndFeel (nullptr);
}

void HubEditor::mouseDown (const juce::MouseEvent& e)
{
    // Logo: the window size menu.
    if (juce::Rectangle<int> (14, 14, 190, 40).contains (e.getPosition()))
        ScalingEditor::showMenuFor (*this);
}

void HubEditor::setView (HubCanvas::View v)
{
    canvas.setView (v);
}

std::vector<HubIssue> HubEditor::diagnose (const std::vector<HubTrack>& list) const
{
    const auto& layout = BandLayout::get();
    std::vector<HubIssue> out;

    for (size_t t = 0; t < list.size(); ++t)
    {
        const auto& tr = list[t];
        const auto& d = tr.data;
        if ((d.flags & bus::flagBypassed) != 0)
            continue;

        if (d.references[0] == 0)
            continue;

        if ((d.flags & bus::flagReferenceLost) != 0)
        {
            HubIssue is;
            is.trackIndex = (int) t;
            is.trackId = tr.id;
            is.key = juce::String::toHexString ((juce::int64) tr.id) + ":silent";
            is.title = tr.name + u8 (": референс молчит");
            is.hint = u8 ("Трек «") + tr.referenceName + u8 ("» не найден или не играет. Освобождение на паузе.");
            out.push_back (is);
            continue;
        }

        // Rules, in words:
        //  - Less than 2 dB missing is not a conflict anyone hears: say nothing.
        //  - Look at the price: how much the yielding track already gives in those bands.
        //    If it gives a lot (EQ at its Depth, or 6 dB and more), pushing harder would make it
        //    disappear: advise to ease off and separate the two by hand instead.
        //  - Below Bass Mono only EQ works: never suggest Space or Mono Safety there.
        //  - A track that gives 9 dB or more, even with nothing missing, is flagged: too much.
        auto runOf = [&] (auto&& isBad, int& outFrom, int& outTo) {
            int from = -1;
            outFrom = outTo = -1;
            for (int b = 0; b <= kNumBands; ++b)
            {
                const bool bad = b < kNumBands && isBad (b);
                if (bad && from < 0)
                    from = b;
                if (! bad && from >= 0)
                {
                    if (outFrom < 0 || b - 1 - from > outTo - outFrom)
                        outFrom = from, outTo = b - 1;
                    from = -1;
                }
            }
            return outFrom >= 0;
        };
        auto bandsText = [&] (int from, int to) {
            return hzText (layout.centreHz[(size_t) from]) + u8 ("–") + hzText (layout.centreHz[(size_t) to]);
        };

        int bestFrom = -1, bestTo = -1;
        const bool missing = runOf ([&] (int b) { return d.restDb[b] > 2.0f; }, bestFrom, bestTo);

        if (missing)
        {
            float worst = 0.0f, given = 0.0f, eqGiven = 0.0f;
            for (int b = bestFrom; b <= bestTo; ++b)
            {
                worst = juce::jmax (worst, d.restDb[b]);
                given = juce::jmax (given, d.eqDb[b] + d.spaceDb[b]);
                eqGiven = juce::jmax (eqGiven, d.eqDb[b]);
            }
            const bool atLimit = eqGiven >= d.eqDepthDb - 0.5f || given >= 6.0f;
            const bool belowBassMono = layout.centreHz[(size_t) bestTo] < d.bassMonoHz;
            const auto price = u8 ("«") + tr.name + u8 ("» сейчас уступает ") + dbText (given) + ". ";

            HubIssue is;
            is.trackIndex = (int) t;
            is.trackId = tr.id;
            is.key = juce::String::toHexString ((juce::int64) tr.id) + ":missing";
            is.worstDb = worst;
            is.bandFrom = bestFrom;
            is.bandTo = bestTo;
            is.severe = worst > 4.0f || atLimit;
            is.title = tr.referenceName + u8 (" тонет под «") + tr.name + u8 ("» на ") + bandsText (bestFrom, bestTo);
            if (atLimit)
                is.hint = price + u8 ("Давить сильнее не стоит — «") + tr.name + u8 ("» потеряется в этих частотах. "
                          "Лучше ослабь Amount на нём и разведи их вручную: дай каждому свою частоту "
                          "или реши, кто здесь главный. Не хватает ещё ") + dbText (worst) + ".";
            else if ((d.flags & bus::flagLinkOff) != 0)
                is.hint = price + u8 ("Не хватает ") + dbText (worst) + u8 (". На треке выключен Link — включи, и EQ доберёт остаток.");
            else if (belowBassMono)
                is.hint = price + u8 ("Не хватает ") + dbText (worst) + u8 (". Ниже Bass Mono работает только EQ: можно чуть поднять "
                          "Amount или EQ Depth на «") + tr.name + u8 ("».");
            else
                is.hint = price + u8 ("Не хватает ") + dbText (worst) + u8 (". Можно чуть поднять Amount или EQ Depth на «")
                          + tr.name + u8 ("», либо Space Amount здесь.");
            out.push_back (is);
            continue;
        }

        // Nothing missing, but is the track giving away too much?
        int muchFrom = -1, muchTo = -1;
        if (runOf ([&] (int b) { return d.eqDb[b] + d.spaceDb[b] >= 9.0f; }, muchFrom, muchTo))
        {
            float given = 0.0f;
            for (int b = muchFrom; b <= muchTo; ++b)
                given = juce::jmax (given, d.eqDb[b] + d.spaceDb[b]);
            HubIssue is;
            is.trackIndex = (int) t;
            is.trackId = tr.id;
            is.key = juce::String::toHexString ((juce::int64) tr.id) + ":much";
            is.worstDb = given;
            is.bandFrom = muchFrom;
            is.bandTo = muchTo;
            is.severe = false;
            is.title = u8 ("«") + tr.name + u8 ("» уступает много на ") + bandsText (muchFrom, muchTo);
            is.hint = u8 ("Уже ") + dbText (given) + u8 (". Проверь на слух, не пропадает ли «") + tr.name
                      + u8 ("»; если да — ослабь Amount на нём или трим здесь.");
            out.push_back (is);
        }
    }

    std::stable_sort (out.begin(), out.end(), [] (const HubIssue& a, const HubIssue& b) { return a.severe && ! b.severe; });
    return out;
}

std::vector<HubIssue> HubEditor::holdIssues (std::vector<HubIssue> current)
{
    constexpr std::uint64_t holdMs = 10000, textEveryMs = 1000;
    const auto now = SharedBus::nowMs();

    for (auto& is : current)
    {
        auto it = held.find (is.key);
        if (it == held.end())
        {
            held[is.key] = { is, now, now, now, now };
            continue;
        }
        auto& h = it->second;
        const bool comingBack = now - h.lastSeenMs > 500;
        h.lastSeenMs = now;
        if (comingBack)
            h.firstSeenMs = juce::jmin (h.firstSeenMs, now);
        // Refresh the text at most once a second, keeping the worst value seen and the severity.
        const bool worse = is.worstDb > h.issue.worstDb + 1.0f || (is.severe && ! h.issue.severe);
        if (worse || now - h.textMs >= textEveryMs || comingBack)
        {
            // The worst value of the last 10 s: a new worst, or a fresh start every 10 s.
            const bool restart = comingBack || now - h.worstMs >= holdMs;
            const bool severe = restart ? is.severe : (h.issue.severe || is.severe);
            if (restart || is.worstDb >= h.issue.worstDb)
            {
                if (restart || is.worstDb > h.issue.worstDb)
                    h.worstMs = now;
                h.issue = is;
            }
            h.issue.severe = severe;
            h.textMs = now;
        }
    }

    std::vector<std::pair<std::uint64_t, HubIssue>> shown;
    for (auto it = held.begin(); it != held.end();)
    {
        auto& h = it->second;
        const bool trackAlive = std::any_of (tracks.begin(), tracks.end(), [&] (const HubTrack& t) { return t.id == h.issue.trackId; });
        if (now - h.lastSeenMs > holdMs || ! trackAlive)
        {
            it = held.erase (it);
            continue;
        }
        auto is = h.issue;
        is.ageSeconds = now - h.lastSeenMs > 500 ? (int) ((now - h.lastSeenMs + 500) / 1000) : 0;
        is.trackIndex = -1;
        for (size_t i = 0; i < tracks.size(); ++i)
            if (tracks[i].id == is.trackId)
                is.trackIndex = (int) i;
        shown.push_back ({ h.firstSeenMs, is });
        ++it;
    }
    // Red first, then in the order they appeared: the list does not jump around.
    std::stable_sort (shown.begin(), shown.end(), [] (const auto& a, const auto& b) {
        if (a.second.severe != b.second.severe)
            return a.second.severe;
        return a.first < b.first;
    });
    std::vector<HubIssue> out;
    for (auto& s : shown)
        out.push_back (std::move (s.second));
    return out;
}

void HubEditor::timerCallback()
{
    auto entries = processor.sharedBus().list();
    std::vector<HubTrack> list;
    for (const auto& e : entries)
    {
        if (e.stale)
            continue;
        HubTrack t;
        t.id = e.id;
        t.name = juce::String::fromUTF8 (e.data.name);
        t.colour = theme::trackColour (e.data.colourArgb, e.id);
        t.data = e.data;
        list.push_back (t);
    }
    std::sort (list.begin(), list.end(), [] (const HubTrack& a, const HubTrack& b) { return a.name < b.name; });
    for (auto& t : list)
    {
        t.referenceName = {};
        for (const auto& other : list)
            if (other.id == t.data.references[0])
                t.referenceName = other.name;
        if (t.data.references[0] != 0 && t.referenceName.isEmpty())
            t.referenceName = u8 ("нет сигнала");
    }

    tracks = list;
    shownIssues = holdIssues (diagnose (list));
    rebuildRows();
    if (remote.isVisible())
        remote.update (list);
    canvas.setData (list);
    issues.setIssues (shownIssues);

    const bool a = processor.state().getRawParameterValue (hubParams::compareA)->load() > 0.5f;
    compareA.setToggleState (a, juce::dontSendNotification);
    compareB.setToggleState (! a, juce::dontSendNotification);

    repaint (topBar);
    repaint (sidebar);
}

void HubEditor::openRemote (std::uint64_t trackId)
{
    for (const auto& tr : tracks)
        if (tr.id == trackId)
            remote.open (tr, tracks);
}

void HubEditor::rebuildRows()
{
    // Display order: tracks with a real problem first (they need attention), then by name.
    // Rows are kept per track id, so a row being dragged is never recreated.
    std::vector<bool> problem (tracks.size(), false);
    for (const auto& is : shownIssues)
        if (is.severe)
            for (size_t i = 0; i < tracks.size(); ++i)
                if (tracks[i].id == is.trackId)
                    problem[i] = true;

    std::vector<size_t> order (tracks.size());
    for (size_t i = 0; i < order.size(); ++i)
        order[i] = i;
    std::stable_sort (order.begin(), order.end(), [&] (size_t a, size_t b) { return problem[a] && ! problem[b]; });

    // Don't shuffle the list under the mouse: while a trim is dragged, only update values.
    if (juce::Desktop::getInstance().getMainMouseSource().isDragging() && rowsHolder.isMouseOverOrDragging (true))
    {
        for (auto& r : rows)
            for (size_t i = 0; i < tracks.size(); ++i)
                if (tracks[i].id == r->id())
                    r->update (tracks[i], problem[i]);
        return;
    }

    std::vector<std::unique_ptr<TrackRow>> next;
    bool changed = rows.size() != tracks.size();
    for (size_t k = 0; k < order.size(); ++k)
    {
        const auto& t = tracks[order[k]];
        auto found = std::find_if (rows.begin(), rows.end(), [&] (const auto& r) { return r != nullptr && r->id() == t.id; });
        if (found != rows.end())
        {
            changed = changed || (found - rows.begin()) != (std::ptrdiff_t) k;
            next.push_back (std::move (*found));
        }
        else
        {
            auto row = std::make_unique<TrackRow> (processor, t.id);
            row->onOpen = [this] (std::uint64_t id) { openRemote (id); };
            rowsHolder.addAndMakeVisible (*row);
            row->sendLookAndFeelChange();   // created before it had our look: rebuild its text boxes
            next.push_back (std::move (row));
            changed = true;
        }
        next.back()->setCompact (compact);
        next.back()->update (t, problem[order[k]]);
    }
    rows = std::move (next);   // rows of tracks that are gone are deleted here
    if (changed)
        layoutRows();
}

void HubEditor::layoutRows()
{
    const int h = TrackRow::heightFor (compact), gap = compact ? 4 : 6;
    const int width = rowsView.getWidth() - (rows.size() * (size_t) (h + gap) > (size_t) rowsView.getHeight() ? 10 : 0);
    int y = 0;
    for (auto& row : rows)
    {
        row->setBounds (0, y, juce::jmax (0, width), h);
        y += h + gap;
    }
    rowsHolder.setSize (juce::jmax (0, width), juce::jmax (0, y - gap));
}

void HubEditor::resized()
{
    auto r = getLocalBounds();
    topBar = r.removeFromTop (68);
    footer = r.removeFromBottom (28);

    totalSlider.setBounds (370, 22, 220, 24);
    spaceSlider.setBounds (720, 22, 220, 24);
    bypassAll.setBounds (972, 20, 72, 28);
    compareA.setBounds (1050, 20, 32, 28);
    compareB.setBounds (1082, 20, 32, 28);

    sidebar = r.removeFromLeft (236);
    {
        auto s = sidebar.reduced (14, 16);
        compactButton.setBounds (s.getRight() - 84, s.getY(), 84, 22);
        s.removeFromTop (30 + (processor.isDuplicate() ? 40 : 0));
        resetTrims.setBounds (s.removeFromBottom (28));
        s.removeFromBottom (30);
        rowsView.setBounds (s);
        layoutRows();
    }
    rightPanel = r.removeFromRight (330);
    issues.setBounds (rightPanel);

    centre = r.reduced (20, 16);
    auto tabs = centre.removeFromTop (34);
    tabMap.setBounds (tabs.removeFromLeft (120));
    tabs.removeFromLeft (6);
    tabMatrix.setBounds (tabs.removeFromLeft (100));
    tabs.removeFromLeft (6);
    tabStereo.setBounds (tabs.removeFromLeft (90));
    centre.removeFromTop (14);
    canvas.setBounds (centre);
    remote.setBounds (centre.withHeight (262).reduced (10, 10).translated (0, 40));
}

void HubEditor::paint (juce::Graphics& g)
{
    g.fillAll (theme::background);
    brand::drawFooter (g, footer, "FreqFreaks Hub v" + AboutPanel::version() + "  ·  GPLv3");

    // Top bar.
    g.setColour (theme::headerBg);
    g.fillRect (topBar);
    g.setColour (theme::border);
    g.drawHorizontalLine (topBar.getBottom() - 1, 0.0f, (float) getWidth());
    drawLogo (g, { 20.0f, 24.0f });
    g.setColour (theme::text);
    g.setFont (theme::font (18.0f, true));
    g.drawText ("FreqFreaks", 52, 22, 110, 24, juce::Justification::centredLeft);
    g.setColour (theme::space);
    g.fillRoundedRectangle (156.0f, 24.0f, 40.0f, 20.0f, 5.0f);
    g.setColour (theme::background);
    g.setFont (theme::font (11.0f, true));
    g.drawText ("HUB", 156, 24, 40, 20, juce::Justification::centred);

    g.setFont (theme::font (12.0f, true));
    g.setColour (theme::text);
    g.drawText ("Total Amount", 270, 22, 96, 24, juce::Justification::centredLeft);
    g.setColour (theme::space);
    g.drawText ("Space Amount", 620, 22, 96, 24, juce::Justification::centredLeft);

    // Mono meter: how much quieter the mix gets when folded to mono.
    const float lossDb = 10.0f * std::log10 (juce::jmax (1.0e-4f, processor.monoCompatibility()));
    const juce::Rectangle<float> meter (1128.0f, 38.0f, 132.0f, 6.0f);
    g.setColour (theme::textDim);
    g.setFont (theme::font (11.0f));
    g.drawText (u8 ("Потеря в моно"), 1128, 18, 90, 16, juce::Justification::centredLeft);
    g.setColour (theme::text);
    g.setFont (theme::mono (12.0f));
    g.drawText (dbText (-lossDb), 1200, 18, 60, 16, juce::Justification::centredRight);
    g.setColour (theme::track);
    g.fillRoundedRectangle (meter, 3.0f);
    g.setColour (lossDb < -3.5f ? theme::eq : theme::space);
    g.fillRoundedRectangle (meter.withWidth (meter.getWidth() * juce::jlimit (0.02f, 1.0f, 1.0f + lossDb / 6.0f)), 3.0f);

    // Sidebar: the tracks.
    g.setColour (theme::panelDeep);
    g.fillRect (sidebar);
    g.setColour (theme::border);
    g.drawVerticalLine (sidebar.getRight() - 1, (float) sidebar.getY(), (float) sidebar.getBottom());
    g.drawVerticalLine (rightPanel.getX(), (float) rightPanel.getY(), (float) rightPanel.getBottom());

    auto s = sidebar.reduced (14, 16);
    g.setColour (theme::text);
    g.setFont (theme::font (14.0f, true));
    g.drawText (u8 ("Треки"), s.removeFromTop (22), juce::Justification::centredLeft);
    s.removeFromTop (8);

    if (processor.isDuplicate())
    {
        g.setColour (theme::eq);
        g.setFont (theme::font (11.5f));
        g.drawFittedText (u8 ("В проекте уже есть другой Hub. Этот только показывает данные."),
                          s.removeFromTop (34), juce::Justification::topLeft, 2);
        s.removeFromTop (6);
    }

    int unresolved = 0;
    for (const auto& t : tracks)
        for (float v : t.data.restDb)
            if (v > 0.25f)
                ++unresolved;

    g.setColour (theme::textDim);
    g.setFont (theme::font (12.0f));
    auto footer = sidebar.reduced (14, 16).withTrimmedBottom (36).removeFromBottom (20);
    g.drawText (u8 ("Треков: ") + juce::String ((int) tracks.size()) + u8 (" · неразрешённых полос: ") + juce::String (unresolved),
                footer, juce::Justification::centredLeft);
}

} // namespace ff
