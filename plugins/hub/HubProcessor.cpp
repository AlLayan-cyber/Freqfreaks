// FreqFreaks — frequency-unmasking plugin suite
// Copyright (C) 2026 FreqFreaks contributors
// SPDX-License-Identifier: GPL-3.0-or-later
#include "HubProcessor.h"
#include "HubEditor.h"
#include "../common/ScalingEditor.h"

namespace ff
{

HubProcessor::HubProcessor()
    : AudioProcessor (BusesProperties().withInput ("Input", juce::AudioChannelSet::stereo(), true)
                                       .withOutput ("Output", juce::AudioChannelSet::stereo(), true)),
      parameters (*this, nullptr, "FreqFreaksHub", createLayout())
{
    duplicate = ! bus.claimHub (id);
    timerCallback();
    startTimerHz (15);
}

HubProcessor::~HubProcessor()
{
    stopTimer();
    bus.releaseHub();
}

juce::AudioProcessorValueTreeState::ParameterLayout HubProcessor::createLayout()
{
    using namespace juce;
    AudioProcessorValueTreeState::ParameterLayout layout;
    auto percent = [] (const char* pid, const char* name) {
        return std::make_unique<AudioParameterFloat> (ParameterID { pid, 1 }, name, NormalisableRange<float> (0.0f, 200.0f, 0.1f), 100.0f,
                                                      AudioParameterFloatAttributes().withLabel ("%")
                                                          .withStringFromValueFunction ([] (float v, int) { return String (v, 0) + " %"; }));
    };
    layout.add (percent (hubParams::totalAmount, "Total Amount"));
    layout.add (percent (hubParams::spaceAmount, "Space Amount"));
    layout.add (std::make_unique<AudioParameterBool> (ParameterID { hubParams::compareA, 1 }, "Compare A (all off)", false));
    layout.add (std::make_unique<AudioParameterBool> (ParameterID { hubParams::bypassAll, 1 }, "Bypass All", false));
    return layout;
}

bus::HubData& HubProcessor::currentHubData() noexcept
{
    // Called from the audio thread and from the timer: each uses its own scratch copy.
    const bool onMessageThread = juce::MessageManager::existsAndIsCurrentThread();
    auto& d = onMessageThread ? timerScratch : publishScratch;

    d.totalAmount = parameters.getRawParameterValue (hubParams::totalAmount)->load() / 100.0f;
    d.spaceAmount = parameters.getRawParameterValue (hubParams::spaceAmount)->load() / 100.0f;
    d.flags = (parameters.getRawParameterValue (hubParams::compareA)->load() > 0.5f ? bus::hubCompareA : 0u)
            | (parameters.getRawParameterValue (hubParams::bypassAll)->load() > 0.5f ? bus::hubBypassAll : 0u);

    if (onMessageThread)
    {
        const juce::SpinLock::ScopedLockType lock (trimLock);
        std::copy (trims.begin(), trims.end(), d.trims);
    }
    else
    {
        // Never wait on the audio thread: if the table is being edited, use the last copy.
        const juce::SpinLock::ScopedTryLockType lock (trimLock);
        if (lock.isLocked())
            audioTrims = trims;
        std::copy (audioTrims.begin(), audioTrims.end(), d.trims);
    }
    return d;
}

bus::HubTrim* HubProcessor::findOrAddTrim (std::uint64_t trackId)
{
    for (auto& t : trims)
        if (t.id == trackId)
            return &t;
    for (auto& t : trims)
        if (t.id == 0)
        {
            t = {};
            t.id = trackId;
            return &t;
        }

    // Full: reuse an entry of a track that is not live any more.
    const auto live = bus.list();
    for (auto& t : trims)
    {
        const bool alive = std::any_of (live.begin(), live.end(), [&t] (const BusEntry& e) { return e.id == t.id; });
        if (! alive)
        {
            t = {};
            t.id = trackId;
            return &t;
        }
    }
    return nullptr;
}

float HubProcessor::trimFor (std::uint64_t trackId) const
{
    const juce::SpinLock::ScopedLockType lock (trimLock);
    for (const auto& t : trims)
        if (t.id == trackId && trackId != 0)
            return t.trim;
    return 1.0f;
}

bool HubProcessor::isTrackBypassed (std::uint64_t trackId) const
{
    const juce::SpinLock::ScopedLockType lock (trimLock);
    for (const auto& t : trims)
        if (t.id == trackId && trackId != 0)
            return (t.flags & 1u) != 0;
    return false;
}

void HubProcessor::setTrim (std::uint64_t trackId, float trim)
{
    const juce::SpinLock::ScopedLockType lock (trimLock);
    if (auto* t = findOrAddTrim (trackId))
        t->trim = juce::jlimit (0.0f, 1.5f, trim);
}

void HubProcessor::setTrackBypassed (std::uint64_t trackId, bool bypassed)
{
    const juce::SpinLock::ScopedLockType lock (trimLock);
    if (auto* t = findOrAddTrim (trackId))
        t->flags = bypassed ? (t->flags | 1u) : (t->flags & ~1u);
}

void HubProcessor::resetTrims()
{
    const juce::SpinLock::ScopedLockType lock (trimLock);
    trims.fill ({});
}

void HubProcessor::timerCallback()
{
    // Holds the bus, or takes it back: a second Hub instance (a DAW preview, a copy) may have
    // taken it while this one stalled, and released it when it went away.
    duplicate = ! bus.ensureHub (id);

    // Published here (host idle) and from the audio thread (offline render can starve this timer).
    bus.publishHub (currentHubData());
    bus.heartbeat();
}

bool HubProcessor::isBusesLayoutSupported (const BusesLayout& layouts) const
{
    const auto out = layouts.getMainOutputChannelSet();
    return (out == juce::AudioChannelSet::stereo() || out == juce::AudioChannelSet::mono())
           && layouts.getMainInputChannelSet() == out;
}

void HubProcessor::prepareToPlay (double sampleRate, int samplesPerBlock)
{
    // ~300 ms meter integration, applied per block.
    meterCoeff = (float) std::exp (-(double) juce::jmax (1, samplesPerBlock) / (0.3 * sampleRate));
    midPower = sidePower = 0.0f;
}

void HubProcessor::processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer&)
{
    bus.publishHub (currentHubData());

    if (buffer.getNumChannels() < 2 || buffer.getNumSamples() == 0)
        return;

    const float* l = buffer.getReadPointer (0);
    const float* r = buffer.getReadPointer (1);
    double pm = 0.0, ps = 0.0;
    for (int i = 0; i < buffer.getNumSamples(); ++i)
    {
        const float m = 0.5f * (l[i] + r[i]);
        const float s = 0.5f * (l[i] - r[i]);
        pm += (double) (m * m);
        ps += (double) (s * s);
    }
    const auto n = (double) buffer.getNumSamples();
    midPower = (float) (pm / n) + meterCoeff * (midPower - (float) (pm / n));
    sidePower = (float) (ps / n) + meterCoeff * (sidePower - (float) (ps / n));
    if (midPower + sidePower > 1.0e-9f)
        mono = midPower / (midPower + sidePower);
}

