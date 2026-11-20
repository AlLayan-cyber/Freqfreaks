// FreqFreaks — frequency-unmasking plugin suite
// Copyright (C) 2026 FreqFreaks contributors
// SPDX-License-Identifier: GPL-3.0-or-later
//
// End-to-end test: loads the built VST3 binaries the way a DAW does and checks
// that a Track yields room to its reference, that the Hub's controls reach it,
// and that the reference can live in another process (sandboxed DAWs).
//
// Usage: freqfreaks_host_test <Track.vst3> <Hub.vst3>
//        freqfreaks_host_test --serve-reference <Track.vst3> <seconds> <id-file>   (child process mode)

#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_events/juce_events.h>

#include "ff/Biquad.h"

#include <cstdio>
#include <random>

namespace
{
constexpr double kRate = 48000.0;
constexpr int kBlock = 512;

int failures = 0;

void check (bool ok, const juce::String& what)
{
    std::printf ("%s %s\n", ok ? "[ ok ]" : "[FAIL]", what.toRawUTF8());
    if (! ok)
        ++failures;
}

std::unique_ptr<juce::AudioPluginInstance> load (juce::AudioPluginFormatManager& fm, const juce::String& path)
{
    juce::OwnedArray<juce::PluginDescription> types;
    for (auto* format : fm.getFormats())
        format->findAllTypesForFile (types, path);
    if (types.isEmpty())
    {
        std::printf ("cannot find a plug-in in %s\n", path.toRawUTF8());
        return {};
    }
    juce::String error;
    auto instance = fm.createPluginInstance (*types[0], kRate, kBlock, error);
    if (instance == nullptr)
        std::printf ("cannot load %s: %s\n", path.toRawUTF8(), error.toRawUTF8());
    else
        instance->prepareToPlay (kRate, kBlock);
    return instance;
}

/** A JUCE-hosted VST3 wraps the plug-in's own state as base64 in <VST3PluginState><IComponent>. */
juce::ValueTree stateOf (juce::AudioPluginInstance& p)
{
    juce::MemoryBlock outer;
    p.getStateInformation (outer);
    auto wrapper = juce::AudioProcessor::getXmlFromBinary (outer.getData(), (int) outer.getSize());
    if (wrapper == nullptr)
        return {};
    auto* component = wrapper->getChildByName ("IComponent");
    if (component == nullptr)
        return {};

    juce::MemoryBlock inner;
    inner.fromBase64Encoding (component->getAllSubText());
    if (auto xml = juce::AudioProcessor::getXmlFromBinary (inner.getData(), (int) inner.getSize()))
        return juce::ValueTree::fromXml (*xml);
    return {};
}

void setState (juce::AudioPluginInstance& p, const juce::ValueTree& tree)
{
    juce::MemoryBlock inner;
    if (auto xml = tree.createXml())
        juce::AudioProcessor::copyXmlToBinary (*xml, inner);

    juce::XmlElement wrapper ("VST3PluginState");
    wrapper.createNewChildElement ("IComponent")->addTextElement (inner.toBase64Encoding());

    juce::MemoryBlock outer;
    juce::AudioProcessor::copyXmlToBinary (wrapper, outer);
    p.setStateInformation (outer.getData(), (int) outer.getSize());
}

juce::AudioProcessorParameter* findParam (juce::AudioPluginInstance& p, const juce::String& name)
{
    for (auto* prm : p.getParameters())
        if (prm->getName (64) == name)
            return prm;
    return nullptr;
}

/** Signal generators: the "vocal" sits in 1-4 kHz, the "guitar" is broadband and louder. */
struct Source
{
    std::mt19937 gen;
    std::normal_distribution<float> dist { 0.0f, 1.0f };
    ff::BiquadCoeffs band = ff::BiquadCoeffs::bandpass (kRate, 2000.0, 0.7);
    ff::BiquadState s1, s2;
    float gain;
    bool bandLimited;

    Source (unsigned seed, float g, bool limited) : gen (seed), gain (g), bandLimited (limited) {}

    void fill (juce::AudioBuffer<float>& buffer)
    {
        for (int i = 0; i < buffer.getNumSamples(); ++i)
        {
            float x = dist (gen);
            if (bandLimited)
                x = s2.process (s1.process (x, band), band);
            buffer.setSample (0, i, x * gain);
            buffer.setSample (1, i, x * gain);
        }
    }
};

/** Running band power of a signal at one frequency. */
struct BandMeter
{
    ff::BiquadCoeffs c;
    ff::BiquadState s1, s2;
    double sum = 0.0;
    long count = 0;

    explicit BandMeter (double hz) : c (ff::BiquadCoeffs::bandpass (kRate, hz, 4.0)) {}

