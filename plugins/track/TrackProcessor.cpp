// FreqFreaks — frequency-unmasking plugin suite
// Copyright (C) 2026 FreqFreaks contributors
// SPDX-License-Identifier: GPL-3.0-or-later
#include "TrackProcessor.h"
#include "TrackEditor.h"
#include "../common/Theme.h"

#include <cstring>

namespace ff
{

namespace
{
    juce::String idToString (std::uint64_t id)   { return juce::String::toHexString ((juce::int64) id); }
    std::uint64_t idFromString (const juce::String& s) { return (std::uint64_t) s.getHexValue64(); }

    juce::NormalisableRange<float> logRange (float lo, float hi)
    {
        juce::NormalisableRange<float> r (lo, hi);
        r.setSkewForCentre (std::sqrt (lo * hi));
        return r;
    }
}

TrackProcessor::TrackProcessor()
    : AudioProcessor (BusesProperties().withInput ("Input", juce::AudioChannelSet::stereo(), true)
                                       .withOutput ("Output", juce::AudioChannelSet::stereo(), true)),
      parameters (*this, nullptr, "FreqFreaksTrack", createLayout())
{
    using namespace trackParams;
    raw.amount      = parameters.getRawParameterValue (amount);
    raw.space       = parameters.getRawParameterValue (space);
    raw.link        = parameters.getRawParameterValue (link);
    raw.speed       = parameters.getRawParameterValue (speed);
    raw.eqDepth     = parameters.getRawParameterValue (eqDepth);
    raw.monoSafety  = parameters.getRawParameterValue (monoSafety);
    raw.bassMono    = parameters.getRawParameterValue (bassMono);
    raw.rangeLow    = parameters.getRawParameterValue (rangeLow);
    raw.rangeHigh   = parameters.getRawParameterValue (rangeHigh);
    raw.sensitivity = parameters.getRawParameterValue (sensitivity);
    raw.spaceMode   = parameters.getRawParameterValue (spaceMode);
    raw.panSide     = parameters.getRawParameterValue (panSide);
    raw.bypass      = parameters.getRawParameterValue (bypass);
    raw.output      = parameters.getRawParameterValue (output);
    raw.autoGain    = parameters.getRawParameterValue (autoGain);
    raw.spaceComp   = parameters.getRawParameterValue (spaceComp);
    displayBank.prepare (48000.0);

    claimSlot (SharedBus::makeInstanceId());
    refreshBusName();
    startTimerHz (10);
}

TrackProcessor::~TrackProcessor()
{
    stopTimer();
    bus.release();
}

juce::AudioProcessorValueTreeState::ParameterLayout TrackProcessor::createLayout()
{
    using namespace juce;
    using namespace trackParams;
    AudioProcessorValueTreeState::ParameterLayout layout;

    auto percent = [] (const String& id, const String& label, float max, float def)
    {
        return std::make_unique<AudioParameterFloat> (ParameterID { id, 1 }, label, NormalisableRange<float> (0.0f, max, 0.1f), def,
                                                      AudioParameterFloatAttributes().withLabel ("%")
                                                          .withStringFromValueFunction ([] (float v, int) { return String (v, 0) + " %"; }));
    };
    auto decibels = [] (const String& id, const String& label, float max, float def)
    {
        return std::make_unique<AudioParameterFloat> (ParameterID { id, 1 }, label, NormalisableRange<float> (0.0f, max, 0.1f), def,
                                                      AudioParameterFloatAttributes().withLabel ("dB")
                                                          .withStringFromValueFunction ([] (float v, int) { return String (v, 1) + " dB"; }));
    };
    auto hertz = [] (const String& id, const String& label, float lo, float hi, float def)
    {
        return std::make_unique<AudioParameterFloat> (ParameterID { id, 1 }, label, logRange (lo, hi), def,
                                                      AudioParameterFloatAttributes().withLabel ("Hz")
                                                          .withStringFromValueFunction ([] (float v, int)
                                                          { return v >= 1000.0f ? String (v / 1000.0f, 1) + " kHz" : String (v, 0) + " Hz"; }));
    };

    layout.add (percent (amount, "Amount", 100.0f, 70.0f));
    layout.add (percent (space, "Space Amount", 200.0f, 100.0f));
    layout.add (std::make_unique<AudioParameterBool> (ParameterID { link, 1 }, "Link", true));
    layout.add (percent (speed, "Speed (Rider - Duck)", 100.0f, 30.0f));
    layout.add (decibels (eqDepth, "EQ Depth", 12.0f, 6.0f));
    layout.add (decibels (monoSafety, "Mono Safety", 12.0f, 6.0f));
    layout.add (hertz (bassMono, "Bass Mono", 40.0f, 400.0f, 150.0f));
    layout.add (hertz (rangeLow, "Range Low", 20.0f, 1000.0f, 60.0f));
    layout.add (hertz (rangeHigh, "Range High", 2000.0f, 20000.0f, 14000.0f));
    layout.add (decibels (sensitivity, "Sensitivity", 12.0f, 6.0f));
    layout.add (std::make_unique<AudioParameterChoice> (ParameterID { spaceMode, 1 }, "Space Mode",
                                                        StringArray { "Spread", "Pan Away", "Decorrelate" }, 0));
    layout.add (std::make_unique<AudioParameterChoice> (ParameterID { panSide, 1 }, "Pan Away Side",
                                                        StringArray { "Auto", "Left", "Right" }, 0));
    layout.add (std::make_unique<AudioParameterBool> (ParameterID { bypass, 1 }, "Bypass", false));
    layout.add (std::make_unique<AudioParameterFloat> (ParameterID { output, 1 }, "Output", NormalisableRange<float> (-12.0f, 12.0f, 0.1f), 0.0f,
                                                       AudioParameterFloatAttributes().withLabel ("dB")
                                                           .withStringFromValueFunction ([] (float v, int) { return (v > 0.05f ? "+" : "") + String (v, 1) + " dB"; })));
    layout.add (std::make_unique<AudioParameterBool> (ParameterID { autoGain, 1 }, "Auto Gain", false));
    layout.add (std::make_unique<AudioParameterBool> (ParameterID { spaceComp, 1 }, "Space Compensation", true));
    return layout;
}

void TrackProcessor::claimSlot (std::uint64_t preferredId)
{
    if (bus.claim (preferredId))
        return;
    // The id is taken by a live instance (duplicated track) or the bus is full.
    for (int attempt = 0; attempt < 4; ++attempt)
        if (bus.claim (SharedBus::makeInstanceId()))
            return;
}

//==============================================================================
bool TrackProcessor::isBusesLayoutSupported (const BusesLayout& layouts) const
{
    const auto out = layouts.getMainOutputChannelSet();
    if (out != juce::AudioChannelSet::stereo() && out != juce::AudioChannelSet::mono())
        return false;
    return layouts.getMainInputChannelSet() == out;
}

void TrackProcessor::prepareToPlay (double sampleRate, int samplesPerBlock)
{
    engine.prepare (sampleRate, samplesPerBlock);
    displayBank.prepare (sampleRate);
    monoScratch.assign ((size_t) juce::jmax (1, samplesPerBlock), 0.0f);
    setLatencySamples (engine.latencySamples());
}

TrackParams TrackProcessor::readParams() const noexcept
{
    TrackParams p;
    p.amount        = raw.amount->load() / 100.0f;
    p.spaceAmount   = raw.space->load() / 100.0f;
    p.link          = raw.link->load() > 0.5f;
    p.speed         = raw.speed->load() / 100.0f;
    p.eqDepthDb     = raw.eqDepth->load();
    p.monoSafetyDb  = raw.monoSafety->load();
    p.bassMonoHz    = raw.bassMono->load();
    p.rangeLowHz    = raw.rangeLow->load();
    p.rangeHighHz   = raw.rangeHigh->load();
    p.sensitivityDb = raw.sensitivity->load();
    p.spaceMode     = (SpaceMode) juce::jlimit (0, 2, (int) raw.spaceMode->load());
    p.bypass        = raw.bypass->load() > 0.5f;
    p.outputDb      = raw.output->load();
    p.autoGain      = raw.autoGain->load() > 0.5f;
    p.spaceComp     = raw.spaceComp->load() > 0.5f;

    const int side = (int) raw.panSide->load();
    p.panSign = side == 1 ? 1.0f : side == 2 ? -1.0f : ((instanceId() & 1u) ? 1.0f : -1.0f);
    return p;
}

void TrackProcessor::processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer&)
{
    juce::ScopedNoDenormals noDenormals;
    const int numSamples = buffer.getNumSamples();
    if (numSamples == 0)
        return;

    auto params = readParams();

    bus::HubData& hub = hubScratch;
    float trim = 1.0f;
    bool hubOff = false;
    if (bus.readHub (hub))
    {
        const auto mine = hub.trimFor (instanceId());
        trim = juce::jlimit (0.0f, 1.5f, mine.trim);
        params.hubAmount = hub.totalAmount * trim;
        params.hubSpace = hub.spaceAmount;
        hubOff = (hub.flags & (bus::hubCompareA | bus::hubBypassAll)) != 0 || (mine.flags & 1u) != 0;
    }
    hubBypass = hubOff;
    hubTrim = trim;
    params.bypass = params.bypass || hubOff;

    ReferenceSnapshot ref;
    const auto refId = reference.load();
    bool referenceLost = false;
    if (refId != 0)
    {
        bus::TrackData refData;
        bool stale = true;
        const bool readOk = bus.read (refId, refData, &stale);
        const auto now = SharedBus::nowMs();

        if (readOk && ! stale)
        {
            ref.valid = true;
            std::copy (std::begin (refData.levelMidDb), std::end (refData.levelMidDb), ref.midDb.begin());
            lastReference = ref;
            lastReferenceId = refId;
            lastReferenceMs = now;
        }
        else if (! readOk && lastReferenceId == refId && now - lastReferenceMs < bus::kStaleAfterMs)
        {
            // The reference was being written at that very moment: keep using its last analysis.
            ref = lastReference;
        }
        else
        {
            referenceLost = true;
        }
    }

    float* left = buffer.getWritePointer (0);
    float* right = buffer.getNumChannels() > 1 ? buffer.getWritePointer (1) : nullptr;

    if (right != nullptr)
    {
        engine.process (left, right, numSamples, params, ref);
    }
    else
    {
        // Mono track: Space has no Side to use, so run the chain on a dual-mono copy and fold back.
        params.spaceAmount = 0.0f;
        const int chunk = (int) monoScratch.size();
        for (int start = 0; start < numSamples; start += chunk)
        {
            const int n = juce::jmin (chunk, numSamples - start);
            float* l = left + start;
            float* copy = monoScratch.data();
            std::copy (l, l + n, copy);
            engine.process (l, copy, n, params, ref);
            for (int i = 0; i < n; ++i)
                l[i] = 0.5f * (l[i] + copy[i]);
        }
    }

    // Publish analysis and budget for the reference users and the Hub.
    auto& d = publishData;
    d.kind = bus::Kind::track;
    {
        const juce::SpinLock::ScopedTryLockType lock (identityLock);
        if (lock.isLocked())
        {
            std::memcpy (d.name, nameForBus, sizeof (d.name));
            d.colourArgb = colourArgb;
        }
    }
    d.references[0] = refId;
    const auto& budget = engine.budget();
    for (int b = 0; b < kNumBands; ++b)
    {
        const auto i = (size_t) b;
        d.levelMidDb[b]  = engine.ownMidDb()[i];
        d.levelSideDb[b] = engine.ownSideDb()[i];
        d.totalDb[b]     = budget.total[i];
        d.spaceDb[b]     = budget.space[i];
        d.eqDb[b]        = budget.eq[i];
        d.restDb[b]      = budget.rest[i];
    }
    for (int k = 0; k < kNumBells; ++k)
    {
        d.eqBells[k] = engine.processor().currentEqCut()[(size_t) k];
        d.spaceBells[k] = engine.processor().currentSpaceCut()[(size_t) k];
    }
    d.amount = params.amount;
    d.spaceAmount = params.spaceAmount;
    d.compensationDb = engine.compensationDb();
    d.spaceMode = (std::uint32_t) params.spaceMode;
    d.hubTrim = trim;
    d.flags = (params.bypass ? bus::flagBypassed : 0u) | (referenceLost ? bus::flagReferenceLost : 0u)
            | (params.link ? 0u : bus::flagLinkOff) | (hubOff ? bus::flagHubBypassed : 0u);
    bus.publish (d);
}

