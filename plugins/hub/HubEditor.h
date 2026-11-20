// FreqFreaks — frequency-unmasking plugin suite
// Copyright (C) 2026 FreqFreaks contributors
// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "HubProcessor.h"
#include "../common/BandViews.h"
#include "../common/LookAndFeel.h"

#include <vector>

namespace ff
{

/** A track as the Hub shows it. */
struct HubTrack
{
    std::uint64_t id = 0;
    juce::String name, referenceName;
    juce::Colour colour;
    bus::TrackData data;
};

/** A rule-based diagnosis item (the AI assistant comes in a later stage). */
struct HubIssue
{
    int trackIndex = -1;
    int bandFrom = 0, bandTo = -1;
    bool severe = false;
    juce::String title, hint;
};

/** Centre area: mix map (tracks x bands), matrix (who yields to whom) or stereo map. */
class HubCanvas : public juce::Component
{
public:
    enum class View { map, matrix, stereo };

    void setView (View v)                                { view = v; repaint(); }
    void setData (std::vector<HubTrack> t)               { tracks = std::move (t); repaint(); }
    void setFocus (int track, int from, int to)          { focusTrack = track; focusFrom = from; focusTo = to; repaint(); }

    void paint (juce::Graphics&) override;

private:
    void paintMap (juce::Graphics&, juce::Rectangle<float>);
    void paintMatrix (juce::Graphics&, juce::Rectangle<float>);
    void paintStereo (juce::Graphics&, juce::Rectangle<float>);

    View view = View::map;
    std::vector<HubTrack> tracks;
    int focusTrack = -1, focusFrom = 0, focusTo = -1;
};

/** Right panel: problems found in the mix, click to show them on the map. */
class IssuePanel : public juce::Component
{
public:
    std::function<void (const HubIssue&)> onSelect;

    void setIssues (std::vector<HubIssue> list);
    void paint (juce::Graphics&) override;
    void mouseDown (const juce::MouseEvent&) override;

private:
    std::vector<HubIssue> issues;
    std::vector<juce::Rectangle<int>> itemBounds;
    int selected = -1;
};

class HubEditor : public juce::AudioProcessorEditor,
                  private juce::Timer
{
public:
    explicit HubEditor (HubProcessor&);
    ~HubEditor() override;

    void paint (juce::Graphics&) override;
    void resized() override;

private:
    void timerCallback() override;
    void setView (HubCanvas::View);
    std::vector<HubIssue> diagnose (const std::vector<HubTrack>&) const;

    HubProcessor& processor;
    LookAndFeel lnf;

    juce::Slider totalSlider, spaceSlider;
    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> totalAttach, spaceAttach;
    juce::TextButton compareA { "A" }, compareB { "B" };
    juce::TextButton tabMap, tabMatrix, tabStereo;

    HubCanvas canvas;
    IssuePanel issues;
    std::vector<HubTrack> tracks;

    juce::Rectangle<int> topBar, sidebar, centre, rightPanel;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (HubEditor)
};

} // namespace ff