    /** Measures the Mid ((L + R) / 2): that is what masks a centred reference. */
    void add (const juce::AudioBuffer<float>& buffer, bool measure)
    {
        const float* l = buffer.getReadPointer (0);
        const float* r = buffer.getReadPointer (1);
        for (int i = 0; i < buffer.getNumSamples(); ++i)
        {
            const float y = s2.process (s1.process (0.5f * (l[i] + r[i]), c), c);
            if (measure)
            {
                sum += (double) y * y;
                ++count;
            }
        }
    }

    void clear() { sum = 0.0; count = 0; }
    double db() const { return 10.0 * std::log10 (sum / (double) juce::jmax (1L, count) + 1.0e-20); }
};

struct Measure
{
    BandMeter in2k { 2000.0 }, out2k { 2000.0 }, in200 { 200.0 }, out200 { 200.0 };

    void clear() { in2k.clear(); out2k.clear(); in200.clear(); out200.clear(); }
    double drop2k() const  { return out2k.db() - in2k.db(); }
    double drop200() const { return out200.db() - in200.db(); }
};

/** Runs the guitar (and optionally vocal + hub) for some seconds; measures the guitar. */
void run (double seconds, juce::AudioPluginInstance& guitar, juce::AudioPluginInstance* vocal,
          juce::AudioPluginInstance* hub, Source& guitarSrc, Source* vocalSrc, Measure& m, double measureFrom)
{
    juce::AudioBuffer<float> g (2, kBlock), v (2, kBlock), mix (2, kBlock), dry (2, kBlock);
    juce::MidiBuffer midi;
    const int blocks = (int) (seconds * kRate / kBlock);

    for (int b = 0; b < blocks; ++b)
    {
        const bool measure = b * kBlock >= (int) (measureFrom * kRate);
        if (vocal != nullptr && vocalSrc != nullptr)
        {
            vocalSrc->fill (v);
            vocal->processBlock (v, midi);
        }
        guitarSrc.fill (g);
        dry.makeCopyOf (g);
        guitar.processBlock (g, midi);

        // Both are measured through the same meters; latency is irrelevant for long-term band power.
        m.in2k.add (dry, measure);
        m.out2k.add (g, measure);
        m.in200.add (dry, measure);
        m.out200.add (g, measure);

        if (hub != nullptr)
        {
            mix.makeCopyOf (g);
            if (vocal != nullptr)
                for (int ch = 0; ch < 2; ++ch)
                    mix.addFrom (ch, 0, v, ch, 0, kBlock);
            hub->processBlock (mix, midi);
        }

        if (b % 8 == 0)
            juce::MessageManager::getInstance()->runDispatchLoopUntil (1);   // plug-in timers (heartbeats)
    }
}

int serveReference (const juce::String& trackPath, double seconds, const juce::File& idFile)
{
    juce::AudioPluginFormatManager fm;
    fm.addFormat (new juce::VST3PluginFormat());
    auto vocal = load (fm, trackPath);
    if (vocal == nullptr)
        return 2;

    // The id goes through a file: a pipe would only be readable once this process ends.
    idFile.replaceWithText (stateOf (*vocal).getProperty ("instanceId").toString());

    Source src (11, 0.03f, true);
    juce::AudioBuffer<float> buffer (2, kBlock);
    juce::MidiBuffer midi;
    long blocks = 0;
    const auto start = juce::Time::getMillisecondCounterHiRes();
    while (juce::Time::getMillisecondCounterHiRes() - start < seconds * 1000.0)
    {
        // No sleeping: macOS App Nap stretches sleeps of a windowless background process,
        // which would make the published data go stale. Running faster than real time is fine,
        // the other process always reads the latest analysis.
        src.fill (buffer);
        vocal->processBlock (buffer, midi);
        if (++blocks % 64 == 0)
            juce::MessageManager::getInstance()->runDispatchLoopUntil (1);
    }
    return 0;
}
} // namespace

