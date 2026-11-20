// FreqFreaks — frequency-unmasking plugin suite
// Copyright (C) 2026 FreqFreaks contributors
// SPDX-License-Identifier: GPL-3.0-or-later
#include "HubProcessor.h"
#include "HubEditor.h"

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
    return layout;
}

bus::HubData HubProcessor::currentHubData() const noexcept
{
    bus::HubData d;
    d.totalAmount = parameters.getRawParameterValue (hubParams::totalAmount)->load() / 100.0f;
    d.spaceAmount = parameters.getRawParameterValue (hubParams::spaceAmount)->load() / 100.0f;
    d.flags = parameters.getRawParameterValue (hubParams::compareA)->load() > 0.5f ? 1u : 0u;
    return d;
}

void HubProcessor::timerCallback()
{
    if (duplicate.load())
        duplicate = ! bus.claimHub (id);   // take over if the other Hub went away

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
    if (auto xml = parameters.copyState().createXml())
        copyXmlToBinary (*xml, destData);
}

void HubProcessor::setStateInformation (const void* data, int sizeInBytes)
{
    if (auto xml = getXmlFromBinary (data, sizeInBytes))
        if (xml->hasTagName (parameters.state.getType()))
            parameters.replaceState (juce::ValueTree::fromXml (*xml));
}

juce::AudioProcessorEditor* HubProcessor::createEditor()
{
    return new HubEditor (*this);
}

} // namespace ff
