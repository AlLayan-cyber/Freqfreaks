// FreqFreaks — frequency-unmasking plugin suite
// Copyright (C) 2026 FreqFreaks contributors
// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "Theme.h"
#include "ff/BellBank.h"
#include "ff/SharedBus.h"

#include <array>

namespace ff
{

/** "30", "450", "1.1k", "18k" — compact frequency labels. */
juce::String shortHz (float hz);

/** Log-frequency mapping shared by every band view (20 Hz .. 20 kHz). */
float frequencyToX (float hz, juce::Rectangle<float> area) noexcept;

/** Frequency grid with labels along the bottom. */
void drawFrequencyGrid (juce::Graphics&, juce::Rectangle<float> area, bool labels);

/** Rounded card background used for every panel. */
void drawCard (juce::Graphics&, juce::Rectangle<float> area, juce::Colour topAccent = juce::Colours::transparentBlack);

//==============================================================================
/** Spectrum of the track before and (estimated) after processing, with the
    reference track's spectrum as a dashed ghost and the Bass Mono zone shaded.

    On top hangs the suppression wave: the exact response of the Space bells
    (teal) and of the EQ bells (amber), stacked down from a 0 dB line, with a red
    edge where release is missing and a slowly falling peak line. */
class SpectrumView : public juce::Component,
                     public juce::SettableTooltipClient
{
public:
    SpectrumView();

    /** bank: the track's bell layout (for the exact wave); may be null (no wave). */
    void setData (const bus::TrackData& own, juce::Colour ownColour,
                  const bus::TrackData* reference, juce::Colour referenceColour,
                  juce::String referenceName, float bassMonoHz, const BellBank* bank);

    void paint (juce::Graphics&) override;
    void mouseMove (const juce::MouseEvent&) override;
    void mouseExit (const juce::MouseEvent&) override;

private:
    juce::Path curve (const float* levelsDb, juce::Rectangle<float> area, bool closed) const;
    void paintWave (juce::Graphics&, juce::Rectangle<float> plot);
    juce::Rectangle<float> plotArea() const;

    static constexpr int kWavePoints = 240;
    static constexpr float kWaveRangeDb = 12.0f;   // full depth of the wave area
    std::array<float, kWavePoints> waveHz {}, spaceCut {}, eqCut {}, peakCut {};
    bool hasWave = false;
    float hoverX = -1.0f;

    bus::TrackData own, ref;
    bool hasReference = false;
    juce::Colour ownColour, refColour;
    juce::String refName;
    float bassMono = 150.0f;
};

//==============================================================================
/** The "release budget": one stacked bar per band — Space (teal), EQ (amber),
    and what is left (red when Link is on, grey when it is off). */
class BudgetView : public juce::Component,
                   public juce::SettableTooltipClient
{
public:
    void setData (const bus::TrackData& data);
    void paint (juce::Graphics&) override;

    /** Share of the applied release done by Space, 0..100. */
    int spacePercent() const noexcept       { return spacePct; }
    int unresolvedBands() const noexcept    { return unresolved; }
    bool linkOn() const noexcept            { return link; }

private:
    bus::TrackData data;
    int spacePct = 0, unresolved = 0;
    bool link = true;
};

} // namespace ff
