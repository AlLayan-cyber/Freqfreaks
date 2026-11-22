// FreqFreaks — frequency-unmasking plugin suite
// Copyright (C) 2026 FreqFreaks contributors
// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "TrackProcessor.h"

namespace ff
{

/** A small EQ-style window for the frequency settings in Expert.

    EQ kind: Range low / high (amber edges; outside is dimmed, nothing is touched there).
    Space kind: Bass Mono (teal edge; left of it Space does not work, the low end stays mono),
    with Range shown for context. The fixed bells are dots on the axis, lit where that module
    may work. Drag an edge, mouse wheel for fine steps, double-click for the default. */
class RangeView : public juce::Component,
                  public juce::SettableTooltipClient
{
public:
    enum class Kind { eq, space };
    RangeView (TrackProcessor&, Kind);

    void paint (juce::Graphics&) override;
    void mouseMove (const juce::MouseEvent&) override;
    void mouseExit (const juce::MouseEvent&) override;
    void mouseDown (const juce::MouseEvent&) override;
    void mouseDrag (const juce::MouseEvent&) override;
    void mouseUp (const juce::MouseEvent&) override;
    void mouseDoubleClick (const juce::MouseEvent&) override;
    void mouseWheelMove (const juce::MouseEvent&, const juce::MouseWheelDetails&) override;

private:
    enum Edge { none = -1, rangeLow = 0, rangeHigh = 1, bassMono = 2 };

    juce::Rectangle<float> plot() const;
    float xFor (float hz) const;
    float hzFor (float x) const;
    float value (Edge) const;
    void set (Edge, float hz);
    Edge edgeAt (float x) const;
    juce::RangedAudioParameter* param (Edge) const;

    TrackProcessor& processor;
    Kind kind;
    Edge hover = none, dragging = none;
};

} // namespace ff
