// FreqFreaks — frequency-unmasking plugin suite
// Copyright (C) 2026 FreqFreaks contributors
// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

/** The visual language from the spec: one colour = one meaning, everywhere. */
namespace ff::theme
{
    inline const juce::Colour background  { 0xff0b0e13 };
    inline const juce::Colour headerBg    { 0xff0e1218 };
    inline const juce::Colour panel       { 0xff11161d };
    inline const juce::Colour panelDeep   { 0xff0d1117 };
    inline const juce::Colour raised      { 0xff161b22 };
    inline const juce::Colour selected    { 0xff2a313c };
    inline const juce::Colour border      { 0xff1e2530 };
    inline const juce::Colour borderLight { 0xff2a313c };
    inline const juce::Colour track       { 0xff232a35 };
    inline const juce::Colour gridLine    { 0xff1c222b };

    inline const juce::Colour text        { 0xffe6eaf0 };
    inline const juce::Colour textDim     { 0xff9aa4b2 };

    inline const juce::Colour space       { 0xff3dd6c6 };   // Space module
    inline const juce::Colour eq          { 0xfff5a524 };   // EQ module
    inline const juce::Colour conflict    { 0xffff5d5d };   // unresolved
    inline const juce::Colour idle        { 0xff3a4250 };   // not released (Link off)
    inline const juce::Colour reference   { 0xffe879f9 };

    /** Track colour from the DAW, or a stable fallback derived from the instance id. */
    inline juce::Colour trackColour (juce::uint32 argb, juce::uint64 id)
    {
        if ((argb >> 24) != 0)
            return juce::Colour (argb);
        static const juce::uint32 palette[] = { 0xff60a5fa, 0xffa3e635, 0xffc4b5fd, 0xfffde047,
                                                0xff38bdf8, 0xfffb923c, 0xff94a3b8, 0xff34d399 };
        return juce::Colour (palette[id % (sizeof (palette) / sizeof (palette[0]))]);
    }

    inline juce::Font font (float height, bool bold = false)
    {
        return juce::Font (juce::FontOptions (height, bold ? juce::Font::bold : juce::Font::plain));
    }

    /** UTF-8 literal to juce::String (the source files are UTF-8). */
    inline juce::String u8 (const char* utf8)   { return juce::String::fromUTF8 (utf8); }

    inline juce::Font mono (float height)
    {
        return juce::Font (juce::FontOptions (juce::Font::getDefaultMonospacedFontName(), height, juce::Font::plain));
    }
}
