// FreqFreaks — frequency-unmasking plugin suite
// Copyright (C) 2026 FreqFreaks contributors
// SPDX-License-Identifier: GPL-3.0-or-later
//
// A stand-in for Studio One's PreSonus extension (IContextInfoProvider on the component
// handler): the Track plug-in must pick up the track name and colour from it, and follow
// a rename.

#include "track/TrackProcessor.h"

#define JUCE_VST3HEADERS_INCLUDE_HEADERS_ONLY 1
#include <juce_audio_processors/format_types/juce_VST3Headers.h>
#include <juce_audio_processors/format_types/pslextensions/ipslcontextinfo.h>

#include <cstring>

namespace
{
int failures = 0;

void check (bool ok, const juce::String& what)
{
    std::printf ("%s %s\n", ok ? "[ ok ]" : "[FAIL]", what.toRawUTF8());
    if (! ok)
        ++failures;
}

struct FakeStudioOne : public Presonus::IContextInfoProvider
{
    std::u16string name = u"Вокал";   // "Вокал"
    Steinberg::int32 rgba = (Steinberg::int32) 0xfff979e8u;    // pink e879f9: red in the lowest byte, as Studio One sends it
    int refs = 1;

    Steinberg::tresult PLUGIN_API queryInterface (const Steinberg::TUID iid, void** obj) override
    {
        if (Steinberg::FUnknownPrivate::iidEqual (iid, Presonus::IContextInfoProvider::iid))
        {
            addRef();
            *obj = this;
            return Steinberg::kResultOk;
        }
        *obj = nullptr;
        return Steinberg::kNoInterface;
    }
    Steinberg::uint32 PLUGIN_API addRef() override   { return (Steinberg::uint32) ++refs; }
    Steinberg::uint32 PLUGIN_API release() override  { return (Steinberg::uint32) --refs; }

    Steinberg::tresult PLUGIN_API getContextInfoValue (Steinberg::int32& value, Steinberg::FIDString id) override
    {
        if (std::strcmp (id, Presonus::ContextInfo::kColor) != 0)
            return Steinberg::kResultFalse;
        value = rgba;
        return Steinberg::kResultOk;
    }
    Steinberg::tresult PLUGIN_API getContextInfoString (Steinberg::Vst::TChar* string, Steinberg::int32 maxCharCount,
                                                        Steinberg::FIDString id) override
    {
        if (std::strcmp (id, Presonus::ContextInfo::kName) != 0)
            return Steinberg::kResultFalse;
        const auto n = std::min<std::size_t> (name.size(), (std::size_t) maxCharCount - 1);
        std::memcpy (string, name.data(), n * sizeof (char16_t));
        string[n] = 0;
        return Steinberg::kResultOk;
    }
};

void pump (int ms)
{
    juce::MessageManager::getInstance()->runDispatchLoopUntil (ms);
}
} // namespace

int main()
{
    juce::ScopedJuceInitialiser_GUI juce;
    FakeStudioOne host;
    {
        ff::TrackProcessor track;
        check (track.trackName().startsWith ("Track "), "before the host answers: fallback name");

        auto* ext = track.getVST3ClientExtensions();
        check (ext != nullptr, "Track offers VST3 client extensions");
        if (ext == nullptr)
            return 1;
        ext->setIComponentHandler (&host);
        pump (400);

        check (track.trackName() == juce::String::fromUTF8 ("Вокал"),
               "name from Studio One's extension, Cyrillic (got \"" + track.trackName() + "\")");
        check (track.trackColour().getARGB() == 0xffe879f9u,
               "colour from Studio One's extension (got " + track.trackColour().toDisplayString (true) + ")");
        check (track.hostInfo().presonus, "Expert shows the host answers the extension");

        host.name = u"Lead 2";
        pump (400);
        check (track.trackName() == "Lead 2", "a rename in the DAW follows (got \"" + track.trackName() + "\")");

        track.setManualName ("Mine");
        pump (300);
        check (track.trackName() == "Mine", "a name typed in the plug-in wins");
        track.setManualName ({});
        check (track.trackName() == "Lead 2", "clearing it goes back to the DAW's name");
    }
    check (host.refs == 1, "the plug-in releases the host's interface");

    std::printf ("%d failures\n", failures);
    return failures == 0 ? 0 : 1;
}
