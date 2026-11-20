// FreqFreaks — frequency-unmasking plugin suite
// Copyright (C) 2026 FreqFreaks contributors
// SPDX-License-Identifier: GPL-3.0-or-later
#include "HubEditor.h"

#include <map>

namespace ff
{

using theme::u8;

namespace
{
    constexpr int kWidth = 1280;
    constexpr int kHeight = 800;
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
    if (selected >= (int) issues.size())
        selected = -1;
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
        auto box = area.removeFromTop (86);
        area.removeFromTop (8);
        if (box.getBottom() > getHeight() - 8)
            break;
        itemBounds.push_back (box);

        g.setColour ((int) i == selected ? theme::raised : theme::panel);
        g.fillRoundedRectangle (box.toFloat(), 10.0f);
        g.setColour ((int) i == selected ? theme::borderLight : theme::border);
        g.drawRoundedRectangle (box.toFloat().reduced (0.5f), 10.0f, 1.0f);

        auto inner = box.reduced (12, 10);
        auto dot = inner.removeFromLeft (14);
        g.setColour (is.severe ? theme::conflict : theme::eq);
        g.fillEllipse ((float) dot.getX(), (float) inner.getY() + 5.0f, 8.0f, 8.0f);

        g.setColour (theme::text);
        g.setFont (theme::font (13.0f, true));
        g.drawFittedText (is.title, inner.removeFromTop (36), juce::Justification::topLeft, 2);
        g.setColour (theme::textDim);
        g.setFont (theme::font (11.5f));
        g.drawFittedText (is.hint, inner, juce::Justification::topLeft, 3);
    }
}

void IssuePanel::mouseDown (const juce::MouseEvent& e)
{
    for (size_t i = 0; i < itemBounds.size(); ++i)
    {
        if (itemBounds[i].contains (e.getPosition()))
        {
            selected = (int) i;
            repaint();
            if (onSelect)
                onSelect (issues[i]);
            return;
        }
    }
}

//==============================================================================
HubEditor::HubEditor (HubProcessor& p)
    : AudioProcessorEditor (p), processor (p)
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
    setLookAndFeel (nullptr);
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
            is.title = tr.name + u8 (": референс молчит");
            is.hint = u8 ("Трек «") + tr.referenceName + u8 ("» не найден или не играет. Освобождение на паузе.");
            out.push_back (is);
            continue;
        }

        // Longest run of unresolved bands.
        int bestFrom = -1, bestTo = -1, from = -1;
        float worst = 0.0f;
        for (int b = 0; b <= kNumBands; ++b)
        {
            const bool bad = b < kNumBands && d.restDb[b] > 0.25f;
            if (bad && from < 0)
                from = b;
            if (! bad && from >= 0)
            {
                if (bestFrom < 0 || b - 1 - from > bestTo - bestFrom)
                    bestFrom = from, bestTo = b - 1;
                from = -1;
            }
            if (bad)
                worst = juce::jmax (worst, d.restDb[b]);
        }

        if (bestFrom >= 0)
        {
            HubIssue is;
            is.trackIndex = (int) t;
            is.bandFrom = bestFrom;
            is.bandTo = bestTo;
            is.severe = worst > 2.0f;
            is.title = tr.referenceName + u8 (" тонет под «") + tr.name + u8 ("» на ")
                       + hzText (layout.centreHz[(size_t) bestFrom]) + u8 ("–") + hzText (layout.centreHz[(size_t) bestTo]);
            if ((d.flags & bus::flagLinkOff) != 0)
                is.hint = u8 ("Не хватает до ") + dbText (worst) + u8 (". На треке выключен Link — включи, и EQ доберёт остаток.");
            else
                is.hint = u8 ("Не хватает до ") + dbText (worst) + u8 (". Подними EQ Depth или Mono Safety на треке, либо Space Amount здесь.");
            out.push_back (is);
        }
    }

    std::stable_sort (out.begin(), out.end(), [] (const HubIssue& a, const HubIssue& b) { return a.severe && ! b.severe; });
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
    canvas.setData (list);
    issues.setIssues (diagnose (list));

    const bool a = processor.state().getRawParameterValue (hubParams::compareA)->load() > 0.5f;
    compareA.setToggleState (a, juce::dontSendNotification);
    compareB.setToggleState (! a, juce::dontSendNotification);

    repaint (topBar);
    repaint (sidebar);
}

void HubEditor::resized()
{
    auto r = getLocalBounds();
    topBar = r.removeFromTop (68);

    totalSlider.setBounds (370, 22, 220, 24);
    spaceSlider.setBounds (720, 22, 220, 24);
    compareA.setBounds (980, 20, 34, 28);
    compareB.setBounds (1014, 20, 34, 28);

    sidebar = r.removeFromLeft (236);
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
}

void HubEditor::paint (juce::Graphics& g)
{
    g.fillAll (theme::background);

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
    const juce::Rectangle<float> meter (1090.0f, 38.0f, 160.0f, 6.0f);
    g.setColour (theme::textDim);
    g.setFont (theme::font (11.0f));
    g.drawText (u8 ("Потеря в моно"), 1090, 18, 100, 16, juce::Justification::centredLeft);
    g.setColour (theme::text);
    g.setFont (theme::mono (12.0f));
    g.drawText (dbText (-lossDb), 1180, 18, 70, 16, juce::Justification::centredRight);
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
    {
        auto row = s.removeFromTop (48);
        s.removeFromTop (6);
        if (row.getBottom() > sidebar.getBottom() - 40)
            break;

        g.setColour (theme::panel);
        g.fillRoundedRectangle (row.toFloat(), 9.0f);
        g.setColour (theme::border);
        g.drawRoundedRectangle (row.toFloat().reduced (0.5f), 9.0f, 1.0f);

        auto inner = row.reduced (10, 6);
        g.setColour (t.colour);
        g.fillEllipse ((float) inner.getX(), (float) inner.getCentreY() - 5.0f, 10.0f, 10.0f);
        inner.removeFromLeft (18);
        g.setColour (theme::text);
        g.setFont (theme::font (13.0f, true));
        g.drawText (t.name, inner.removeFromTop (18), juce::Justification::centredLeft, true);

        juce::String sub;
        const bool bypassed = (t.data.flags & bus::flagBypassed) != 0;
        if (bypassed)
            sub = u8 ("обход");
        else if (t.data.references[0] == 0)
            sub = u8 ("референс · ") + juce::String (juce::roundToInt (t.data.amount * 100.0f)) + "%";
        else
            sub = u8 ("→ ") + t.referenceName + u8 (" · ") + juce::String (juce::roundToInt (t.data.amount * 100.0f)) + "%";
        g.setColour (theme::textDim);
        g.setFont (theme::font (11.0f));
        g.drawText (sub, inner, juce::Justification::centredLeft, true);

        for (float v : t.data.restDb)
            if (v > 0.25f)
                ++unresolved;
    }

    g.setColour (theme::textDim);
    g.setFont (theme::font (12.0f));
    auto footer = sidebar.reduced (14, 12).removeFromBottom (20);
    g.drawText (u8 ("Треков: ") + juce::String ((int) tracks.size()) + u8 (" · неразрешённых полос: ") + juce::String (unresolved),
                footer, juce::Justification::centredLeft);
}

} // namespace ff
