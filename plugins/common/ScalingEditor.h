// FreqFreaks — frequency-unmasking plugin suite
// Copyright (C) 2026 FreqFreaks contributors
// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <juce_audio_processors/juce_audio_processors.h>

#include <functional>

namespace ff
{

/** The plug-in window: holds the real UI at its base size and scales it as a whole.

    Dragging the bottom-right corner resizes the window from 75 % to 200 % with the
    aspect ratio kept. The layout never changes, only the scale, so nothing jumps or
    overlaps while dragging, and text and lines are drawn sharp at every size.

    The size is stored with the session (through onScaleChanged) and remembered as the
    default for new windows (a small settings file under the user's app data). A window
    never opens bigger than the screen: on a laptop it starts smaller to fit. If the DAW
    gives the window another shape, the UI is fitted inside and centred, and the margins
    are the plug-in's own background (no bars). */
class ScalingEditor : public juce::AudioProcessorEditor
{
public:
    static constexpr float kMinScale = 0.6f, kMaxScale = 2.0f;

    /** Largest scale at which the window fits on the screen it opens on (DAW frame included). */
    static float fitToScreen (int baseWidth, int baseHeight);

    ScalingEditor (juce::AudioProcessor&, std::unique_ptr<juce::Component> content, int baseWidth, int baseHeight,
                   juce::String settingsKey, float savedScale, std::function<void (float)> onScaleChanged);
    ~ScalingEditor() override;

    juce::Component& getContent() noexcept          { return *content; }
    float getScale() const noexcept                 { return scale; }

    /** Sets the window to an exact scale (from the size menu). */
    void setScale (float);

    /** The size menu: 75 / 100 / 125 / 150 / 200 %. */
    void showScaleMenu();

    /** Opens the size menu from anywhere inside the UI. */
    static void showMenuFor (juce::Component& inside);

    void paint (juce::Graphics&) override;
    void resized() override;

private:
    static float loadDefault (const juce::String& key);
    static void storeDefault (const juce::String& key, float);

    std::unique_ptr<juce::Component> content;
    const int baseW, baseH;
    const juce::String key;
    std::function<void (float)> onScaleChanged;
    float scale = 1.0f;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (ScalingEditor)
};

} // namespace ff
