// FreqFreaks — frequency-unmasking plugin suite
// Copyright (C) 2026 FreqFreaks contributors
// SPDX-License-Identifier: GPL-3.0-or-later
#include "HostTrackInfo.h"

#define JUCE_VST3HEADERS_INCLUDE_HEADERS_ONLY 1
#include <juce_audio_processors/format_types/juce_VST3Headers.h>
#include <juce_audio_processors/format_types/pslextensions/ipslcontextinfo.h>

namespace Presonus
{
    DEF_CLASS_IID (IContextInfoProvider)
}

namespace ff
{

namespace
{
    Presonus::IContextInfoProvider* asProvider (void* p) { return static_cast<Presonus::IContextInfoProvider*> (p); }
}

HostTrackInfo::~HostTrackInfo()
{
    setIComponentHandler (nullptr);
}

void HostTrackInfo::setIComponentHandler (Steinberg::FUnknown* handler)
{
    if (provider != nullptr)
    {
        asProvider (provider)->release();
        provider = nullptr;
    }
    if (handler == nullptr)
        return;

    void* obj = nullptr;
    if (handler->queryInterface (Presonus::IContextInfoProvider::iid, &obj) == Steinberg::kResultOk && obj != nullptr)
        provider = obj;   // queryInterface added the reference we hold
}

bool HostTrackInfo::read (juce::String& name, juce::uint32& argb) const
{
    if (provider == nullptr)
        return false;
    auto* p = asProvider (provider);

    Steinberg::Vst::TChar text[128] {};
    bool any = false;
    if (p->getContextInfoString (text, 128, Presonus::ContextInfo::kName) == Steinberg::kResultOk)
    {
        name = juce::String (juce::CharPointer_UTF16 (reinterpret_cast<juce::CharPointer_UTF16::CharType*> (text)));
        any = name.isNotEmpty();
    }

    Steinberg::int32 colour = 0;
    argb = 0;
    if (p->getContextInfoValue (colour, Presonus::ContextInfo::kColor) == Steinberg::kResultOk && colour != 0)
    {
        // "int32: RGBA": red in the top byte, alpha in the bottom one.
        const auto c = (juce::uint32) colour;
        argb = juce::Colour ((juce::uint8) (c >> 24), (juce::uint8) (c >> 16), (juce::uint8) (c >> 8)).getARGB();
        any = true;
    }
    return any;
}

} // namespace ff
