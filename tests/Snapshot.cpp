// FreqFreaks — frequency-unmasking plugin suite
// Copyright (C) 2026 FreqFreaks contributors
// SPDX-License-Identifier: GPL-3.0-or-later
//
// Renders the real Track and Hub editors to PNG files after playing a small
// demo mix through them, so the UI can be checked (and documented) without a DAW.
//
// Usage: freqfreaks_snapshot <output-directory>

#include "track/TrackEditor.h"
#include "track/TrackProcessor.h"
#include "hub/HubEditor.h"
#include "hub/HubProcessor.h"
#include "common/ScalingEditor.h"

#include "ff/Biquad.h"

#include <functional>
#include <map>
#include <random>

namespace
{
constexpr double kRate = 48000.0;
constexpr int kBlock = 512;

struct Voice
{
    std::unique_ptr<ff::TrackProcessor> processor = std::make_unique<ff::TrackProcessor>();
    std::mt19937 gen;
    std::normal_distribution<float> dist { 0.0f, 1.0f };
    ff::BiquadCoeffs shape;
    ff::BiquadState s1, s2, side1, side2;
    float gain, width;

    Voice (const char* name, juce::uint32 colour, unsigned seed, double hz, double q, float g, float w)
        : gen (seed), shape (ff::BiquadCoeffs::bandpass (kRate, hz, q)), gain (g), width (w)
    {
        juce::AudioProcessor::TrackProperties props;
        props.name = juce::String::fromUTF8 (name);
        props.colour = juce::Colour (colour);
        processor->updateTrackProperties (props);
        processor->setPlayConfigDetails (2, 2, kRate, kBlock);
        processor->prepareToPlay (kRate, kBlock);
    }

    void yieldTo (const Voice& other)
    {
        processor->setReference (other.processor->instanceId(), other.processor->trackName());
    }

    void process (juce::AudioBuffer<float>& mixBus)
    {
        juce::AudioBuffer<float> buffer (2, kBlock);
        for (int i = 0; i < kBlock; ++i)
        {
            const float m = s2.process (s1.process (dist (gen), shape), shape) * gain;
            const float s = side2.process (side1.process (dist (gen), shape), shape) * gain * width;
            buffer.setSample (0, i, m + s);
            buffer.setSample (1, i, m - s);
        }
        juce::MidiBuffer midi;
        processor->processBlock (buffer, midi);
        for (int ch = 0; ch < 2; ++ch)
            mixBus.addFrom (ch, 0, buffer, ch, 0, kBlock);
    }
};

bool save (juce::Component& c, const juce::File& file)
{
    const auto image = c.createComponentSnapshot (c.getLocalBounds(), true, 2.0f);
    file.deleteFile();
    juce::FileOutputStream out (file);
    juce::PNGImageFormat png;
    const bool ok = out.openedOk() && png.writeImageToStream (image, out);
    std::printf ("%s %s\n", ok ? "wrote" : "FAILED", file.getFullPathName().toRawUTF8());
    return ok;
}
/** For the manual: one picture per control of the Track window. Every control carries a
    "Title\nexplanation" tooltip (it feeds the help block); controls with the same title
    (the A and B buttons, the Space mode chips) share one picture. The painted captions
    around a control (a knob's name, a row's heading) are taken in with some margin. */
void exportControls (juce::Component& content, const juce::File& dir)
{
    dir.createDirectory();
    const auto image = content.createComponentSnapshot (content.getLocalBounds(), true, 2.0f);
    std::map<juce::String, juce::Rectangle<int>> areas;
    std::function<void (juce::Component&)> walk = [&] (juce::Component& c) {
        for (auto* child : c.getChildren())
        {
            if (! child->isVisible())
                continue;
            if (auto* tip = dynamic_cast<juce::TooltipClient*> (child))
            {
                auto title = tip->getTooltip().upToFirstOccurrenceOf ("\n", false, false).trim();
                if (title.startsWith (juce::String::fromUTF8 ("Режим Space")))
                    title = juce::String::fromUTF8 ("Режим Space");
                if (title == "Simple" || title == "Detail" || title == "Expert")
                    title = "Simple / Detail / Expert";
                if (title.isNotEmpty())
                {
                    const auto r = content.getLocalArea (child, child->getLocalBounds());
                    auto& a = areas[title];
                    a = a.isEmpty() ? r : a.getUnion (r);
                }
            }
            walk (*child);
        }
    };
    walk (content);

    // Extra room (left, top, right, bottom) for captions the editor paints around a control.
    const std::map<juce::String, std::array<int, 4>> margins {
        { "Amount", { 14, 6, 14, 46 } }, { "Space Amount", { 14, 6, 14, 46 } },
        { juce::String::fromUTF8 ("Скорость"), { 4, 26, 4, 24 } },
        { "Output", { 4, 26, 4, 6 } },
        { "Sharp", { 230, 6, 6, 6 } },
    };
    juce::String index;
    int n = 0;
    for (const auto& [title, area] : areas)
    {
        auto m = std::array<int, 4> { 8, 8, 8, 8 };
        if (auto it = margins.find (title); it != margins.end())
            m = it->second;
        else if (title == juce::String::fromUTF8 ("Режим Space") || title.startsWith (juce::String::fromUTF8 ("Сторона")))
            m = { 4, 26, 4, 6 };
        const auto r = area.withTrimmedLeft (-m[0]).withTrimmedTop (-m[1]).withTrimmedRight (-m[2]).withTrimmedBottom (-m[3])
                           .getIntersection (content.getLocalBounds());
        const auto crop = image.getClippedImage (juce::Rectangle<int> (r.getX() * 2, r.getY() * 2, r.getWidth() * 2, r.getHeight() * 2));
        const auto name = "control-" + juce::String (++n) + ".png";
        juce::FileOutputStream out (dir.getChildFile (name));
        out.setPosition (0);
        out.truncate();
        juce::PNGImageFormat().writeImageToStream (crop, out);
        index << title << "\t" << name << "\n";
    }
    dir.getChildFile ("index.tsv").replaceWithText (index, false, false, "\n");
    std::printf ("wrote %d control pictures\n", n);
}
} // namespace

