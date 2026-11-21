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

/** The Track window from the UI mockup: header with the reference picker,
    spectrum, release budget, the Amount / Link / Space Amount knobs, and an
    Expert drawer. Simple / Detail / Expert show progressively more. */
class TrackEditor : public juce::AudioProcessorEditor,
                    private juce::Timer
{
public:
    explicit TrackEditor (TrackProcessor&);
    ~TrackEditor() override;

    void paint (juce::Graphics&) override;
    void resized() override;

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

    TrackProcessor& processor;
    LookAndFeel lnf;

    juce::Label trackLabel, yieldsLabel;
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

    juce::Slider outputSlider;
    juce::TextButton autoGainButton { "Auto" }, spaceCompButton { "Space comp" };
    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> outputAttach;
    std::unique_ptr<juce::AudioProcessorValueTreeState::ButtonAttachment> autoGainAttach, spaceCompAttach;
    float shownCompensationDb = 0.0f;

    ParamRow eqDepth, rangeLow, rangeHigh, bassMono, monoSafety, sensitivity;

    // Layout rectangles used by paint().
    juce::Rectangle<int> header, spectrumCard, budgetCard, knobCard, drawer, amountArea, spaceArea;
    juce::Rectangle<int> eqCard, spaceCard, detectCard;
    bool hubOnline = false, referenceLost = false;
    int trackCount = 0;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (TrackEditor)
};

} // namespace ff
