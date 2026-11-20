// FreqFreaks — frequency-unmasking plugin suite
// Copyright (C) 2026 FreqFreaks contributors
// SPDX-License-Identifier: GPL-3.0-or-later
#include "TrackEditor.h"

namespace ff
{

using theme::u8;

namespace
{
    constexpr int kWidth = 1120;
    constexpr int kHeight = 760;
    constexpr int groupCompare = 1001, groupLevel = 1002, groupMode = 1003, groupSide = 1004;

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

TrackEditor::TrackEditor (TrackProcessor& p)
    : AudioProcessorEditor (p), processor (p)
{
    setLookAndFeel (&lnf);
    auto& apvts = processor.state();

    trackLabel.setFont (theme::font (15.0f, true));
    yieldsLabel.setText (u8 ("Уступает:"), juce::dontSendNotification);
    yieldsLabel.setFont (theme::font (12.0f));
    yieldsLabel.setColour (juce::Label::textColourId, theme::textDim);
    yieldsLabel.setJustificationType (juce::Justification::centredRight);
    addAndMakeVisible (trackLabel);
    addAndMakeVisible (yieldsLabel);

    referenceBox.setTextWhenNothingSelected (u8 ("Выбрать трек…"));
    referenceBox.setTooltip (u8 ("Трек, которому этот трек уступает частоты"));
    referenceBox.onChange = [this] {
        const int index = referenceBox.getSelectedItemIndex();
        if (index >= 0 && index < (int) referenceIds.size())
            processor.setReference (referenceIds[(size_t) index], index == 0 ? juce::String() : referenceBox.getText());
    };
    addAndMakeVisible (referenceBox);

    // A = original (bypass), B = processed.
    for (auto* b : { &compareA, &compareB })
    {
        styleSegment (*b, groupCompare);
        addAndMakeVisible (*b);
    }
    compareA.onClick = [this] { if (auto* prm = processor.state().getParameter (trackParams::bypass)) prm->setValueNotifyingHost (1.0f); };
    compareB.onClick = [this] { if (auto* prm = processor.state().getParameter (trackParams::bypass)) prm->setValueNotifyingHost (0.0f); };

    for (auto* b : { &levelSimple, &levelDetail, &levelExpert })
    {
        styleSegment (*b, groupLevel);
        addAndMakeVisible (*b);
    }
    levelSimple.onClick = [this] { applyLevel (0); };
    levelDetail.onClick = [this] { applyLevel (1); };
    levelExpert.onClick = [this] { applyLevel (2); };

    addAndMakeVisible (spectrum);
    addAndMakeVisible (budget);

    for (auto* k : { &amountKnob, &spaceKnob })
    {
        k->setSliderStyle (juce::Slider::RotaryHorizontalVerticalDrag);
        k->setTextBoxStyle (juce::Slider::NoTextBox, false, 0, 0);
        k->setRotaryParameters (juce::MathConstants<float>::pi * 1.25f, juce::MathConstants<float>::pi * 2.75f, true);
        addAndMakeVisible (*k);
    }
    amountKnob.setColour (juce::Slider::rotarySliderFillColourId, theme::text);
    spaceKnob.setColour (juce::Slider::rotarySliderFillColourId, theme::space);
    amountKnob.setTooltip (u8 ("Сколько освобождать всего"));
    spaceKnob.setTooltip (u8 ("Какую часть берёт Space. 100% = выбор плагина"));
    amountAttach = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment> (apvts, trackParams::amount, amountKnob);
    spaceAttach = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment> (apvts, trackParams::space, spaceKnob);
    spaceKnob.setDoubleClickReturnValue (true, 100.0);
    amountKnob.setDoubleClickReturnValue (true, 70.0);

    linkButton.setClickingTogglesState (true);
    linkButton.setColour (juce::TextButton::buttonOnColourId, theme::space.withAlpha (0.14f));
    linkButton.setColour (juce::TextButton::textColourOnId, theme::space);
    linkButton.setColour (juce::ComboBox::outlineColourId, theme::borderLight);
    linkButton.setTooltip (u8 ("Link: EQ добирает то, что не сделал Space"));
    linkAttach = std::make_unique<juce::AudioProcessorValueTreeState::ButtonAttachment> (apvts, trackParams::link, linkButton);
    addAndMakeVisible (linkButton);

    speedSlider.setSliderStyle (juce::Slider::LinearHorizontal);
    speedSlider.setTextBoxStyle (juce::Slider::NoTextBox, false, 0, 0);
    speedAttach = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment> (apvts, trackParams::speed, speedSlider);
    addAndMakeVisible (speedSlider);

    styleChip (modeSpread, theme::space, groupMode);
    styleChip (modePan, theme::space, groupMode);
    styleChip (modeDecor, theme::space, groupMode);
    modeSpread.onClick = [this] { setChoice (trackParams::spaceMode, 0); };
    modePan.onClick    = [this] { setChoice (trackParams::spaceMode, 1); };
    modeDecor.onClick  = [this] { setChoice (trackParams::spaceMode, 2); };
    modeSpread.setTooltip (u8 ("Уводит энергию полосы из центра в стороны"));
    modePan.setTooltip (u8 ("Сдвигает полосу влево или вправо"));
    modeDecor.setTooltip (u8 ("Расширяет моно-источник фазовыми фильтрами"));

    styleChip (sideAuto, theme::space, groupSide);
    styleChip (sideLeft, theme::space, groupSide);
    styleChip (sideRight, theme::space, groupSide);
    sideAuto.onClick  = [this] { setChoice (trackParams::panSide, 0); };
    sideLeft.onClick  = [this] { setChoice (trackParams::panSide, 1); };
    sideRight.onClick = [this] { setChoice (trackParams::panSide, 2); };

    for (auto* b : { &modeSpread, &modePan, &modeDecor, &sideAuto, &sideLeft, &sideRight })
        addAndMakeVisible (*b);

    addAndMakeVisible (outputSlider);   // first, so the text box gets our look
    outputSlider.setSliderStyle (juce::Slider::LinearHorizontal);
    outputSlider.setTextBoxStyle (juce::Slider::TextBoxRight, false, 70, 20);
    outputSlider.setColour (juce::Slider::trackColourId, theme::text);
    outputSlider.setDoubleClickReturnValue (true, 0.0);
    outputSlider.setTooltip (u8 ("Ручная поправка громкости на выходе"));
    outputAttach = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment> (apvts, trackParams::output, outputSlider);

    for (auto* b : { &autoGainButton, &spaceCompButton })
    {
        b->setClickingTogglesState (true);
        b->setColour (juce::TextButton::buttonOnColourId, theme::space.withAlpha (0.14f));
        b->setColour (juce::TextButton::textColourOnId, theme::space);
        b->setColour (juce::ComboBox::outlineColourId, theme::borderLight);
        addAndMakeVisible (*b);
    }
    autoGainButton.setTooltip (u8 ("Вернуть громкость, которую трек потерял от освобождения (до +6 дБ). "
                                   "Делает сравнение A/B честным, но немного уменьшает эффект."));
    spaceCompButton.setTooltip (u8 ("Держать громкость постоянной, когда крутишь Space Amount"));
    autoGainAttach = std::make_unique<juce::AudioProcessorValueTreeState::ButtonAttachment> (apvts, trackParams::autoGain, autoGainButton);
    spaceCompAttach = std::make_unique<juce::AudioProcessorValueTreeState::ButtonAttachment> (apvts, trackParams::spaceComp, spaceCompButton);

    setupRow (eqDepth, trackParams::eqDepth, u8 ("Depth (макс.)"));
    setupRow (rangeLow, trackParams::rangeLow, u8 ("Range от"));
    setupRow (rangeHigh, trackParams::rangeHigh, u8 ("Range до"));
    setupRow (bassMono, trackParams::bassMono, u8 ("Bass Mono ниже"));
    setupRow (monoSafety, trackParams::monoSafety, u8 ("Mono Safety"));
    setupRow (sensitivity, trackParams::sensitivity, u8 ("Чувствительность"));
    eqDepth.slider.setColour (juce::Slider::trackColourId, theme::eq);
    rangeLow.slider.setColour (juce::Slider::trackColourId, theme::eq);
    rangeHigh.slider.setColour (juce::Slider::trackColourId, theme::eq);
    sensitivity.slider.setColour (juce::Slider::trackColourId, theme::textDim);

    setSize (kWidth, kHeight);
    applyLevel (processor.uiLevel());
    syncChoiceButtons();
    refreshReferenceList();
    timerCallback();
    startTimerHz (30);
}

TrackEditor::~TrackEditor()
{
    stopTimer();
    setLookAndFeel (nullptr);
}

void TrackEditor::setupRow (ParamRow& row, const char* paramId, const juce::String& text)
{
    addAndMakeVisible (row.label);
    addAndMakeVisible (row.slider);   // first, so the slider's text box is created with our look
    row.label.setText (text, juce::dontSendNotification);
    row.label.setFont (theme::font (12.0f));
    row.label.setColour (juce::Label::textColourId, theme::textDim);
    row.slider.setSliderStyle (juce::Slider::LinearHorizontal);
    row.slider.setTextBoxStyle (juce::Slider::TextBoxRight, false, 74, 20);
    row.attachment = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment> (processor.state(), paramId, row.slider);
}

void TrackEditor::setChoice (const char* paramId, int index)
{
    if (auto* prm = dynamic_cast<juce::AudioParameterChoice*> (processor.state().getParameter (paramId)))
    {
        prm->beginChangeGesture();
        prm->setValueNotifyingHost (prm->convertTo0to1 ((float) index));
        prm->endChangeGesture();
    }
    syncChoiceButtons();
}

void TrackEditor::syncChoiceButtons()
{
    auto& apvts = processor.state();
    const int mode = (int) apvts.getRawParameterValue (trackParams::spaceMode)->load();
    const int side = (int) apvts.getRawParameterValue (trackParams::panSide)->load();
    const bool bypassed = apvts.getRawParameterValue (trackParams::bypass)->load() > 0.5f;

    modeSpread.setToggleState (mode == 0, juce::dontSendNotification);
    modePan.setToggleState (mode == 1, juce::dontSendNotification);
    modeDecor.setToggleState (mode == 2, juce::dontSendNotification);
    sideAuto.setToggleState (side == 0, juce::dontSendNotification);
    sideLeft.setToggleState (side == 1, juce::dontSendNotification);
    sideRight.setToggleState (side == 2, juce::dontSendNotification);
    for (auto* b : { &sideAuto, &sideLeft, &sideRight })
        b->setEnabled (mode == 1);
    compareA.setToggleState (bypassed, juce::dontSendNotification);
    compareB.setToggleState (! bypassed, juce::dontSendNotification);
}

void TrackEditor::applyLevel (int level)
{
    processor.setUiLevel (level);
    levelSimple.setToggleState (level == 0, juce::dontSendNotification);
    levelDetail.setToggleState (level == 1, juce::dontSendNotification);
    levelExpert.setToggleState (level == 2, juce::dontSendNotification);

    spectrum.setVisible (level >= 1);
    for (auto* row : { &eqDepth, &rangeLow, &rangeHigh, &bassMono, &monoSafety, &sensitivity })
    {
        row->label.setVisible (level == 2);
        row->slider.setVisible (level == 2);
    }
    resized();
    repaint();
}

//==============================================================================
void TrackEditor::refreshReferenceList()
{
    const auto entries = processor.sharedBus().list();
    const auto selfId = processor.instanceId();
    const auto currentRef = processor.referenceId();

    std::vector<std::uint64_t> ids { 0 };
    juce::StringArray names { u8 ("— нет —") };
    juce::String signature;
    bool currentFound = currentRef == 0;
    trackCount = 0;

    for (const auto& e : entries)
    {
        if (e.stale)
            continue;
        ++trackCount;
        if (e.id == selfId)
            continue;
        ids.push_back (e.id);
        names.add (juce::String::fromUTF8 (e.data.name));
        signature << juce::String::toHexString ((juce::int64) e.id) << names[names.size() - 1];
        currentFound = currentFound || e.id == currentRef;
    }

    if (! currentFound)
    {
        // Keep the saved reference visible even while it is not playing.
        ids.push_back (currentRef);
        names.add (processor.referenceName() + u8 (" (нет сигнала)"));
        signature << "lost";
    }

    if (signature == referenceSignature && referenceBox.getNumItems() == (int) ids.size())
        return;
    if (referenceBox.isPopupActive())
        return;

    referenceSignature = signature;
    referenceIds = ids;
    referenceBox.clear (juce::dontSendNotification);
    for (int i = 0; i < names.size(); ++i)
        referenceBox.addItem (names[i], i + 1);

    for (size_t i = 0; i < ids.size(); ++i)
        if (ids[i] == currentRef)
            referenceBox.setSelectedItemIndex ((int) i, juce::dontSendNotification);
}

void TrackEditor::timerCallback()
{
    static int tick = 0;
    if (++tick % 15 == 0)
        refreshReferenceList();

    auto& bus = processor.sharedBus();
    bus::TrackData own, ref;
    bus.read (processor.instanceId(), own);

    const auto refId = processor.referenceId();
    bool stale = true;
    const bool haveRef = refId != 0 && bus.read (refId, ref, &stale) && ! stale;
    referenceLost = refId != 0 && ! haveRef;

    bus::HubData hub;
    hubOnline = bus.readHub (hub);

    const auto bassHz = processor.state().getRawParameterValue (trackParams::bassMono)->load();
    spectrum.setData (own, processor.trackColour(), haveRef ? &ref : nullptr,
                      haveRef ? theme::trackColour (ref.colourArgb, refId) : theme::reference,
                      haveRef ? juce::String::fromUTF8 (ref.name) : juce::String(), bassHz);
    budget.setData (own);
    shownCompensationDb = own.compensationDb;

    trackLabel.setText (processor.trackName(), juce::dontSendNotification);
    syncChoiceButtons();
    repaint (header);
    repaint (budgetCard.withHeight (34));
    repaint (knobCard);
}

//==============================================================================
void TrackEditor::resized()
{
    auto r = getLocalBounds();
    header = r.removeFromTop (64);

    // Header controls.
    trackLabel.setBounds (226, 17, 150, 30);
    yieldsLabel.setBounds (380, 17, 76, 30);
    referenceBox.setBounds (462, 17, 210, 30);
    compareA.setBounds (838, 18, 34, 28);
    compareB.setBounds (872, 18, 34, 28);
    levelSimple.setBounds (920, 18, 60, 28);
    levelDetail.setBounds (980, 18, 60, 28);
    levelExpert.setBounds (1040, 18, 64, 28);

    r.reduce (16, 16);
    const int level = processor.uiLevel();
    if (level == 2)
    {
        drawer = r.removeFromBottom (132);
        r.removeFromBottom (16);
    }
    else
    {
        drawer = {};
    }

    knobCard = r.removeFromRight (300);
    r.removeFromRight (16);

    if (level >= 1)
    {
        spectrumCard = r.removeFromTop (juce::jmin (300, r.getHeight() / 2 + 60));
        r.removeFromTop (16);
        spectrum.setBounds (spectrumCard.reduced (16, 14).withTrimmedTop (26));
    }
    else
    {
        spectrumCard = {};
    }
    budgetCard = r;
    budget.setBounds (budgetCard.reduced (16, 12).withTrimmedTop (32));

    // Knob card.
    auto k = knobCard.reduced (18);
    amountArea = juce::Rectangle<int> (k.getX() - 4, k.getY(), 112, 112);
    spaceArea = juce::Rectangle<int> (k.getRight() - 108, k.getY(), 112, 112);
    amountKnob.setBounds (amountArea);
    spaceKnob.setBounds (spaceArea);
    linkButton.setBounds (k.getCentreX() - 22, k.getY() + 41, 44, 30);

    int y = k.getY() + 186;
    speedSlider.setBounds (k.getX(), y + 18, k.getWidth(), 20);
    y += 70;
    const int third = (k.getWidth() - 12) / 3;
    modeSpread.setBounds (k.getX(), y + 20, third, 30);
    modePan.setBounds (k.getX() + third + 6, y + 20, third, 30);
    modeDecor.setBounds (k.getX() + 2 * (third + 6), y + 20, third, 30);
    y += 64;
    sideAuto.setBounds (k.getX(), y + 20, third, 30);
    sideLeft.setBounds (k.getX() + third + 6, y + 20, third, 30);
    sideRight.setBounds (k.getX() + 2 * (third + 6), y + 20, third, 30);
    y += 64;
    outputSlider.setBounds (k.getX(), y + 20, k.getWidth(), 22);
    const int half = (k.getWidth() - 6) / 2;
    autoGainButton.setBounds (k.getX(), y + 48, half, 28);
    spaceCompButton.setBounds (k.getX() + half + 6, y + 48, half, 28);

    // Expert drawer: three cards.
    if (! drawer.isEmpty())
    {
        const int w = (drawer.getWidth() - 32) / 3;
        eqCard = drawer.withWidth (w);
        spaceCard = eqCard.translated (w + 16, 0);
        detectCard = spaceCard.translated (w + 16, 0);

        auto place = [] (juce::Rectangle<int> card, std::initializer_list<ParamRow*> rows) {
            auto inner = card.reduced (14, 10).withTrimmedTop (26);
            for (auto* row : rows)
            {
                auto line = inner.removeFromTop (28);
                row->label.setBounds (line.removeFromLeft (118));
                row->slider.setBounds (line);
            }
        };
        place (eqCard, { &eqDepth, &rangeLow, &rangeHigh });
        place (spaceCard, { &bassMono, &monoSafety });
        place (detectCard, { &sensitivity });
    }
}

void TrackEditor::paint (juce::Graphics& g)
{
    g.fillAll (theme::background);

    // Header.
    g.setColour (theme::headerBg);
    g.fillRect (header);
    g.setColour (theme::border);
    g.drawHorizontalLine (header.getBottom() - 1, 0.0f, (float) getWidth());
    drawLogo (g, { 20.0f, 22.0f });
    g.setColour (theme::text);
    g.setFont (theme::font (18.0f, true));
    g.drawText ("FreqFreaks", 52, 20, 110, 24, juce::Justification::centredLeft);
    g.setColour (theme::borderLight);
    g.drawRoundedRectangle (154.5f, 22.5f, 50.0f, 20.0f, 5.0f, 1.0f);
    g.setColour (theme::textDim);
    g.setFont (theme::font (11.0f, true));
    g.drawText ("TRACK", 154, 22, 51, 20, juce::Justification::centred);

    g.setColour (processor.trackColour());
    g.fillEllipse (212.0f, 27.0f, 10.0f, 10.0f);

    g.setFont (theme::font (12.0f));
    g.setColour (hubOnline ? theme::space : theme::textDim);
    g.fillEllipse (690.0f, 28.0f, 8.0f, 8.0f);
    g.setColour (theme::textDim);
    g.drawText (hubOnline ? u8 ("Hub · треков: ") + juce::String (trackCount) : u8 ("Hub не найден"),
                704, 20, 128, 24, juce::Justification::centredLeft);

    // Spectrum card.
    if (! spectrumCard.isEmpty())
    {
        drawCard (g, spectrumCard.toFloat());
        auto t = spectrumCard.reduced (16, 12).removeFromTop (22);
        g.setColour (theme::text);
        g.setFont (theme::font (14.0f, true));
        g.drawText (u8 ("Спектр"), t.removeFromLeft (70), juce::Justification::centredLeft);
        g.setFont (theme::font (12.0f));
        auto legend = [&] (juce::Colour c, const juce::String& text, bool dashed) {
            auto item = t.removeFromLeft (juce::roundToInt (juce::GlyphArrangement::getStringWidth (theme::font (12.0f), text)) + 40);
            g.setColour (c);
            if (dashed)
                for (int i = 0; i < 2; ++i)
                    g.fillRect ((float) item.getX() + (float) i * 8.0f, (float) item.getCentreY() - 1.0f, 5.0f, 2.0f);
            else
                g.fillRoundedRectangle ((float) item.getX(), (float) item.getCentreY() - 1.5f, 14.0f, 3.0f, 1.5f);
            g.setColour (theme::textDim);
            g.drawText (text, item.withTrimmedLeft (20), juce::Justification::centredLeft);
        };
        legend (processor.trackColour(), u8 ("Mid после обработки"), false);
        legend (processor.trackColour().withAlpha (0.35f), u8 ("до"), false);
        if (processor.referenceId() != 0)
            legend (theme::reference, referenceLost ? u8 ("референс: нет сигнала") : u8 ("референс"), true);
    }

    // Budget card + legend.
    drawCard (g, budgetCard.toFloat());
    {
        auto t = budgetCard.reduced (16, 12).removeFromTop (22);
        g.setColour (theme::text);
        g.setFont (theme::font (14.0f, true));
        g.drawText (u8 ("Бюджет освобождения"), t.removeFromLeft (180), juce::Justification::centredLeft);

        const int sp = budget.spacePercent();
        auto swatch = [&] (juce::Colour c, const juce::String& text, int width) {
            auto item = t.removeFromLeft (width);
            g.setColour (c);
            g.fillRoundedRectangle ((float) item.getX(), (float) item.getCentreY() - 5.0f, 10.0f, 10.0f, 3.0f);
            g.setColour (theme::textDim);
            g.setFont (theme::font (12.0f));
            g.drawText (text, item.withTrimmedLeft (16), juce::Justification::centredLeft);
        };
        swatch (theme::space, "Space " + juce::String (sp) + "%", 96);
        swatch (theme::eq, "EQ " + juce::String (sp > 0 || budget.unresolvedBands() > 0 ? 100 - sp : 0) + "%", 80);
        swatch (budget.linkOn() ? theme::conflict : theme::idle,
                budget.linkOn() ? u8 ("не разрешено") : u8 ("не освобождено"), 130);

        juce::String status;
        juce::Colour statusColour = theme::space;
        if (processor.isHubBypassing())
            status = u8 ("Hub: сравнение A — обработка выключена");
        else if (processor.referenceId() == 0)
            status = u8 ("Референс не выбран"), statusColour = theme::textDim;
        else if (referenceLost)
            status = u8 ("Референс молчит или не найден"), statusColour = theme::eq;
        else if (! budget.linkOn())
            status = u8 ("Link выкл: модули независимы"), statusColour = theme::textDim;
        else if (budget.unresolvedBands() > 0)
            status = u8 ("Неразрешённых полос: ") + juce::String (budget.unresolvedBands()), statusColour = theme::conflict;
        else
            status = u8 ("Все конфликты разрешены");
        g.setColour (statusColour);
        g.setFont (theme::font (12.0f, true));
        g.drawText (status, t, juce::Justification::centredRight);
    }

    // Knob card.
    drawCard (g, knobCard.toFloat());
    {
        auto& apvts = processor.state();
        const float amount = apvts.getRawParameterValue (trackParams::amount)->load();
        const float space = apvts.getRawParameterValue (trackParams::space)->load();
        const float speed = apvts.getRawParameterValue (trackParams::speed)->load();
        const bool link = apvts.getRawParameterValue (trackParams::link)->load() > 0.5f;

        g.setFont (theme::mono (20.0f));
        g.setColour (theme::text);
        g.drawText (juce::String (amount, 0) + "%", amountArea, juce::Justification::centred);
        g.setColour (theme::space);
        g.drawText (juce::String (space, 0) + "%", spaceArea.translated (0, -6), juce::Justification::centred);
        g.setFont (theme::font (10.5f));
        g.setColour (theme::textDim);
        g.drawText (std::abs (space - 100.0f) < 0.5f ? u8 ("авто") : (space > 100.0f ? u8 ("шире") : u8 ("уже")),
                    spaceArea.translated (0, 14), juce::Justification::centred);

        auto caption = [&] (juce::Rectangle<int> area, const juce::String& title, const juce::String& sub) {
            g.setColour (theme::text);
            g.setFont (theme::font (13.0f, true));
            g.drawText (title, area.getX() - 10, area.getBottom() + 4, area.getWidth() + 20, 18, juce::Justification::centred);
            g.setColour (theme::textDim);
            g.setFont (theme::font (11.0f));
            g.drawText (sub, area.getX() - 10, area.getBottom() + 22, area.getWidth() + 20, 16, juce::Justification::centred);
        };
        caption (amountArea, "Amount", u8 ("сколько освобождать"));
        caption (spaceArea, "Space Amount", u8 ("100% = авто"));

        auto k = knobCard.reduced (18);
        g.setFont (theme::font (11.5f));
        g.setColour (link ? theme::space : theme::textDim);
        g.drawText (link ? u8 ("Link вкл: EQ добирает остаток Space") : u8 ("Link выкл: Space и EQ независимы"),
                    k.getX(), k.getY() + 160, k.getWidth(), 18, juce::Justification::centred);

        int y = k.getY() + 186;
        g.setColour (theme::text);
        g.setFont (theme::font (12.0f, true));
        g.drawText (u8 ("Скорость"), k.getX(), y, 100, 16, juce::Justification::centredLeft);
        g.setColour (theme::textDim);
        g.setFont (theme::font (12.0f));
        g.drawText (speed < 35.0f ? "Rider" : (speed > 65.0f ? "Duck" : u8 ("Смешанно")),
                    k.getRight() - 100, y, 100, 16, juce::Justification::centredRight);
        g.setFont (theme::font (11.0f));
        g.drawText (u8 ("Rider · фразы"), k.getX(), y + 40, 120, 14, juce::Justification::centredLeft);
        g.drawText (u8 ("Duck · быстро"), k.getRight() - 120, y + 40, 120, 14, juce::Justification::centredRight);

        y += 70;
        g.setColour (theme::text);
        g.setFont (theme::font (12.0f, true));
        g.drawText (u8 ("Режим Space"), k.getX(), y, 200, 16, juce::Justification::centredLeft);
        y += 64;
        g.drawText (u8 ("Сторона для Pan Away"), k.getX(), y, 200, 16, juce::Justification::centredLeft);

        y += 64;
        g.setColour (theme::text);
        g.drawText ("Output", k.getX(), y, 100, 16, juce::Justification::centredLeft);
        const auto& apvts2 = processor.state();
        const bool autoOn = apvts2.getRawParameterValue (trackParams::autoGain)->load() > 0.5f;
        const bool compOn = apvts2.getRawParameterValue (trackParams::spaceComp)->load() > 0.5f;
        juce::String compText;
        if (autoOn || compOn)
            compText = (autoOn ? "Auto " : u8 ("компенсация ")) + (shownCompensationDb > 0.05f ? "+" : "")
                       + juce::String (shownCompensationDb, 1) + " dB";
        g.setColour (theme::textDim);
        g.setFont (theme::font (11.5f));
        g.drawText (compText, k.getRight() - 170, y, 170, 16, juce::Justification::centredRight);
    }

    // Expert drawer.
    if (! drawer.isEmpty())
    {
        auto title = [&] (juce::Rectangle<int> card, juce::Colour accent, const juce::String& text) {
            drawCard (g, card.toFloat(), accent);
            g.setColour (accent == theme::textDim ? theme::text : accent);
            g.setFont (theme::font (13.0f, true));
            g.drawText (text, card.reduced (14, 10).removeFromTop (20), juce::Justification::centredLeft);
        };
        title (eqCard, theme::eq, "EQ");
        title (spaceCard, theme::space, "Space");
        title (detectCard, theme::textDim, u8 ("Обнаружение"));
        g.setColour (theme::textDim);
        g.setFont (theme::font (12.0f));
        auto info = detectCard.reduced (14, 10).withTrimmedTop (58);
        g.drawText (u8 ("Lookahead: 30 мс (задержка сообщается DAW)"), info.removeFromTop (20), juce::Justification::centredLeft);
        g.drawText (u8 ("Защита атак и формант — в версии v1"), info.removeFromTop (20), juce::Justification::centredLeft);
    }
}

} // namespace ff