void HubProcessor::getStateInformation (juce::MemoryBlock& destData)
{
    auto tree = parameters.copyState();
    juce::ValueTree list ("Trims");
    {
        const juce::SpinLock::ScopedLockType lock (trimLock);
        for (const auto& t : trims)
        {
            if (t.id == 0 || (std::abs (t.trim - 1.0f) < 1.0e-4f && t.flags == 0))
                continue;
            juce::ValueTree item ("Trim");
            item.setProperty ("id", juce::String::toHexString ((juce::int64) t.id), nullptr);
            item.setProperty ("trim", t.trim, nullptr);
            item.setProperty ("bypass", (t.flags & 1u) != 0, nullptr);
            list.appendChild (item, nullptr);
        }
    }
    tree.removeChild (tree.getChildWithName ("Trims"), nullptr);
    tree.setProperty ("uiScale", editorScale.load(), nullptr);
    tree.appendChild (list, nullptr);

    if (auto xml = tree.createXml())
        copyXmlToBinary (*xml, destData);
}

void HubProcessor::setStateInformation (const void* data, int sizeInBytes)
{
    auto xml = getXmlFromBinary (data, sizeInBytes);
    if (xml == nullptr || ! xml->hasTagName (parameters.state.getType()))
        return;

    auto tree = juce::ValueTree::fromXml (*xml);
    const auto list = tree.getChildWithName ("Trims");
    tree.removeChild (list, nullptr);
    editorScale = (float) tree.getProperty ("uiScale", 0.0f);
    parameters.replaceState (tree);

    const juce::SpinLock::ScopedLockType lock (trimLock);
    trims.fill ({});
    size_t n = 0;
    for (const auto& item : list)
    {
        if (n >= trims.size())
            break;
        auto& t = trims[n++];
        t.id = (std::uint64_t) item.getProperty ("id").toString().getHexValue64();
        t.trim = juce::jlimit (0.0f, 1.5f, (float) item.getProperty ("trim", 1.0f));
        t.flags = (bool) item.getProperty ("bypass", false) ? 1u : 0u;
    }
}

juce::AudioProcessorEditor* HubProcessor::createEditor()
{
    return new ScalingEditor (*this, std::make_unique<HubEditor> (*this), HubEditor::kWidth, HubEditor::kHeight,
                              "hubScale", editorScale.load(), [this] (float s) { editorScale = s; });
}

} // namespace ff
