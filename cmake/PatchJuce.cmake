# Small, idempotent fix to the JUCE 8.0.9 VST3 wrapper, applied at configure time.
#
# 1. The host tells a VST3 plug-in its track name and colour (IInfoListener::setChannelContextInfos).
#    Some hosts send it right after creating the plug-in, before JUCE has connected the
#    processor; JUCE then drops it. The patch keeps what arrived early and hands it over once
#    the processor exists.
# 2. JUCE hands the host's component handler to the plug-in (VST3ClientExtensions) only when
#    the processor is installed. Studio One sets the handler later, so the plug-in never got
#    it and could not ask Studio One for the track name (PreSonus IContextInfoProvider). The
#    patch passes it on every time the host sets it.

function (freqfreaks_patch_juce juceDir)
    set (file "${juceDir}/modules/juce_audio_plugin_client/juce_audio_plugin_client_VST3.cpp")
    if (NOT EXISTS "${file}")
        message (WARNING "FreqFreaks: JUCE VST3 wrapper not found, early track names are not kept")
        return()
    endif()
    file (READ "${file}" text)
    set (original "${text}")

    set (anchorInfo "    tresult PLUGIN_API setChannelContextInfos (Vst::IAttributeList* list) override\n    {\n")
    set (anchorInstall "    void installAudioProcessor (const VSTComSmartPtr<JuceAudioProcessor>& newAudioProcessor)\n    {\n        audioProcessor = newAudioProcessor;\n")
    string (FIND "${text}" "${anchorInfo}" atInfo)
    string (FIND "${text}" "${anchorInstall}" atInstall)
    if (atInfo EQUAL -1 OR atInstall EQUAL -1)
        message (WARNING "FreqFreaks: JUCE VST3 wrapper changed, the track-name patch is not applied")
        return()
    endif()

    set (early [=[
        if (getPluginInstance() == nullptr)
        {
            // FreqFreaks: the host sent the track name before the processor was connected.
            // Keep it and hand it over in installAudioProcessor().
            if (list != nullptr)
            {
                AudioProcessor::TrackProperties properties;
                Vst::String128 channelName;
                if (list->getString (Vst::ChannelContext::kChannelNameKey, channelName, sizeof (channelName)) == kResultTrue)
                    properties.name = std::make_optional (toString (channelName));
                Steinberg::int64 colour;
                if (list->getInt (Vst::ChannelContext::kChannelColorKey, colour) == kResultTrue)
                    properties.colour = std::make_optional (Colour (Vst::ChannelContext::GetRed ((uint32) colour), Vst::ChannelContext::GetGreen ((uint32) colour),
                                                                    Vst::ChannelContext::GetBlue ((uint32) colour), Vst::ChannelContext::GetAlpha ((uint32) colour)));
                freqfreaksPendingTrackProperties = properties;
            }
            return kResultOk;
        }

]=])
    set (handOver [=[
        if (freqfreaksPendingTrackProperties.has_value())
        {
            if (auto* instance = getPluginInstance())
                instance->updateTrackProperties (*freqfreaksPendingTrackProperties);
            freqfreaksPendingTrackProperties.reset();
        }
]=])
    set (member "    std::optional<AudioProcessor::TrackProperties> freqfreaksPendingTrackProperties;   // FreqFreaks\n\n")

    set (handler [=[
    tresult PLUGIN_API setComponentHandler (Vst::IComponentHandler* handler) override
    {
        // FreqFreaks: the host may set its handler after the processor was installed
        // (Studio One does); tell the plug-in every time, not only at install.
        const auto result = Vst::EditController::setComponentHandler (handler);
        if (audioProcessor != nullptr)
            if (auto* extensions = audioProcessor->get()->getVST3ClientExtensions())
                extensions->setIComponentHandler (componentHandler);
        return result;
    }

]=])

    if (NOT text MATCHES "freqfreaksPendingTrackProperties")
        string (REPLACE "${anchorInfo}" "${anchorInfo}${early}" text "${text}")
        string (REPLACE "${anchorInstall}" "${member}${anchorInstall}${handOver}" text "${text}")
    endif()
    if (NOT text MATCHES "FreqFreaks: the host may set its handler")
        string (REPLACE "${anchorInfo}" "${handler}${anchorInfo}" text "${text}")
    endif()
    if (NOT text STREQUAL original)
        file (WRITE "${file}" "${text}")
        message (STATUS "FreqFreaks: patched the JUCE VST3 wrapper (track name from the host)")
    endif()
endfunction()
