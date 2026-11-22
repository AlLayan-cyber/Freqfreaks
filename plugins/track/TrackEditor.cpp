// FreqFreaks — frequency-unmasking plugin suite
// Copyright (C) 2026 FreqFreaks contributors
// SPDX-License-Identifier: GPL-3.0-or-later
#include "TrackEditor.h"
#include "../common/ScalingEditor.h"
#include "../common/Brand.h"

namespace ff
{

using theme::u8;

namespace
{
    constexpr int groupCompare = 1001, groupLevel = 1002, groupMode = 1003, groupSide = 1004;
    const juce::Rectangle<int> logoArea { 14, 12, 196, 40 };

    struct NamedColour { const char* name; juce::uint32 argb; };
    const NamedColour palette[] = {
        { "Красный", 0xffef4444 }, { "Оранжевый", 0xfffb923c }, { "Жёлтый", 0xfffde047 }, { "Лайм", 0xffa3e635 },
        { "Зелёный", 0xff34d399 }, { "Бирюзовый", 0xff2dd4bf }, { "Голубой", 0xff38bdf8 }, { "Синий", 0xff60a5fa },
        { "Фиолетовый", 0xffa78bfa }, { "Сиреневый", 0xffc4b5fd }, { "Розовый", 0xfff472b6 }, { "Серый", 0xff94a3b8 } };

    const char* listenNames[] = { "Обычный звук", "Δ всё — слышно только то, что убрано",
                                  "Δ только EQ", "Δ только Space", "Вход без обработки",
                                  "Референс — только его звук", "Трек + референс" };
    const char* listenShort[] = { "", "Δ", "Δ EQ", "Δ Sp", "IN", "REF", "+REF" };