int main (int argc, char** argv)
{
    juce::ScopedJuceInitialiser_GUI juce;
    const juce::File outDir (argc > 1 ? juce::File::getCurrentWorkingDirectory().getChildFile (argv[1])
                                      : juce::File::getCurrentWorkingDirectory());
    outDir.createDirectory();

    Voice vocal ("Вокал", 0xffe879f9, 1, 1600.0, 0.6, 0.16f, 0.03f);
    Voice guitarL ("Гитара L", 0xff60a5fa, 2, 1500.0, 0.45, 0.16f, 0.12f);
    Voice pad ("Пэд", 0xffc4b5fd, 3, 600.0, 0.5, 0.09f, 0.25f);
    Voice bass ("Бас", 0xffa3e635, 4, 90.0, 0.9, 0.22f, 0.0f);
    Voice kick ("Бочка", 0xff94a3b8, 5, 65.0, 1.2, 0.22f, 0.0f);
    // A bigger session, so the Hub's list has to scroll.
    std::vector<std::unique_ptr<Voice>> more;
    const char* extraNames[] = { "Гитара R", "Клавиши", "Синт лид", "Бэк-вокал", "Перкуссия", "Струнные", "Хэт", "Малый" };
    for (int i = 0; i < 8; ++i)
        more.push_back (std::make_unique<Voice> (extraNames[i], 0xff38bdf8u + (juce::uint32) i * 0x00102030u, 10u + (unsigned) i,
                                                 300.0 * (1 + i), 0.6, 0.05f, 0.1f));
    for (int i = 0; i < 4; ++i)
        more[(size_t) i]->yieldTo (vocal);

    guitarL.yieldTo (vocal);
    pad.yieldTo (vocal);
    bass.yieldTo (kick);

    // Guitar: Expert view; push it a bit so the budget shows Space, EQ and an unresolved band.
    if (auto* p = guitarL.processor->state().getParameter (ff::trackParams::amount))
        p->setValueNotifyingHost (p->convertTo0to1 (85.0f));
    guitarL.processor->setUiLevel (2);
    // Attack set by hand (neon ring), Release left on auto (grey, follows Speed).
    if (auto* p = guitarL.processor->state().getParameter (ff::trackParams::attack))
        p->setValueNotifyingHost (p->convertTo0to1 (25.0f));
    if (auto* p = guitarL.processor->state().getParameter (ff::trackParams::attackManual))
        p->setValueNotifyingHost (1.0f);

    // Sharp at 40 % on the guitar, so the wave shows the narrower bells.
    if (auto* p = guitarL.processor->state().getParameter (ff::trackParams::sharp))
        p->setValueNotifyingHost (p->convertTo0to1 (40.0f));

    ff::HubProcessor hub;
    hub.setPlayConfigDetails (2, 2, kRate, kBlock);
    hub.prepareToPlay (kRate, kBlock);

    juce::MidiBuffer midi;
    for (int b = 0; b < (int) (4.0 * kRate / kBlock); ++b)
    {
        juce::AudioBuffer<float> mix (2, kBlock);
        mix.clear();
        for (auto* v : { &vocal, &guitarL, &pad, &bass, &kick })
            v->process (mix);
        for (auto& v : more)
            v->process (mix);
        hub.processBlock (mix, midi);
    }

    bool ok = true;
    {
        auto editor = std::unique_ptr<juce::AudioProcessorEditor> (guitarL.processor->createEditor());
        editor->setSize (ff::TrackEditor::kWidth, ff::TrackEditor::kHeight);   // 100 %, whatever the screen
        ok &= save (*editor, outDir.getChildFile ("track.png"));
        if (auto* scalingForControls = dynamic_cast<ff::ScalingEditor*> (editor.get()))
            exportControls (scalingForControls->getContent(), outDir.getChildFile ("controls"));

        // The window scales as a whole: dragging the corner to 150 % keeps the layout.
        if (auto* scaling = dynamic_cast<ff::ScalingEditor*> (editor.get()))
        {
            const auto before = scaling->getScale();
            ok &= std::abs (before - 1.0f) < 0.01f;
            scaling->setSize (ff::TrackEditor::kWidth * 3 / 2, ff::TrackEditor::kHeight * 3 / 2);
            ok &= std::abs (scaling->getScale() - 1.5f) < 0.01f;
            ok &= scaling->getContent().getWidth() == ff::TrackEditor::kWidth;
            std::printf ("scale %.2f -> %.2f\n", before, scaling->getScale());
            scaling->setScale (before);
            scaling->setSize (ff::TrackEditor::kWidth, ff::TrackEditor::kHeight);
            scaling->showAbout();
            ok &= save (*editor, outDir.getChildFile ("about.png"));
        }
    }
    // Hub: guitar trimmed to 60 %, pad bypassed from the Hub.
    hub.setTrim (guitarL.processor->instanceId(), 0.6f);
    hub.setTrackBypassed (pad.processor->instanceId(), true);
    for (int b = 0; b < (int) (1.0 * kRate / kBlock); ++b)
    {
        juce::AudioBuffer<float> mix (2, kBlock);
        mix.clear();
        for (auto* v : { &vocal, &guitarL, &pad, &bass, &kick })
            v->process (mix);
        for (auto& v : more)
            v->process (mix);
        hub.processBlock (mix, midi);
    }
    {
        auto editor = std::unique_ptr<juce::AudioProcessorEditor> (hub.createEditor());
        editor->setSize (ff::HubEditor::kWidth, ff::HubEditor::kHeight);
        ok &= save (*editor, outDir.getChildFile ("hub-map.png"));
        auto* scaling = dynamic_cast<ff::ScalingEditor*> (editor.get());
        if (auto* hubEditor = scaling != nullptr ? dynamic_cast<ff::HubEditor*> (&scaling->getContent()) : nullptr)
        {
            hubEditor->openRemote (guitarL.processor->instanceId());
            ok &= save (*editor, outDir.getChildFile ("hub-panel.png"));
        }
    }
    return ok ? 0 : 1;
}
