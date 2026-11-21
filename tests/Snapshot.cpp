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

#include "ff/Biquad.h"

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
        hub.processBlock (mix, midi);
    }

    bool ok = true;
    {
        auto editor = std::unique_ptr<juce::AudioProcessorEditor> (guitarL.processor->createEditor());
        ok &= save (*editor, outDir.getChildFile ("track.png"));
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
        hub.processBlock (mix, midi);
    }
    {
        auto editor = std::unique_ptr<juce::AudioProcessorEditor> (hub.createEditor());
        ok &= save (*editor, outDir.getChildFile ("hub-map.png"));
        if (auto* hubEditor = dynamic_cast<ff::HubEditor*> (editor.get()))
        {
            hubEditor->openRemote (guitarL.processor->instanceId());
            ok &= save (*editor, outDir.getChildFile ("hub-panel.png"));
        }
    }
    return ok ? 0 : 1;
}