    /** "Title\nBody" tooltips feed the description block. */
    juce::String help (const char* title, const char* body)
    {
        return theme::u8 (title) + "\n" + theme::u8 (body);
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
ListenButton::ListenButton (TrackProcessor& p) : processor (p)
{
    setTooltip (help ("Прослушка (Delta, референс)",
                      "Клик — слышно только то, что плагин убрал. Ещё клик — обычный звук. Правый клик — "
                      "Δ всё / только EQ / только Space / вход / референс / трек + референс. Референс слышно "
                      "и в соло этого трека: звук берётся с его FreqFreaks, до фейдера."));
    setMouseCursor (juce::MouseCursor::PointingHandCursor);
}

void ListenButton::paint (juce::Graphics& g)
{
    const int mode = processor.listenMode();
    const auto accent = mode == 0 ? theme::textDim
                      : mode == 4 ? theme::space
                      : mode >= 5 ? theme::reference
                                  : theme::eq;
    auto r = getLocalBounds().toFloat();

    if (mode != 0)
    {
        g.setColour (accent.withAlpha (0.16f));
        g.fillRoundedRectangle (r.reduced (0.5f), 6.0f);
        g.setColour (accent.withAlpha (0.7f));
        g.drawRoundedRectangle (r.reduced (0.5f), 6.0f, 1.0f);
    }
    else if (isMouseOver())
    {
        g.setColour (theme::raised);
        g.fillRoundedRectangle (r.reduced (0.5f), 6.0f);
    }

    // Speaker: body, cone and two waves.
    const float cx = 8.0f, cy = r.getCentreY();
    juce::Path speaker;
    speaker.startNewSubPath (cx - 3.0f, cy - 3.0f);
    speaker.lineTo (cx, cy - 3.0f);
    speaker.lineTo (cx + 5.0f, cy - 7.0f);
    speaker.lineTo (cx + 5.0f, cy + 7.0f);
    speaker.lineTo (cx, cy + 3.0f);
    speaker.lineTo (cx - 3.0f, cy + 3.0f);
    speaker.closeSubPath();
    g.setColour (accent);
    g.fillPath (speaker);
    for (int i = 0; i < 2; ++i)
    {
        juce::Path wave;
        const float rad = 4.0f + 4.0f * (float) i;
        wave.addCentredArc (cx + 6.0f, cy, rad, rad, 0.0f, 0.75f, 2.39f, true);
        g.strokePath (wave, juce::PathStrokeType (1.4f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
    }

    if (mode != 0)
    {
        g.setFont (theme::font (10.5f, true));
        g.drawText (theme::u8 (listenShort[mode]), r.withTrimmedLeft (22.0f).withTrimmedRight (2.0f), juce::Justification::centred);
    }
}

void ListenButton::mouseDown (const juce::MouseEvent& e)
{
    if (e.mods.isPopupMenu())
    {
        showMenu();
        return;
    }
    const int mode = processor.listenMode();
    if (mode == 0)
    {
        processor.setListenMode (lastDelta);
    }
    else
    {
        lastDelta = mode;
        processor.setListenMode (0);
    }
    repaint();
}

void ListenButton::showMenu()
{
    juce::PopupMenu menu;
    menu.addSectionHeader (theme::u8 ("Что слушать"));
    const int mode = processor.listenMode();
    const bool hasReference = processor.referenceId() != 0;
    for (int i = 0; i < TrackProcessor::numListenModes; ++i)
    {
        if (i == 1 || i == 4 || i == 5)
            menu.addSeparator();
        // The reference modes need a reference; its sound is taken where FreqFreaks sits on its
        // track (before its fader and pan), and works while that track is muted by a solo.
        const bool enabled = i < 5 || hasReference;
        menu.addItem (theme::u8 (listenNames[i]) + (i >= 5 && ! hasReference ? theme::u8 (" (выбери референс)") : juce::String()),
                      enabled, mode == i,
                      [safe = juce::Component::SafePointer<ListenButton> (this), i] {
                          if (safe == nullptr)
                              return;
                          if (i != 0)
                              safe->lastDelta = i;
                          safe->processor.setListenMode (i);
                          safe->repaint();
                      });
    }
    menu.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (this));
}

//==============================================================================
ColourDot::ColourDot (TrackProcessor& p) : processor (p)
{
    setTooltip (help ("Цвет трека", "Берётся из DAW. Клик — выбрать свой цвет или вернуть цвет из DAW. "
                                    "Этот цвет у линии референса на других треках и в Hub."));
    setMouseCursor (juce::MouseCursor::PointingHandCursor);
}

void ColourDot::paint (juce::Graphics& g)
{
    auto r = getLocalBounds().toFloat().withSizeKeepingCentre (12.0f, 12.0f);
    g.setColour (processor.trackColour());
    g.fillEllipse (r);
    if (isMouseOver())
    {
        g.setColour (theme::text.withAlpha (0.8f));
        g.drawEllipse (r.expanded (2.0f), 1.0f);
    }
}

void ColourDot::mouseDown (const juce::MouseEvent&)
{
    juce::PopupMenu menu;
    menu.addSectionHeader (theme::u8 ("Цвет трека"));
    const bool manual = processor.hasManualColour();
    const auto info = processor.hostInfo();
    auto safe = juce::Component::SafePointer<ColourDot> (this);
    menu.addItem (info.colourArgb != 0 ? theme::u8 ("Цвет из DAW") : theme::u8 ("Цвет из DAW (DAW его не передаёт)"),
                  true, ! manual, [safe] { if (safe != nullptr) { safe->processor.setManualColour ({}); safe->repaint(); } });
    menu.addSeparator();
    const auto current = processor.trackColour().getARGB();
    for (const auto& c : palette)
    {
        juce::PopupMenu::Item item (theme::u8 (c.name));
        item.colour = juce::Colour (c.argb);
        item.isTicked = manual && current == c.argb;
        const auto argb = c.argb;
        item.action = [safe, argb] { if (safe != nullptr) { safe->processor.setManualColour (juce::Colour (argb)); safe->repaint(); } };
        menu.addItem (item);
    }
    menu.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (this));
}

//==============================================================================
TrackEditor::TrackEditor (TrackProcessor& p)
    : processor (p), colourDot (p), listenButton (p), timing (p)
{
    setLookAndFeel (&lnf);
    auto& apvts = processor.state();

    addAndMakeVisible (colourDot);
    addAndMakeVisible (listenButton);

    trackLabel.setEditable (true, false, false);
    trackLabel.setTooltip (help ("Имя трека", "Берётся из DAW и меняется вместе с ним. Клик — задать своё имя "
                                              "(оно главнее). Сотри его — снова будет имя из DAW."));
    trackLabel.setColour (juce::Label::textWhenEditingColourId, theme::text);
    trackLabel.setColour (juce::Label::backgroundWhenEditingColourId, theme::panelDeep);
    trackLabel.setColour (juce::Label::outlineWhenEditingColourId, theme::space);
    trackLabel.onEditorShow = [this] {
        if (auto* ed = trackLabel.getCurrentTextEditor())
        {
            ed->setText (processor.trackName(), false);
            ed->selectAll();
        }
    };
    trackLabel.onTextChange = [this] {
        const auto typed = trackLabel.getText().trim();
        const auto daw = processor.hostInfo().name;
        // Typing the DAW's own name back means "follow the DAW again".
        processor.setManualName (typed == daw ? juce::String() : typed);
        trackLabel.setText (processor.trackName(), juce::dontSendNotification);
    };

    trackLabel.setFont (theme::font (15.0f, true));
    yieldsLabel.setText (u8 ("Уступает:"), juce::dontSendNotification);
    yieldsLabel.setFont (theme::font (12.0f));
    yieldsLabel.setColour (juce::Label::textColourId, theme::textDim);
    yieldsLabel.setJustificationType (juce::Justification::centredRight);
    addAndMakeVisible (trackLabel);
    addAndMakeVisible (yieldsLabel);

    referenceBox.setTextWhenNothingSelected (u8 ("Выбрать трек…"));
    referenceBox.setTooltip (help ("Уступает (референс)", "Трек, которому этот трек освобождает частоты. "
                                                          "Его спектр — пунктир в цвет его дорожки."));
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
    compareA.setTooltip (help ("A / B", "A — трек без обработки (с той же задержкой), B — с обработкой."));
    compareB.setTooltip (compareA.getTooltip());
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
    levelSimple.setTooltip (help ("Simple", "Только главное: Amount, Space Amount и бюджет освобождения."));
    levelDetail.setTooltip (help ("Detail", "Плюс спектр с волной подавления, Атака/Релиз и Sharp."));
    levelExpert.setTooltip (help ("Expert", "Все настройки: EQ Depth, Range, Bass Mono, Mono Safety, Чувствительность."));

    sharpKnob.setSliderStyle (juce::Slider::RotaryHorizontalVerticalDrag);
    sharpKnob.setTextBoxStyle (juce::Slider::NoTextBox, false, 0, 0);
    sharpKnob.setRotaryParameters (juce::MathConstants<float>::pi * 1.25f, juce::MathConstants<float>::pi * 2.75f, true);
    sharpKnob.setColour (juce::Slider::rotarySliderFillColourId, theme::eq);
    sharpKnob.setDoubleClickReturnValue (true, 0.0);
    sharpKnob.setTooltip (help ("Sharp", "Насколько узко вырезать. 0% — как раньше: 16 широких полос, музыкально и незаметно. "
                                         "Вверх — полосы уже (до 1/3 октавы, 32 полосы): точнее, только конфликтные места, "
                                         "но может звучать резче. Двойной клик — 0%."));
    addAndMakeVisible (sharpKnob);
    sharpAttach = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment> (apvts, trackParams::sharp, sharpKnob);

    addAndMakeVisible (spectrum);
    addAndMakeVisible (timing);   // over the spectrum
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
    amountKnob.setTooltip (help ("Amount", "Сколько места освобождать для референса всего. Двойной клик — 70%."));
    spaceKnob.setTooltip (help ("Space Amount", "Какую часть берёт Space (уводит в стороны, не теряя энергию). "
                                                "100% — выбор плагина, меньше — больше работает EQ."));
    amountAttach = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment> (apvts, trackParams::amount, amountKnob);
    spaceAttach = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment> (apvts, trackParams::space, spaceKnob);
    spaceKnob.setDoubleClickReturnValue (true, 100.0);
    amountKnob.setDoubleClickReturnValue (true, 70.0);

    linkButton.setClickingTogglesState (true);
    linkButton.setColour (juce::TextButton::buttonOnColourId, theme::space.withAlpha (0.14f));
    linkButton.setColour (juce::TextButton::textColourOnId, theme::space);
    linkButton.setColour (juce::ComboBox::outlineColourId, theme::borderLight);
    linkButton.setTooltip (help ("Link", "EQ добирает то, что не успел Space. Выкл — Space и EQ работают независимо."));
    linkAttach = std::make_unique<juce::AudioProcessorValueTreeState::ButtonAttachment> (apvts, trackParams::link, linkButton);
    addAndMakeVisible (linkButton);

    speedSlider.setSliderStyle (juce::Slider::LinearHorizontal);
    speedSlider.setTextBoxStyle (juce::Slider::NoTextBox, false, 0, 0);
    speedSlider.setTooltip (help ("Скорость", "Rider — плавно, по фразам. Duck — быстро, по нотам. "
                                              "Атака и релиз в режиме «авто» следуют за ней."));
    speedAttach = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment> (apvts, trackParams::speed, speedSlider);
    addAndMakeVisible (speedSlider);

    styleChip (modeSpread, theme::space, groupMode);
    styleChip (modePan, theme::space, groupMode);
    styleChip (modeDecor, theme::space, groupMode);
    modeSpread.onClick = [this] { setChoice (trackParams::spaceMode, 0); };
    modePan.onClick    = [this] { setChoice (trackParams::spaceMode, 1); };
    modeDecor.onClick  = [this] { setChoice (trackParams::spaceMode, 2); };
    modeSpread.setTooltip (help ("Режим Space: Spread", "Уводит энергию полосы из центра в стороны."));
    modePan.setTooltip (help ("Режим Space: Pan Away", "Сдвигает полосу влево или вправо от референса."));
    modeDecor.setTooltip (help ("Режим Space: Decorrelate", "Расширяет моно-источник фазовыми фильтрами."));

    styleChip (sideAuto, theme::space, groupSide);
    styleChip (sideLeft, theme::space, groupSide);
    styleChip (sideRight, theme::space, groupSide);
    sideAuto.onClick  = [this] { setChoice (trackParams::panSide, 0); };
    sideLeft.onClick  = [this] { setChoice (trackParams::panSide, 1); };
    sideRight.onClick = [this] { setChoice (trackParams::panSide, 2); };

    for (auto* b : { &sideAuto, &sideLeft, &sideRight })
        b->setTooltip (help ("Сторона для Pan Away", "Куда сдвигать полосы в режиме Pan Away. Auto — по треку."));
    for (auto* b : { &modeSpread, &modePan, &modeDecor, &sideAuto, &sideLeft, &sideRight })
        addAndMakeVisible (*b);

    addAndMakeVisible (outputSlider);   // first, so the text box gets our look
    outputSlider.setSliderStyle (juce::Slider::LinearHorizontal);
    outputSlider.setTextBoxStyle (juce::Slider::TextBoxRight, false, 70, 20);
    outputSlider.setColour (juce::Slider::trackColourId, theme::text);
    outputSlider.setDoubleClickReturnValue (true, 0.0);
    outputSlider.setTooltip (help ("Output", "Ручная поправка громкости на выходе, ±12 дБ. Двойной клик — 0."));
    outputAttach = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment> (apvts, trackParams::output, outputSlider);

    for (auto* b : { &autoGainButton, &spaceCompButton })
    {
        b->setClickingTogglesState (true);
        b->setColour (juce::TextButton::buttonOnColourId, theme::space.withAlpha (0.14f));
        b->setColour (juce::TextButton::textColourOnId, theme::space);
        b->setColour (juce::ComboBox::outlineColourId, theme::borderLight);
        addAndMakeVisible (*b);
    }
    autoGainButton.setTooltip (help ("Auto", "Вернуть громкость, которую трек потерял от освобождения (до +6 дБ). "
                                             "Сравнение A/B честнее, но эффект чуть меньше."));
    spaceCompButton.setTooltip (help ("Space comp", "Держать громкость постоянной, когда крутишь Space Amount."));
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
    auto rowHelp = [] (ParamRow& row, const juce::String& text) { row.label.setTooltip (text); row.slider.setTooltip (text); };
    rowHelp (eqDepth, help ("EQ Depth", "Самый глубокий вырез, который может сделать EQ."));
    rowHelp (rangeLow, help ("Range от", "Ниже этой частоты плагин ничего не трогает."));
    rowHelp (rangeHigh, help ("Range до", "Выше этой частоты плагин ничего не трогает."));
    rowHelp (bassMono, help ("Bass Mono", "Ниже этой частоты Space не работает, низ остаётся в моно. "
                                          "Там освобождает только EQ. Серая зона слева на спектре."));
    rowHelp (monoSafety, help ("Mono Safety", "Сколько дБ Space может увести из центра: столько трек потеряет при сведении в моно."));
    rowHelp (sensitivity, help ("Чувствительность", "Насколько сильно референс должен перекрывать трек, чтобы плагин начал уступать."));
    spectrum.setTooltip (help ("Спектр", "Заливка — трек до, линия — после обработки, пунктир — референс. Сверху висит волна "
                                         "подавления: бирюзовое — Space, янтарное — EQ, красное — не хватило."));
    budget.setTooltip (help ("Бюджет освобождения", "Сколько освобождено в каждой полосе: Space, EQ и сколько не хватило."));

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
    timing.setVisible (level >= 1);
    sharpKnob.setVisible (level >= 1);
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
    if (haveRef)
    {
        shownRefColour = theme::trackColour (ref.colourArgb, refId);
        shownRefName = juce::String::fromUTF8 (ref.name);
    }
    else if (refId == 0)
    {
        shownRefName = {};
    }
    spectrum.setData (own, processor.trackColour(), haveRef ? &ref : nullptr, shownRefColour,
                      shownRefName, bassHz, &processor.bellBank ((int) own.sharpStep));
    budget.setData (own);
    shownCompensationDb = own.compensationDb;
    timing.repaint();

    if (! trackLabel.isBeingEdited())
        trackLabel.setText (processor.trackName(), juce::dontSendNotification);
    colourDot.repaint();
    listenButton.repaint();
    syncChoiceButtons();
    updateHelp();
    repaint (header);
    repaint (spectrumCard.withHeight (40));
    repaint (budgetCard.withHeight (34));
    repaint (knobCard);
}

//==============================================================================
void TrackEditor::resized()
{
    auto r = getLocalBounds();
    header = r.removeFromTop (64);
    footer = r.removeFromBottom (28);

    // Header controls.
    colourDot.setBounds (206, 22, 20, 20);
    trackLabel.setBounds (226, 17, 128, 30);
    listenButton.setBounds (356, 18, 46, 28);
    yieldsLabel.setBounds (402, 17, 56, 30);
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
        // Attack / Release sits in the top-left of the graph, under the Bass Mono label,
        // where the wave and the spectrum are usually quiet.
        timing.setBounds (spectrum.getX() + 10, spectrum.getY() + 26, 96, 96);

        // Sharp: a small knob at the right end of the card's title row.
        auto row = spectrumCard.reduced (16, 12).removeFromTop (22);
        sharpKnob.setBounds (juce::Rectangle<int> (34, 34).withCentre ({ row.getRight() - 17, row.getCentreY() + 1 }));
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

    // Description block under Output, to the bottom of the card.
    helpArea = juce::Rectangle<int> (k.getX(), y + 90, k.getWidth(), juce::jmin (150, k.getBottom() - (y + 90)));

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
    brand::drawFooter (g, footer, "FreqFreaks Track");

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


    g.setFont (theme::font (12.0f));
    g.setColour (hubOnline ? theme::space : theme::textDim);
    g.fillEllipse (690.0f, 28.0f, 8.0f, 8.0f);
    g.setColour (theme::textDim);
    juce::String hubText = hubOnline ? u8 ("Hub · треков: ") + juce::String (trackCount) : u8 ("Hub не найден");
    if (hubOnline && processor.isHubBypassing())
        hubText = u8 ("Hub: обход");
    else if (hubOnline && std::abs (processor.hubTrimValue() - 1.0f) > 0.005f)
        hubText = u8 ("Hub: ×") + juce::String (processor.hubTrimValue(), 2);
    g.setColour (hubOnline && (processor.isHubBypassing() || std::abs (processor.hubTrimValue() - 1.0f) > 0.005f)
                     ? theme::eq : theme::textDim);
    g.drawText (hubText, 704, 20, 128, 24, juce::Justification::centredLeft);

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
        legend (processor.trackColour(), u8 ("после обработки"), false);
        legend (processor.trackColour().withAlpha (0.35f), u8 ("до"), false);
        if (processor.referenceId() != 0)
            legend (shownRefColour, referenceLost ? u8 ("референс: нет сигнала")
                                                  : u8 ("референс: ") + (shownRefName.isNotEmpty() ? shownRefName : processor.referenceName()), true);

        g.setColour (theme::textDim);
        g.setFont (theme::font (11.5f));
        // "Sharp 40 % · Q 2.8", or "Sharp 0 % · как раньше".
        const float sharp = processor.state().getRawParameterValue (trackParams::sharp)->load();
        const int step = sharpStepFor (sharp / 100.0f);
        const juce::String sharpText = "Sharp " + juce::String (juce::roundToInt (sharp)) + " %  "
            + (step == 0 ? u8 ("· как раньше") : u8 ("· Q ") + juce::String (processor.bellBank (step).q (0), 1)
                                                  + u8 (" · полос ") + juce::String (bellsForStep (step)));
        g.setColour (step == 0 ? theme::textDim : theme::eq);
        g.drawText (sharpText, sharpKnob.getX() - 214, spectrumCard.getY() + 12, 208, 22, juce::Justification::centredRight);
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
        const int listen = processor.listenMode();
        if (listen >= 5 && ! processor.isReferenceAudioLive())
            status = u8 ("Звук референса не приходит"), statusColour = theme::eq;
        else if (listen == 5)
            status = u8 ("Слушаешь референс"), statusColour = theme::reference;
        else if (listen == 6)
            status = u8 ("Слушаешь трек + референс"), statusColour = theme::reference;
        else if (listen == 4)
            status = u8 ("Слушаешь вход без обработки");
        else if (listen != 0)
            status = u8 ("Delta: слышно только убранное"), statusColour = theme::eq;
        else if (processor.isHubBypassing())
            status = u8 ("Hub: обход — обработка выключена");
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
        juce::ignoreUnused (link);

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

        paintHelp (g);
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
        // What the DAW told us about the track: shows at once whether the host sends a name.
        const auto host = processor.hostInfo();
        if (host.calls == 0 && host.name.isEmpty() && ! host.presonus)
        {
            g.setColour (theme::eq);
            g.drawText (u8 ("DAW не передаёт имя трека"), info.removeFromTop (20), juce::Justification::centredLeft);
            g.setColour (theme::textDim);
            g.drawText (u8 ("Кликни по имени вверху, чтобы задать своё"), info.removeFromTop (20), juce::Justification::centredLeft);
        }
        else
        {
            g.drawText ((host.presonus ? u8 ("Имя от Studio One: ") : u8 ("Имя от DAW: ")) + (host.name.isNotEmpty() ? u8 ("«") + host.name + u8 ("»") : u8 ("не прислано")),
                        info.removeFromTop (20), juce::Justification::centredLeft);
            auto line = info.removeFromTop (20);
            g.drawText (u8 ("Цвет от DAW: ") + (host.colourArgb != 0 ? juce::String() : u8 ("не прислан")),
                        line, juce::Justification::centredLeft);
            if (host.colourArgb != 0)
            {
                const float x = (float) line.getX() + juce::GlyphArrangement::getStringWidth (theme::font (12.0f), u8 ("Цвет от DAW: ")) + 2.0f;
                g.setColour (juce::Colour (host.colourArgb));
                g.fillEllipse (x, (float) line.getCentreY() - 5.0f, 10.0f, 10.0f);
            }
        }
    }
}

//==============================================================================
void TrackEditor::mouseDown (const juce::MouseEvent& e)
{
    // Logo: the window size menu.
    if (logoArea.contains (e.getPosition()))
        ScalingEditor::showMenuFor (*this);
}

void TrackEditor::updateHelp()
{
    juce::String tip;
    auto* under = juce::Desktop::getInstance().getMainMouseSource().getComponentUnderMouse();
    if (under != nullptr && (under == this || isParentOf (under)))
    {
        for (auto* c = under; c != nullptr && c != this; c = c->getParentComponent())
            if (auto* client = dynamic_cast<juce::TooltipClient*> (c))
                if ((tip = client->getTooltip()).isNotEmpty())
                    break;
        if (tip.isEmpty() && logoArea.contains (getMouseXYRelative()))
            tip = help ("Размер окна", "Клик по логотипу — меню размеров 75–200%. "
                                       "Или тяни за правый нижний угол окна.");
    }

    juce::String title, text;
    if (tip.isNotEmpty())
    {
        title = tip.upToFirstOccurrenceOf ("\n", false, false);
        text = tip.fromFirstOccurrenceOf ("\n", false, false);
    }
    else
    {
        title = u8 ("Подсказка · наведи мышь на ручку");
        text = u8 ("Simple — Amount, Space Amount, бюджет.\n"
                   "Detail — плюс спектр, волна, Атака/Релиз, Sharp.\n"
                   "Expert — все настройки: EQ, Range, Bass Mono…\n"
                   "Link — EQ добирает то, что не успел Space.");
    }
    if (title != helpTitle || text != helpText)
    {
        helpTitle = title;
        helpText = text;
        repaint (helpArea);
    }
}

void TrackEditor::paintHelp (juce::Graphics& g)
{
    if (helpArea.getHeight() < 40)
        return;
    auto box = helpArea.toFloat();
    g.setColour (theme::panelDeep);
    g.fillRoundedRectangle (box, 8.0f);
    g.setColour (theme::border);
    g.drawRoundedRectangle (box.reduced (0.5f), 8.0f, 1.0f);

    auto inner = helpArea.reduced (10, 8);
    g.setColour (theme::text);
    g.setFont (theme::font (12.0f, true));
    g.drawText (helpTitle, inner.removeFromTop (18), juce::Justification::centredLeft);
    g.setColour (theme::textDim);
    g.setFont (theme::font (11.5f));
    g.drawFittedText (helpText, inner, juce::Justification::topLeft, juce::jmax (1, inner.getHeight() / 14), 0.9f);
}

} // namespace ff
