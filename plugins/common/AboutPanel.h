// FreqFreaks — frequency-unmasking plugin suite
// Copyright (C) 2026 FreqFreaks contributors
// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

namespace ff
{

/** "About": the developer, the version, the licence (with its full text) and where the
    source code is. Lies over the whole window; click outside, Esc or Close to dismiss. */
class AboutPanel : public juce::Component
{
public:
    explicit AboutPanel (const juce::String& productName);

    /** Called (asynchronously) when the panel wants to close; the owner deletes it. */
    std::function<void()> onClose;

    void paint (juce::Graphics&) override;
    void resized() override;
    void mouseDown (const juce::MouseEvent&) override;
    bool keyPressed (const juce::KeyPress&) override;

    /** Version string of this build, e.g. "0.3.0". */
    static juce::String version();

private:
    juce::Rectangle<int> card() const;
    void close();

    juce::String product;
    juce::TextEditor licence;
    juce::HyperlinkButton source;
    juce::TextButton closeButton;
};

} // namespace ff
