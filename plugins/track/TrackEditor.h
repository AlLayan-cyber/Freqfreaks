// FreqFreaks — frequency-unmasking plugin suite
// Copyright (C) 2026 FreqFreaks contributors
// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "TrackProcessor.h"
#include "TimingKnob.h"
#include "../common/BandViews.h"
#include "../common/LookAndFeel.h"

#include <vector>

namespace ff
{

/** Speaker icon next to the track name: click = Delta (hear only what is taken away),
    right click = choose what to hear. Lit while anything but the normal sound is on. */
class ListenButton : public juce::Component,
                     public juce::SettableTooltipClient
{
public:
    explicit ListenButton (TrackProcessor&);
    void paint (juce::Graphics&) override;
    void mouseDown (const juce::MouseEvent&) override;

private:
    void showMenu();
    TrackProcessor& processor;
    int lastDelta = 1;
};

/** The track colour dot: click to pick a colour, or go back to the DAW's. */
class ColourDot : public juce::Component,
                  public juce::SettableTooltipClient
{
public:
    explicit ColourDot (TrackProcessor&);
    void paint (juce::Graphics&) override;
    void mouseDown (const juce::MouseEvent&) override;

private:
    TrackProcessor& processor;
};

/** The Track window from the UI mockup: header with the reference picker,
    spectrum, release budget, the Amount / Link / Space Amount knobs, and an
    Expert drawer. Simple / Detail / Expert show progressively more.

    Drawn at a fixed base size; ScalingEditor scales it as a whole. */
class TrackEditor : public juce::Component,
                    private juce::Timer
{
public:
    static constexpr int kWidth = 1120;
    static constexpr int kHeight = 860;

    explicit TrackEditor (TrackProcessor&);
    ~TrackEditor() override;

    void paint (juce::Graphics&) override;
    void resized() override;
    void mouseDown (const juce::MouseEvent&) override;

private:
    struct ParamRow
    {
        juce::Label label;
        juce::Slider slider;
        std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> attachment;
    };

    void timerCallback() override;
    void refreshReferenceList();
    void setChoice (const char* paramId, int index);
    void syncChoiceButtons();
    void applyLevel (int level);
    void setupRow (ParamRow&, const char* paramId, const juce::String& text);

    void updateHelp();
    void paintHelp (juce::Graphics&);

    TrackProcessor& processor;
    LookAndFeel lnf;

    juce::Label trackLabel, yieldsLabel;
    ColourDot colourDot;
    ListenButton listenButton;
    juce::ComboBox referenceBox;
    std::vector<std::uint64_t> referenceIds;
    juce::String referenceSignature;

    juce::TextButton compareA { "A" }, compareB { "B" };
    juce::TextButton levelSimple { "Simple" }, levelDetail { "Detail" }, levelExpert { "Expert" };

    SpectrumView spectrum;
    TimingKnob timing;
    BudgetView budget;

    juce::Slider amountKnob, spaceKnob, speedSlider;
    juce::TextButton linkButton { "Link" };
    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> amountAttach, spaceAttach, speedAttach;
    std::unique_ptr<juce::AudioProcessorValueTreeState::ButtonAttachment> linkAttach;

    juce::TextButton modeSpread { "Spread" }, modePan { "Pan Away" }, modeDecor { "Decorrelate" };
    juce::TextButton sideAuto { "Auto" }, sideLeft { "L" }, sideRight { "R" };
    juce::Slider sharpKnob;   // small knob in the spectrum card's title row
    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> sharpAttach;

    juce::Slider outputSlider;
    juce::TextButton autoGainButton { "Auto" }, spaceCompButton { "Space comp" };
    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> outputAttach;
    std::unique_ptr<juce::AudioProcessorValueTreeState::ButtonAttachment> autoGainAttach, spaceCompAttach;
    float shownCompensationDb = 0.0f;

    ParamRow eqDepth, rangeLow, rangeHigh, bassMono, monoSafety, sensitivity;

    // Layout rectangles used by paint().
    juce::Rectangle<int> header, footer, spectrumCard, budgetCard, knobCard, drawer, amountArea, spaceArea;
    juce::Rectangle<int> eqCard, spaceCard, detectCard, helpArea;
    juce::String helpTitle, helpText;
    juce::Colour shownRefColour { theme::reference };
    juce::String shownRefName;
    int tick = 0;
    bool hubOnline = false, referenceLost = false;
    int trackCount = 0;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (TrackEditor)
};

} // namespace ff
