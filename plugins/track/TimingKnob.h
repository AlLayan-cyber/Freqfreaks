// FreqFreaks — frequency-unmasking plugin suite
// Copyright (C) 2026 FreqFreaks contributors
// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "TrackProcessor.h"

namespace ff
{

/** Release in the centre, Attack as a neon ring around it.

    Both start in "auto": they follow the Speed slider (Rider - Duck) exactly as
    before, drawn grey with the time in use. Dragging a part (or using the mouse
    wheel on it) switches that part to manual, starting from the time in use, so
    nothing jumps; the part then lights up. Double-click a part to return it to auto. */
class TimingKnob : public juce::Component,
                   public juce::SettableTooltipClient
{
public:
    explicit TimingKnob (TrackProcessor&);

    void paint (juce::Graphics&) override;
    void mouseMove (const juce::MouseEvent&) override;
    void mouseExit (const juce::MouseEvent&) override;
    void mouseDown (const juce::MouseEvent&) override;
    void mouseDrag (const juce::MouseEvent&) override;
    void mouseUp (const juce::MouseEvent&) override;
    void mouseDoubleClick (const juce::MouseEvent&) override;
    void mouseWheelMove (const juce::MouseEvent&, const juce::MouseWheelDetails&) override;

private:
    enum class Part { none, attack, release };

    struct Control
    {
        juce::RangedAudioParameter* value = nullptr;
        juce::RangedAudioParameter* manual = nullptr;
    };

    Part partAt (juce::Point<float>) const;
    Control& control (Part p)                   { return p == Part::attack ? attack : release; }
    bool isManual (Part) const;
    float msInUse (Part) const;
    void beginEdit (Part);
    void nudge (Part, float deltaNormalised);
    void endEdit();

    TrackProcessor& processor;
    Control attack, release;
    Part hover = Part::none, dragging = Part::none;
    float dragStart = 0.0f;
    int dragStartY = 0;
};

} // namespace ff
