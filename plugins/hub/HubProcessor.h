// FreqFreaks — frequency-unmasking plugin suite
// Copyright (C) 2026 FreqFreaks contributors
// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <juce_audio_processors/juce_audio_processors.h>

#include "ff/SharedBus.h"

#include <atomic>

namespace ff
{

namespace hubParams
{
    inline constexpr const char* totalAmount = "totalAmount";
    inline constexpr const char* spaceAmount = "spaceAmount";
    inline constexpr const char* compareA    = "compareA";
}

/** FreqFreaks Hub: goes on the master. Passes audio through untouched, measures
    mono compatibility, and publishes global multipliers to every Track. */
class HubProcessor : public juce::AudioProcessor,
                     private juce::Timer
{
public:
    HubProcessor();
    ~HubProcessor() override;

    void prepareToPlay (double sampleRate, int samplesPerBlock) override;
    void releaseResources() override {}
    bool isBusesLayoutSupported (const BusesLayout&) const override;
    void processBlock (juce::AudioBuffer<float>&, juce::MidiBuffer&) override;
    using AudioProcessor::processBlock;

    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override                        { return true; }

    const juce::String getName() const override            { return "FreqFreaks Hub"; }
    bool acceptsMidi() const override                      { return false; }
    bool producesMidi() const override                     { return false; }
    bool isMidiEffect() const override                     { return false; }
    double getTailLengthSeconds() const override           { return 0.0; }

    int getNumPrograms() override                          { return 1; }
    int getCurrentProgram() override                       { return 0; }
    void setCurrentProgram (int) override                  {}
    const juce::String getProgramName (int) override       { return {}; }
    void changeProgramName (int, const juce::String&) override {}

    void getStateInformation (juce::MemoryBlock&) override;
    void setStateInformation (const void*, int) override;

    juce::AudioProcessorValueTreeState& state() noexcept   { return parameters; }
    SharedBus& sharedBus() noexcept                        { return bus; }

    /** True while another Hub in the project holds the bus. */
    bool isDuplicate() const noexcept                      { return duplicate.load(); }

    /** Share of master energy that survives a mono fold-down, 0..1. */
    float monoCompatibility() const noexcept               { return mono.load(); }

private:
    static juce::AudioProcessorValueTreeState::ParameterLayout createLayout();
    void timerCallback() override;
    bus::HubData currentHubData() const noexcept;

    juce::AudioProcessorValueTreeState parameters;
    SharedBus bus;
    std::uint64_t id = SharedBus::makeInstanceId();
    std::atomic<bool> duplicate { false };
    std::atomic<float> mono { 1.0f };
    float midPower = 0.0f, sidePower = 0.0f, meterCoeff = 0.9f;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (HubProcessor)
};

} // namespace ff