int main (int argc, char** argv)
{
    juce::ScopedJuceInitialiser_GUI juce;

    if (argc == 5 && juce::String (argv[1]) == "--serve-reference")
        return serveReference (argv[2], juce::String (argv[3]).getDoubleValue(), juce::File (argv[4]));

    if (argc < 3)
    {
        std::printf ("usage: %s <Track.vst3> <Hub.vst3>\n", argv[0]);
        return 2;
    }
    const juce::String trackPath (argv[1]), hubPath (argv[2]);

    juce::AudioPluginFormatManager fm;
    fm.addFormat (new juce::VST3PluginFormat());

    auto hub = load (fm, hubPath);
    auto vocal = load (fm, trackPath);
    auto guitar = load (fm, trackPath);
    check (hub != nullptr && vocal != nullptr && guitar != nullptr, "VST3 binaries load");
    if (failures > 0)
        return 1;

    check (guitar->getLatencySamples() == (int) std::lround (0.030 * kRate), "Track reports 30 ms latency");

    const auto vocalId = stateOf (*vocal).getProperty ("instanceId").toString();
    const auto guitarId = stateOf (*guitar).getProperty ("instanceId").toString();
    check (vocalId.isNotEmpty() && vocalId != guitarId, "every instance gets its own id");

    Source vocalSrc (1, 0.03f, true), guitarSrc (2, 0.25f, false);

    // 1. No reference -> nothing happens.
    {
        Measure m;
        run (2.0, *guitar, vocal.get(), hub.get(), guitarSrc, &vocalSrc, m, 1.0);
        check (std::abs (m.drop2k()) < 0.3, "without a reference the track is untouched (2 kHz "
                                                + juce::String (m.drop2k(), 2) + " dB)");
    }

    // 2. Guitar yields to vocal: 2 kHz goes down in the Mid, 200 Hz stays.
    auto guitarState = stateOf (*guitar);
    guitarState.setProperty ("referenceId", vocalId, nullptr);
    guitarState.setProperty ("referenceName", "Vocal", nullptr);
    setState (*guitar, guitarState);
    {
        Measure m;
        run (4.0, *guitar, vocal.get(), hub.get(), guitarSrc, &vocalSrc, m, 2.0);
        check (m.drop2k() < -2.0, "guitar yields at 2 kHz to the vocal (" + juce::String (m.drop2k(), 2) + " dB in the Mid)");
        check (std::abs (m.drop200()) < 1.0, "guitar keeps its low end (200 Hz " + juce::String (m.drop200(), 2) + " dB)");
    }

    // 3. Hub Total Amount = 0 -> the release goes away.
    if (auto* total = findParam (*hub, "Total Amount"))
    {
        total->setValueNotifyingHost (0.0f);
        Measure m;
        run (4.0, *guitar, vocal.get(), hub.get(), guitarSrc, &vocalSrc, m, 3.0);
        check (std::abs (m.drop2k()) < 0.5, "Hub Total Amount 0% switches release off (" + juce::String (m.drop2k(), 2) + " dB)");
        total->setValueNotifyingHost (0.5f);   // back to 100 %
    }
    else
    {
        check (false, "Hub exposes Total Amount");
    }

    // 4. Hub compare A bypasses every Track.
    if (auto* compare = findParam (*hub, "Compare A (all off)"))
    {
        compare->setValueNotifyingHost (1.0f);
        Measure m;
        run (4.0, *guitar, vocal.get(), hub.get(), guitarSrc, &vocalSrc, m, 3.0);
        check (std::abs (m.drop2k()) < 0.5, "Hub compare A bypasses tracks (" + juce::String (m.drop2k(), 2) + " dB)");
        compare->setValueNotifyingHost (0.0f);
    }

    // 5. Reference in another process (DAWs that sandbox plug-ins).
    vocal.reset();
    {
        juce::ChildProcess child;
        const auto idFile = juce::File::createTempFile ("ffid");
        const auto self = juce::File::getSpecialLocation (juce::File::currentExecutableFile).getFullPathName();
        juce::StringArray cmd { self, "--serve-reference", trackPath, "10", idFile.getFullPathName() };
        check (child.start (cmd, 0), "start reference in a child process");

        juce::String remoteId;
        const auto t0 = juce::Time::getMillisecondCounter();
        while (remoteId.isEmpty() && juce::Time::getMillisecondCounter() - t0 < 15000)
        {
            juce::Thread::sleep (50);
            remoteId = idFile.loadFileAsString().trim();
        }
        idFile.deleteFile();
        check (remoteId.isNotEmpty(), "child reports its instance id");

        auto st = stateOf (*guitar);
        st.setProperty ("referenceId", remoteId, nullptr);
        setState (*guitar, st);

        // Real-time pacing so the child's analysis keeps up.
        Measure m;
        juce::AudioBuffer<float> g (2, kBlock), dry (2, kBlock);
        juce::MidiBuffer midi;
        const int blocks = (int) (5.0 * kRate / kBlock);
        for (int b = 0; b < blocks; ++b)
        {
            const bool measure = b > blocks / 2;
            guitarSrc.fill (g);
            dry.makeCopyOf (g);
            guitar->processBlock (g, midi);
            m.in2k.add (dry, measure);
            m.out2k.add (g, measure);
            juce::Thread::sleep (8);
            if (b % 8 == 0)
                juce::MessageManager::getInstance()->runDispatchLoopUntil (1);
        }
        check (m.drop2k() < -6.0, "guitar yields to a reference in another process (" + juce::String (m.drop2k(), 2) + " dB)");
        child.kill();
    }

    guitar.reset();
    hub.reset();
    std::printf ("%d failures\n", failures);
    return failures == 0 ? 0 : 1;
}
