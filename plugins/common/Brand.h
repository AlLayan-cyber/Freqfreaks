// FreqFreaks — frequency-unmasking plugin suite
// Copyright (C) 2026 FreqFreaks contributors
// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "Theme.h"

/** The developer's mark: Moon Audio Engineering. */
namespace ff::brand
{
    inline constexpr const char* developer = "MOON AUDIO ENGINEERING";

    /** The Moon Audio Engineering logo, drawn as vectors so it is sharp at every size:
        a white disc with three dark bars (medium, short, tall). */
    inline void drawMoonLogo (juce::Graphics& g, juce::Point<float> centre, float radius,
                              juce::Colour disc = juce::Colours::white, juce::Colour ink = juce::Colour (0xff0b0e13))
    {
        g.setColour (disc);
        g.fillEllipse (juce::Rectangle<float> (radius * 2.0f, radius * 2.0f).withCentre (centre));

        // Bars as fractions of the radius: x from the centre, half height, width.
        struct Bar { float x, halfHeight, width; };
        const Bar bars[] = { { -0.36f, 0.50f, 0.16f }, { 0.02f, 0.25f, 0.16f }, { 0.34f, 0.76f, 0.19f } };
        g.setColour (ink);
        for (const auto& b : bars)
        {
            const float w = b.width * radius, h = 2.0f * b.halfHeight * radius;
            g.fillRoundedRectangle (juce::Rectangle<float> (w, h).withCentre ({ centre.x + b.x * radius, centre.y }), w * 0.5f);
        }
    }

    /** Footer strip along the bottom of a window: logo and the developer's name. */
    inline void drawFooter (juce::Graphics& g, juce::Rectangle<int> area, const juce::String& rightText)
    {
        g.setColour (theme::headerBg);
        g.fillRect (area);
        g.setColour (theme::border);
        g.drawHorizontalLine (area.getY(), (float) area.getX(), (float) area.getRight());

        auto r = area.reduced (16, 0);
        const float radius = juce::jmin (9.0f, (float) area.getHeight() * 0.32f);
        drawMoonLogo (g, { (float) r.getX() + radius, (float) r.getCentreY() }, radius);
        r.removeFromLeft (juce::roundToInt (radius * 2.0f) + 8);

        g.setColour (theme::textDim);
        g.setFont (theme::font (11.0f, true).withExtraKerningFactor (0.12f));
        g.drawText (developer, r, juce::Justification::centredLeft);

        g.setFont (theme::font (11.0f));
        g.drawText (rightText, r, juce::Justification::centredRight);
    }
}
