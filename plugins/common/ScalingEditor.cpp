// FreqFreaks — frequency-unmasking plugin suite
// Copyright (C) 2026 FreqFreaks contributors
// SPDX-License-Identifier: GPL-3.0-or-later
#include "ScalingEditor.h"
#include "Theme.h"

namespace ff
{

namespace
{
    juce::PropertiesFile::Options settingsOptions()
    {
        juce::PropertiesFile::Options o;
        o.applicationName = "FreqFreaks";
        o.folderName = "FreqFreaks";
        o.filenameSuffix = ".settings";
        o.osxLibrarySubFolder = "Application Support";
        return o;
    }
}

ScalingEditor::ScalingEditor (juce::AudioProcessor& p, std::unique_ptr<juce::Component> c, int w, int h,
                              juce::String settingsKey, float savedScale, std::function<void (float)> callback)
    : AudioProcessorEditor (p), content (std::move (c)), baseW (w), baseH (h),
      key (std::move (settingsKey)), onScaleChanged (std::move (callback))
{
    addAndMakeVisible (*content);
    content->setBounds (0, 0, baseW, baseH);

    // Setting the limits resizes the (still empty) window to the minimum and calls resized(),
    // which would overwrite the scale: keep the wanted one aside.
    const float initial = juce::jlimit (kMinScale, kMaxScale, savedScale > 0.0f ? savedScale : loadDefault (key));
    scale = initial;

    setResizable (true, true);
    setResizeLimits (juce::roundToInt (baseW * kMinScale), juce::roundToInt (baseH * kMinScale),
                     juce::roundToInt (baseW * kMaxScale), juce::roundToInt (baseH * kMaxScale));
    if (auto* c2 = getConstrainer())
        c2->setFixedAspectRatio ((double) baseW / (double) baseH);
    setSize (juce::roundToInt (baseW * initial), juce::roundToInt (baseH * initial));
}

ScalingEditor::~ScalingEditor()
{
    storeDefault (key, scale);
}

void ScalingEditor::paint (juce::Graphics& g)
{
    g.fillAll (theme::background);
}

void ScalingEditor::resized()
{
    // One uniform scale from the width (the constrainer keeps the aspect ratio).
    const float s = juce::jlimit (kMinScale, kMaxScale, (float) getWidth() / (float) baseW);
    content->setTransform (juce::AffineTransform::scale (s));
    scale = s;
    if (onScaleChanged)
        onScaleChanged (scale);
}

void ScalingEditor::setScale (float s)
{
    s = juce::jlimit (kMinScale, kMaxScale, s);
    setSize (juce::roundToInt (baseW * s), juce::roundToInt (baseH * s));
    storeDefault (key, s);
}

void ScalingEditor::showScaleMenu()
{
    juce::PopupMenu menu;
    menu.addSectionHeader (theme::u8 ("Размер окна"));
    for (int pct : { 75, 100, 125, 150, 200 })
    {
        const bool current = std::abs (scale * 100.0f - (float) pct) < 1.0f;
        menu.addItem (juce::String (pct) + " %", true, current, [safe = juce::Component::SafePointer<ScalingEditor> (this), pct] {
            if (safe != nullptr)
                safe->setScale ((float) pct / 100.0f);
        });
    }
    menu.addSeparator();
    menu.addItem (theme::u8 ("Тяни за правый нижний угол — любой размер"), false, false, nullptr);
    menu.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (this).withMousePosition());
}

void ScalingEditor::showMenuFor (juce::Component& inside)
{
    if (auto* editor = inside.findParentComponentOfClass<ScalingEditor>())
        editor->showScaleMenu();
}

float ScalingEditor::loadDefault (const juce::String& k)
{
    juce::PropertiesFile file (settingsOptions());
    return (float) file.getDoubleValue (k, 1.0);
}

void ScalingEditor::storeDefault (const juce::String& k, float s)
{
    juce::PropertiesFile file (settingsOptions());
    if (std::abs (file.getDoubleValue (k, 1.0) - (double) s) > 1.0e-3)
    {
        file.setValue (k, (double) s);
        file.saveIfNeeded();
    }
}

} // namespace ff