void TrackProcessor::timerCallback()
{
    // Keeps us visible while the host is not running the audio callback.
    bus.heartbeat();
    applyRemoteCommand();
}

void TrackProcessor::applyRemoteCommand()
{
    // The Hub's panel changes this track's own parameters, so the knobs move and the DAW
    // can record them. Runs on the message thread.
    bus::RemoteCommand cmd;
    if (! bus.readRemote (cmd, lastRemoteSerial))
        return;
    lastRemoteSerial = cmd.serial;

    auto set = [this] (const char* id, float plainValue) {
        if (auto* prm = parameters.getParameter (id))
        {
            prm->beginChangeGesture();
            prm->setValueNotifyingHost (prm->convertTo0to1 (plainValue));
            prm->endChangeGesture();
        }
    };

    if (cmd.mask & bus::remoteAmount)     set (trackParams::amount, juce::jlimit (0.0f, 100.0f, cmd.amount * 100.0f));
    if (cmd.mask & bus::remoteSpace)      set (trackParams::space, juce::jlimit (0.0f, 200.0f, cmd.spaceAmount * 100.0f));
    if (cmd.mask & bus::remoteLink)       set (trackParams::link, cmd.link != 0 ? 1.0f : 0.0f);
    if (cmd.mask & bus::remoteSpaceMode)  set (trackParams::spaceMode, (float) juce::jlimit (0u, 2u, cmd.spaceMode));
    if (cmd.mask & bus::remoteReference)
    {
        juce::String newRefName;
        bus::TrackData refData;
        if (cmd.referenceId != 0 && bus.read (cmd.referenceId, refData))
            newRefName = juce::String::fromUTF8 (refData.name);
        setReference (cmd.referenceId, newRefName);
    }
}

