// FreqFreaks — frequency-unmasking plugin suite
// Copyright (C) 2026 FreqFreaks contributors
// SPDX-License-Identifier: GPL-3.0-or-later
#include "LookAndFeel.h"

namespace ff
{

LookAndFeel::LookAndFeel()
{
    using namespace theme;
    setColour (juce::ResizableWindow::backgroundColourId, background);
    setColour (juce::Label::textColourId, text);
    setColour (juce::Slider::textBoxTextColourId, text);
    setColour (juce::Slider::textBoxOutlineColourId, juce::Colours::transparentBlack);
    setColour (juce::Slider::rotarySliderFillColourId, text);
    setColour (juce::Slider::rotarySliderOutlineColourId, track);
    setColour (juce::Slider::trackColourId, space);
    setColour (juce::Slider::backgroundColourId, track);
    setColour (juce::Slider::thumbColourId, text);
    setColour (juce::TextButton::buttonColourId, juce::Colours::transparentBlack);
    setColour (juce::TextButton::buttonOnColourId, selected);
    setColour (juce::TextButton::textColourOffId, textDim);
    setColour (juce::TextButton::textColourOnId, text);
    setColour (juce::ComboBox::backgroundColourId, raised);
    setColour (juce::ComboBox::outlineColourId, borderLight);
    setColour (juce::ComboBox::textColourId, text);
    setColour (juce::ComboBox::arrowColourId, textDim);
    setColour (juce::PopupMenu::backgroundColourId, raised);
    setColour (juce::PopupMenu::textColourId, text);
    setColour (juce::PopupMenu::highlightedBackgroundColourId, selected);
    setColour (juce::PopupMenu::highlightedTextColourId, text);
    setColour (juce::TooltipWindow::backgroundColourId, raised);
    setColour (juce::TooltipWindow::textColourId, text);
    setColour (juce::TooltipWindow::outlineColourId, borderLight);
}

void LookAndFeel::drawRotarySlider (juce::Graphics& g, int x, int y, int width, int height, float sliderPos,
                                    float startAngle, float endAngle, juce::Slider& slider)
{
    const auto bounds = juce::Rectangle<int> (x, y, width, height).toFloat().reduced (8.0f);
    const float radius = juce::jmin (bounds.getWidth(), bounds.getHeight()) * 0.5f;
    const float lineW = radius * 0.2f;
    const auto centre = bounds.getCentre();
    const float arcRadius = radius - lineW * 0.5f;

    juce::Path background;
    background.addCentredArc (centre.x, centre.y, arcRadius, arcRadius, 0.0f, startAngle, endAngle, true);
    g.setColour (slider.findColour (juce::Slider::rotarySliderOutlineColourId));
    g.strokePath (background, juce::PathStrokeType (lineW, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));

    if (sliderPos > 0.001f)
    {
        juce::Path value;
        value.addCentredArc (centre.x, centre.y, arcRadius, arcRadius, 0.0f, startAngle,
                             startAngle + sliderPos * (endAngle - startAngle), true);
        g.setColour (slider.findColour (juce::Slider::rotarySliderFillColourId).withMultipliedAlpha (slider.isEnabled() ? 1.0f : 0.4f));
        g.strokePath (value, juce::PathStrokeType (lineW, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
    }
}

void LookAndFeel::drawLinearSlider (juce::Graphics& g, int x, int y, int width, int height, float sliderPos,
                                    float, float, juce::Slider::SliderStyle style, juce::Slider& slider)
{
    if (style != juce::Slider::LinearHorizontal)
    {
        LookAndFeel_V4::drawLinearSlider (g, x, y, width, height, sliderPos, 0.0f, 0.0f, style, slider);
        return;
    }

    const float trackH = 4.0f;
    const float cy = (float) y + (float) height * 0.5f;
    const juce::Rectangle<float> trackRect ((float) x, cy - trackH * 0.5f, (float) width, trackH);
    g.setColour (slider.findColour (juce::Slider::backgroundColourId));
    g.fillRoundedRectangle (trackRect, trackH * 0.5f);

    g.setColour (slider.findColour (juce::Slider::trackColourId));
    g.fillRoundedRectangle (trackRect.withRight (sliderPos), trackH * 0.5f);

    const float thumb = 12.0f;
    g.setColour (slider.findColour (juce::Slider::thumbColourId));
    g.fillEllipse (sliderPos - thumb * 0.5f, cy - thumb * 0.5f, thumb, thumb);
}

void LookAndFeel::drawButtonBackground (juce::Graphics& g, juce::Button& button, const juce::Colour& backgroundColour,
                                        bool isMouseOver, bool isButtonDown)
{
    auto r = button.getLocalBounds().toFloat().reduced (0.5f);
    const float corner = juce::jmin (7.0f, r.getHeight() * 0.5f);

    auto fill = backgroundColour;
    if (isButtonDown)
        fill = fill.brighter (0.15f);
    else if (isMouseOver && ! button.getToggleState())
        fill = fill.isTransparent() ? theme::raised : fill.brighter (0.08f);

    g.setColour (fill);
    g.fillRoundedRectangle (r, corner);

    const auto outline = button.findColour (juce::ComboBox::outlineColourId);
    if (! outline.isTransparent())
    {
        g.setColour (outline);
        g.drawRoundedRectangle (r, corner, 1.0f);
    }
}

void LookAndFeel::drawButtonText (juce::Graphics& g, juce::TextButton& button, bool, bool)
{
    g.setFont (getTextButtonFont (button, button.getHeight()));
    g.setColour (button.findColour (button.getToggleState() ? juce::TextButton::textColourOnId
                                                             : juce::TextButton::textColourOffId)
                     .withMultipliedAlpha (button.isEnabled() ? 1.0f : 0.5f));
    g.drawFittedText (button.getButtonText(), button.getLocalBounds().reduced (4, 0), juce::Justification::centred, 1);
}

void LookAndFeel::drawComboBox (juce::Graphics& g, int width, int height, bool, int, int, int, int, juce::ComboBox& box)
{
    const juce::Rectangle<float> r (0.5f, 0.5f, (float) width - 1.0f, (float) height - 1.0f);
    g.setColour (box.findColour (juce::ComboBox::backgroundColourId));
    g.fillRoundedRectangle (r, r.getHeight() * 0.5f);
    g.setColour (box.findColour (juce::ComboBox::outlineColourId));
    g.drawRoundedRectangle (r, r.getHeight() * 0.5f, 1.0f);

    juce::Path arrow;
    const float ax = (float) width - 18.0f, ay = (float) height * 0.5f;
    arrow.startNewSubPath (ax - 4.0f, ay - 2.0f);
    arrow.lineTo (ax, ay + 2.0f);
    arrow.lineTo (ax + 4.0f, ay - 2.0f);
    g.setColour (box.findColour (juce::ComboBox::arrowColourId));
    g.strokePath (arrow, juce::PathStrokeType (1.5f));
}

juce::Font LookAndFeel::getTextButtonFont (juce::TextButton&, int buttonHeight)
{
    return theme::font (juce::jmin (13.0f, (float) buttonHeight * 0.48f), true);
}

juce::Font LookAndFeel::getComboBoxFont (juce::ComboBox&)  { return theme::font (13.0f); }
juce::Font LookAndFeel::getPopupMenuFont()                 { return theme::font (14.0f); }

juce::Label* LookAndFeel::createSliderTextBox (juce::Slider& slider)
{
    auto* l = LookAndFeel_V4::createSliderTextBox (slider);
    l->setFont (theme::mono (13.0f));
    l->setColour (juce::Label::backgroundColourId, juce::Colours::transparentBlack);
    l->setColour (juce::Label::outlineColourId, juce::Colours::transparentBlack);
    l->setJustificationType (juce::Justification::centredRight);
    return l;
}

//==============================================================================
void styleSegment (juce::TextButton& button, int radioGroup)
{
    button.setClickingTogglesState (true);
    button.setRadioGroupId (radioGroup);
    button.setColour (juce::TextButton::buttonColourId, juce::Colours::transparentBlack);
    button.setColour (juce::TextButton::buttonOnColourId, theme::selected);
    button.setColour (juce::ComboBox::outlineColourId, juce::Colours::transparentBlack);
}

void styleChip (juce::TextButton& button, juce::Colour accent, int radioGroup)
{
    button.setClickingTogglesState (true);
    button.setRadioGroupId (radioGroup);
    button.setColour (juce::TextButton::buttonColourId, juce::Colours::transparentBlack);
    button.setColour (juce::TextButton::buttonOnColourId, accent.withAlpha (0.14f));
    button.setColour (juce::ComboBox::outlineColourId, theme::borderLight);
    button.onStateChange = [&button, accent] {
        button.setColour (juce::ComboBox::outlineColourId, button.getToggleState() ? accent : theme::borderLight);
    };
}

} // namespace ff
