// FreqFreaks — frequency-unmasking plugin suite
// Copyright (C) 2026 FreqFreaks contributors
// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <juce_audio_processors/juce_audio_processors.h>

#include "ff/SharedBus.h"
#include "ff/TrackEngine.h"

#include <atomic>
#include <vector>

namespace ff
{

namespace trackParams
{
    inline constexpr const char* amount      = "amount";
    inline constexpr const char* space       = "space";
    inline constexpr const char* link        = "link";
    inline constexpr const char* speed       = "speed";
    inline constexpr const char* eqDepth     = "eqDepth";
    inline constexpr const char* monoSafety  = "monoSafety";
    inline constexpr const char* bassMono    = "bassMono";
    inline constexpr const char* rangeLow    = "rangeLow";
    inline constexpr const char* rangeHigh   = "rangeHigh";
    inline constexpr const char* sensitivity = "sensitivity";
    inline constexpr const char* spaceMode   = "spaceMode";
    inline constexpr const char* panSide     = "panSide";
    inline constexpr const char* bypass      = "bypass";
    inline constexpr const char* output      = "output";
    inline constexpr const char* autoGain    = "autoGain";
    inline constexpr const char* spaceComp   = "spaceComp";
}

/** FreqFreaks Track: sits on each track, yields room to its reference track.

    Talks to the other instances only through the SharedBus: it publishes its own
    analysis and budget, and reads the reference track's analysis and the Hub's
    multipliers. */
class TrackProcessor : public juce::AudioProcessor,
                       private juce::Timer
{
public:
    TrackProcessor();
    ~TrackProcessor() override;

    //==============================================================================
    void prepareToPlay (double sampleRate, int samplesPerBlock) override;
    void releaseResources() override {}
    bool isBusesLayoutSupported (const BusesLayout&) const override;
    void processBlock (juce::AudioBuffer<float>&, juce::MidiBuffer&) override;
    using AudioProcessor::processBlock;

    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override                        { return true; }

    const juce::String getName() const override            { return "FreqFreaks Track"; }
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

    void updateTrackProperties (const TrackProperties&) override;

    //==============================================================================
    juce::AudioProcessorValueTreeState& state() noexcept   { return parameters; }
    SharedBus& sharedBus() noexcept                        { return bus; }
    std::uint64_t instanceId() const noexcept              { return bus.instanceId(); }

    /** Reference track (0 = none). */
    void setReference (std::uint64_t id, const juce::String& name);
    std::uint64_t referenceId() const noexcept             { return reference.load(); }
    juce::String referenceName() const;

    juce::String trackName() const;
    juce::Colour trackColour() const;

    /** UI level: 0 Simple, 1 Detail, 2 Expert. Stored with the session. */
    int uiLevel() const noexcept                           { return level.load(); }
    void setUiLevel (int l) noexcept                       { level = juce::jlimit (0, 2, l); }

    bool isHubBypassing() const noexcept                   { return hubBypass.load(); }

    /** Exact cut curve of this track's filters, for the editor (message thread). */
    const BellBank& bellBank() const noexcept              { return displayBank; }

private:
    static juce::AudioProcessorValueTreeState::ParameterLayout createLayout();
    void timerCallback() override;
    void claimSlot (std::uint64_t preferredId);
    void refreshBusName();
    TrackParams readParams() const noexcept;

    juce::AudioProcessorValueTreeState parameters;
    SharedBus bus;
    TrackEngine engine;
    BellBank displayBank;   // same layout as the engine's, prepared on the message thread

    std::atomic<std::uint64_t> reference { 0 };
    std::atomic<int> level { 1 };
    std::atomic<bool> hubBypass { false };

    // Name/colour come from the message thread; the audio thread copies them with a try-lock.
    mutable juce::SpinLock identityLock;
    juce::String name, refName;
    juce::uint32 colourArgb = 0;
    char nameForBus[bus::kNameLength] {};

    bus::TrackData publishData;
    ReferenceSnapshot lastReference;           // audio thread only
    std::uint64_t lastReferenceId = 0, lastReferenceMs = 0;
    std::vector<float> monoScratch;

    struct Raw
    {
        std::atomic<float>* amount = nullptr;
        std::atomic<float>* space = nullptr;
        std::atomic<float>* link = nullptr;
        std::atomic<float>* speed = nullptr;
        std::atomic<float>* eqDepth = nullptr;
        std::atomic<float>* monoSafety = nullptr;
        std::atomic<float>* bassMono = nullptr;
        std::atomic<float>* rangeLow = nullptr;
        std::atomic<float>* rangeHigh = nullptr;
        std::atomic<float>* sensitivity = nullptr;
        std::atomic<float>* spaceMode = nullptr;
        std::atomic<float>* panSide = nullptr;
        std::atomic<float>* bypass = nullptr;
        std::atomic<float>* output = nullptr;
        std::atomic<float>* autoGain = nullptr;
        std::atomic<float>* spaceComp = nullptr;
    } raw;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (TrackProcessor)
};

} // namespace ff