//==============================================================================
void TrackProcessor::setReference (std::uint64_t id, const juce::String& newName)
{
    reference = id;
    const juce::SpinLock::ScopedLockType lock (identityLock);
    refName = newName;
}

juce::String TrackProcessor::referenceName() const
{
    const juce::SpinLock::ScopedLockType lock (identityLock);
    return refName;
}

juce::String TrackProcessor::trackName() const
{
    const juce::SpinLock::ScopedLockType lock (identityLock);
    return name.isNotEmpty() ? name : "Track " + idToString (instanceId()).substring (0, 4).toUpperCase();
}

juce::Colour TrackProcessor::trackColour() const
{
    const juce::SpinLock::ScopedLockType lock (identityLock);
    return theme::trackColour (colourArgb, instanceId());
}

void TrackProcessor::updateTrackProperties (const TrackProperties& properties)
{
    {
        const juce::SpinLock::ScopedLockType lock (identityLock);
        if (properties.name.has_value())
            name = *properties.name;
        if (properties.colour.has_value())
            colourArgb = properties.colour->getARGB();
    }
    refreshBusName();
}

void TrackProcessor::refreshBusName()
{
    const auto shown = trackName();
    const juce::SpinLock::ScopedLockType lock (identityLock);
    std::memset (nameForBus, 0, sizeof (nameForBus));
    shown.copyToUTF8 (nameForBus, sizeof (nameForBus) - 1);
}

