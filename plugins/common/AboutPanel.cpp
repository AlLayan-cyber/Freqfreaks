// FreqFreaks — frequency-unmasking plugin suite
// Copyright (C) 2026 FreqFreaks contributors
// SPDX-License-Identifier: GPL-3.0-or-later
#include "AboutPanel.h"
#include "Brand.h"

#include <FreqFreaksLicence.h>

#ifndef FREQFREAKS_VERSION
 #define FREQFREAKS_VERSION "dev"
#endif

namespace ff
{

juce::String AboutPanel::version()   { return FREQFREAKS_VERSION; }

AboutPanel::AboutPanel (const juce::String& productName)
    : product (productName),
      source (theme::u8 ("Исходный код: github.com/AlLayan-cyber/Freqfreaks"), juce::URL ("https://github.com/AlLayan-cyber/Freqfreaks")),
      closeButton (theme::u8 ("Закрыть"))
{
    licence.setMultiLine (true, true);
    licence.setReadOnly (true);
    licence.setScrollbarsShown (true);
    licence.setCaretVisible (false);
    licence.setFont (theme::mono (11.0f));
    licence.setColour (juce::TextEditor::backgroundColourId, theme::panelDeep);
    licence.setColour (juce::TextEditor::textColourId, theme::textDim);
    licence.setColour (juce::TextEditor::outlineColourId, theme::border);
    licence.setText (juce::String::fromUTF8 (FreqFreaksLicence::LICENSE, FreqFreaksLicence::LICENSESize), false);
    addAndMakeVisible (licence);

    source.setFont (theme::font (12.5f), false, juce::Justification::centredLeft);
    source.setColour (juce::HyperlinkButton::textColourId, theme::space);
    addAndMakeVisible (source);

    closeButton.onClick = [this] { close(); };
    addAndMakeVisible (closeButton);
    setWantsKeyboardFocus (true);
}

juce::Rectangle<int> AboutPanel::card() const
{
    return getLocalBounds().withSizeKeepingCentre (juce::jmin (680, getWidth() - 40), juce::jmin (560, getHeight() - 40));
}

void AboutPanel::resized()
{
    auto c = card().reduced (24);
    c.removeFromTop (150);
    source.setBounds (c.removeFromTop (22));
    c.removeFromTop (8);
    closeButton.setBounds (c.removeFromBottom (30).removeFromRight (110));
    c.removeFromBottom (10);
    licence.setBounds (c);
}

void AboutPanel::paint (juce::Graphics& g)
{
    g.fillAll (juce::Colours::black.withAlpha (0.6f));
    const auto c = card();
    g.setColour (theme::panel);
    g.fillRoundedRectangle (c.toFloat(), 12.0f);
    g.setColour (theme::borderLight);
    g.drawRoundedRectangle (c.toFloat().reduced (0.5f), 12.0f, 1.0f);

    auto r = c.reduced (24);
    brand::drawMoonLogo (g, { (float) r.getX() + 22.0f, (float) r.getY() + 22.0f }, 22.0f);
    g.setColour (theme::textDim);
    g.setFont (theme::font (12.0f, true).withExtraKerningFactor (0.12f));
    g.drawText (brand::developer, r.getX() + 56, r.getY() + 4, 400, 16, juce::Justification::centredLeft);
    g.setColour (theme::text);
    g.setFont (theme::font (22.0f, true));
    g.drawText (product, r.getX() + 56, r.getY() + 20, 400, 28, juce::Justification::centredLeft);
    g.setColour (theme::textDim);
    g.setFont (theme::font (12.5f));
    g.drawText (theme::u8 ("Версия ") + version(), r.getX() + 56, r.getY() + 48, 400, 18, juce::Justification::centredLeft);

    auto text = r.withTrimmedTop (82).removeFromTop (64);
    g.setColour (theme::text);
    g.setFont (theme::font (13.0f));
    g.drawFittedText (theme::u8 ("Бесплатный плагин с открытым исходным кодом. Лицензия GNU GPL версии 3 или новее: "
                                 "можно пользоваться, копировать, изменять и распространять, сохраняя ту же лицензию "
                                 "и открытый код. Без каких-либо гарантий. Сделано на JUCE 8 (AGPLv3)."),
                      text, juce::Justification::topLeft, 4, 0.95f);
}

void AboutPanel::mouseDown (const juce::MouseEvent& e)
{
    if (! card().contains (e.getPosition()))
        close();
}

bool AboutPanel::keyPressed (const juce::KeyPress& key)
{
    if (key == juce::KeyPress::escapeKey)
    {
        close();
        return true;
    }
    return false;
}

void AboutPanel::close()
{
    juce::Component::SafePointer<AboutPanel> self (this);
    juce::MessageManager::callAsync ([self] {
        if (self != nullptr && self->onClose)
            self->onClose();
    });
}

} // namespace ff
