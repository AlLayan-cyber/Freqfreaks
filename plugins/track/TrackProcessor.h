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
    inline constexpr const char* attack      = "attack";
    inline constexpr const char* release     = "release";
    inline constexpr const char* attackManual  = "attackManual";
    inline constexpr const char* releaseManual = "releaseManual";
    inline constexpr const char* sharp       = "sharp";
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
    void processBlockBypassed (juce::AudioBuffer<float>&, juce::MidiBuffer&) override;
    using AudioProcessor::processBlock;
    using AudioProcessor::processBlockBypassed;

    /** The DAW's bypass button drives our own Bypass: it fades out and keeps the latency,
        so switching never shifts the audio by the lookahead (a click). */
    juce::AudioProcessorParameter* getBypassParameter() const override;

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

    /** The name shown everywhere: the one typed in the plug-in, else the DAW's track name,
        else "Track XXXX". */
    juce::String trackName() const;
    /** Same for the colour: picked in the plug-in, else the DAW's, else a stable fallback. */
    juce::Colour trackColour() const;

    /** Name typed in the plug-in; empty = follow the DAW. */
    void setManualName (const juce::String&);
    juce::String manualName() const;
    /** Colour picked in the plug-in; transparent = follow the DAW. */
    void setManualColour (juce::Colour);
    bool hasManualColour() const;

    /** What the DAW told us about the track (for the Expert view). */
    struct HostInfo
    {
        int calls = 0;                 // how many times the DAW sent track properties
        juce::String name;             // empty = never sent
        juce::uint32 colourArgb = 0;   // 0 = never sent
    };
    HostInfo hostInfo() const;

    /** Monitoring: 0 normal, 1 Delta (all), 2 Delta EQ, 3 Delta Space, 4 input,
        5 the reference track alone, 6 this track + the reference. Not saved. */
    enum ListenMode { listenNormal = 0, listenInput = 4, listenReference = 5, listenWithReference = 6, numListenModes = 7 };
    int listenMode() const noexcept                        { return listen.load(); }
    void setListenMode (int m) noexcept                    { listen = juce::jlimit (0, numListenModes - 1, m); }

    /** True while the reference's audio is arriving (for the reference listening modes). */
    bool isReferenceAudioLive() const noexcept             { return referenceAudioLive.load(); }

    /** Editor size, as a scale of the base size (0 = not set yet). Stored with the session. */
    float editorScale() const noexcept                     { return scale.load(); }
    void setEditorScale (float s) noexcept                 { scale = s; }

    /** UI level: 0 Simple, 1 Detail, 2 Expert. Stored with the session. */
    int uiLevel() const noexcept                           { return level.load(); }
    void setUiLevel (int l) noexcept                       { level = juce::jlimit (0, 2, l); }

    bool isHubBypassing() const noexcept                   { return hubBypass.load(); }
    float hubTrimValue() const noexcept                    { return hubTrim.load(); }

    /** Attack / release in use right now (manual, or from Speed when auto), ms. */
    float currentAttackMs() const noexcept                 { return attackInUse.load(); }
    float currentReleaseMs() const noexcept                { return releaseInUse.load(); }

    /** Exact cut curve of this track's filters, for the editor (message thread). */
    const BellBank& bellBank (int sharpStep) const noexcept { return displayBanks[(size_t) juce::jlimit (0, kSharpSteps - 1, sharpStep)]; }

private:
    static juce::AudioProcessorValueTreeState::ParameterLayout createLayout();
    void timerCallback() override;
    void claimSlot (std::uint64_t preferredId);
    void refreshBusName();
    void applyRemoteCommand();
    TrackParams readParams() const noexcept;
    void process (juce::AudioBuffer<float>&, bool forceBypass);

    juce::AudioProcessorValueTreeState parameters;
    SharedBus bus;
    TrackEngine engine;
    std::array<BellBank, kSharpSteps> displayBanks;   // same layouts as the engine's, prepared on the message thread

    std::atomic<std::uint64_t> reference { 0 };
    std::atomic<int> level { 1 }, listen { 0 };
    std::atomic<float> scale { 0.0f };
    std::atomic<bool> hubBypass { false };
    std::atomic<float> hubTrim { 1.0f };
    std::atomic<float> attackInUse { 60.0f }, releaseInUse { 600.0f };
    std::uint32_t lastRemoteSerial = 0;   // message thread
    bus::HubData hubScratch;              // audio thread (too big for the stack)
    bus::HubData lastHub;                 // audio thread: last good Hub read
    std::uint64_t lastHubMs = 0;

    // Name/colour come from the message thread; the audio thread copies them with a try-lock.
    mutable juce::SpinLock identityLock;
    juce::String hostName, typedName, refName;
    juce::uint32 hostColour = 0, typedColour = 0, colourForBus = 0;
    int hostCalls = 0;
    char nameForBus[bus::kNameLength] {};

    bus::TrackData publishData;
    ReferenceSnapshot lastReference;           // audio thread only
    std::uint64_t lastReferenceId = 0, lastReferenceMs = 0;
    std::vector<float> monoScratch;

    // Reference listening (audio thread).
    void mixReference (float* left, float* right, int numSamples, int mode);
    std::vector<float> refLeft, refRight;
    std::uint64_t refReadPos = 0;
    float refGain = 0.0f, trackGain = 1.0f, listenCoeff = 0.999f;
    std::atomic<bool> referenceAudioLive { false };

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
        std::atomic<float>* attack = nullptr;
        std::atomic<float>* release = nullptr;
        std::atomic<float>* attackManual = nullptr;
        std::atomic<float>* releaseManual = nullptr;
        std::atomic<float>* sharp = nullptr;
    } raw;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (TrackProcessor)
};

} // namespace ff