//==============================================================================
void TrackProcessor::getStateInformation (juce::MemoryBlock& destData)
{
    auto tree = parameters.copyState();
    tree.setProperty ("instanceId", idToString (instanceId()), nullptr);
    tree.setProperty ("referenceId", idToString (reference.load()), nullptr);
    tree.setProperty ("referenceName", referenceName(), nullptr);
    tree.setProperty ("uiLevel", level.load(), nullptr);
    {
        const juce::SpinLock::ScopedLockType lock (identityLock);
        tree.setProperty ("trackName", name, nullptr);
    }

    if (auto xml = tree.createXml())
        copyXmlToBinary (*xml, destData);
}

void TrackProcessor::setStateInformation (const void* data, int sizeInBytes)
{
    auto xml = getXmlFromBinary (data, sizeInBytes);
    if (xml == nullptr || ! xml->hasTagName (parameters.state.getType()))
        return;

    auto tree = juce::ValueTree::fromXml (*xml);
    parameters.replaceState (tree);

    // Keep the saved id so other tracks' references to us survive a reload.
    const auto savedId = idFromString (tree.getProperty ("instanceId").toString());
    if (savedId != 0 && savedId != instanceId())
    {
        claimSlot (savedId);
        refreshBusName();
    }

    setReference (idFromString (tree.getProperty ("referenceId").toString()), tree.getProperty ("referenceName").toString());
    setUiLevel ((int) tree.getProperty ("uiLevel", 1));

    const auto savedName = tree.getProperty ("trackName").toString();
    if (savedName.isNotEmpty())
    {
        TrackProperties props;
        props.name = savedName;
        updateTrackProperties (props);
    }
}

juce::AudioProcessorEditor* TrackProcessor::createEditor()
{
    return new TrackEditor (*this);
}

} // namespace ff
