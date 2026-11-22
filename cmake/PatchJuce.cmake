# Small, idempotent fix to the JUCE 8.0.9 VST3 wrapper, applied at configure time.
#
# The host tells a VST3 plug-in its track name and colour (IInfoListener::setChannelContextInfos).
# Some hosts (Studio One) send it right after creating the plug-in, before JUCE has connected
# the processor; JUCE then drops it, and the plug-in only learns the name when the track is
# renamed. The patch keeps what arrived early and hands it over once the processor exists.

function (freqfreaks_patch_juce juceDir)
    set (file "${juceDir}/modules/juce_audio_plugin_client/juce_audio_plugin_client_VST3.cpp")
    if (NOT EXISTS "${file}")
        message (WARNING "FreqFreaks: JUCE VST3 wrapper not found, early track names are not kept")
        return()
    endif()
    file (READ "${file}" text)
    if (text MATCHES "freqfreaksPendingTrackProperties")
        return()   # already patched
    endif()

    set (anchorInfo "    tresult PLUGIN_API setChannelContextInfos (Vst::IAttributeList* list) override\n    {\n")
    set (anchorInstall "    void installAudioProcessor (const VSTComSmartPtr<JuceAudioProcessor>& newAudioProcessor)\n    {\n        audioProcessor = newAudioProcessor;\n")
    string (FIND "${text}" "${anchorInfo}" atInfo)
    string (FIND "${text}" "${anchorInstall}" atInstall)
    if (atInfo EQUAL -1 OR atInstall EQUAL -1)
        message (WARNING "FreqFreaks: JUCE VST3 wrapper changed, early track names are not kept")
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

    string (REPLACE "${anchorInfo}" "${anchorInfo}${early}" text "${text}")
    string (REPLACE "${anchorInstall}" "${member}${anchorInstall}${handOver}" text "${text}")
    file (WRITE "${file}" "${text}")
    message (STATUS "FreqFreaks: patched the JUCE VST3 wrapper (keeps early track names)")
endfunction()
