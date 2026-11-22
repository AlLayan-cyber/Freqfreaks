// FreqFreaks — frequency-unmasking plugin suite
// Copyright (C) 2026 FreqFreaks contributors
// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <juce_audio_processors/juce_audio_processors.h>

namespace ff
{

/** Asks the DAW for the track's name and colour through PreSonus's own VST3 extension
    (IContextInfoProvider on the component handler).

    Studio One does not send the track name through the standard VST3 channel-context
    call that JUCE listens to (not even after a rename), but it answers this one. Other
    hosts simply don't offer it, and nothing happens. Message thread only. */
class HostTrackInfo : public juce::VST3ClientExtensions
{
public:
    HostTrackInfo() = default;
    ~HostTrackInfo() override;

    void setIComponentHandler (Steinberg::FUnknown*) override;

    /** True if the host offers the extension at all. */
    bool isAvailable() const noexcept           { return provider != nullptr; }

    /** Reads the current name and colour (0 = not given). False if the host gave neither. */
    bool read (juce::String& name, juce::uint32& argb) const;

private:
    void* provider = nullptr;   // Presonus::IContextInfoProvider*, one reference held

    JUCE_DECLARE_NON_COPYABLE (HostTrackInfo)
};

} // namespace ff
