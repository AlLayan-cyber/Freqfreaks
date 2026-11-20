# FreqFreaks plug-ins: Track (on every track) and Hub (on the master).

set (FREQFREAKS_FORMATS VST3 Standalone)
if (APPLE)
    list (APPEND FREQFREAKS_FORMATS AU)
endif()
if (UNIX AND NOT APPLE)
    list (APPEND FREQFREAKS_FORMATS LV2)
endif()

set (FREQFREAKS_COMMON_SOURCES
    ${CMAKE_CURRENT_SOURCE_DIR}/common/BandViews.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/common/LookAndFeel.cpp)

function (freqfreaks_configure target)
    target_compile_definitions (${target} PUBLIC
        JUCE_WEB_BROWSER=0
        JUCE_USE_CURL=0
        JUCE_VST3_CAN_REPLACE_VST2=0
        JUCE_DISPLAY_SPLASH_SCREEN=0)
    target_include_directories (${target} PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
    target_link_libraries (${target}
        PRIVATE
            freqfreaks_core
            juce::juce_audio_utils
            juce::juce_dsp
        PUBLIC
            juce::juce_recommended_config_flags
            juce::juce_recommended_lto_flags
            juce::juce_recommended_warning_flags)
endfunction()

juce_add_plugin (FreqFreaksTrack
    COMPANY_NAME "FreqFreaks"
    COMPANY_WEBSITE "https://github.com/AlLayan-cyber/Freqfreaks"
    BUNDLE_ID "org.freqfreaks.track"
    PLUGIN_MANUFACTURER_CODE Frqf
    PLUGIN_CODE FfTr
    PRODUCT_NAME "FreqFreaks Track"
    DESCRIPTION "Frees frequencies for a reference track"
    IS_SYNTH FALSE
    NEEDS_MIDI_INPUT FALSE
    NEEDS_MIDI_OUTPUT FALSE
    IS_MIDI_EFFECT FALSE
    EDITOR_WANTS_KEYBOARD_FOCUS FALSE
    VST3_CATEGORIES Fx EQ
    AU_MAIN_TYPE kAudioUnitType_Effect
    LV2URI "https://github.com/AlLayan-cyber/Freqfreaks/track"
    FORMATS ${FREQFREAKS_FORMATS})

target_sources (FreqFreaksTrack PRIVATE
    track/TrackEntry.cpp
    track/TrackProcessor.cpp
    track/TrackEditor.cpp
    ${FREQFREAKS_COMMON_SOURCES})
freqfreaks_configure (FreqFreaksTrack)

juce_add_plugin (FreqFreaksHub
    COMPANY_NAME "FreqFreaks"
    COMPANY_WEBSITE "https://github.com/AlLayan-cyber/Freqfreaks"
    BUNDLE_ID "org.freqfreaks.hub"
    PLUGIN_MANUFACTURER_CODE Frqf
    PLUGIN_CODE FfHb
    PRODUCT_NAME "FreqFreaks Hub"
    DESCRIPTION "Controls every FreqFreaks Track from the master"
    IS_SYNTH FALSE
    NEEDS_MIDI_INPUT FALSE
    NEEDS_MIDI_OUTPUT FALSE
    IS_MIDI_EFFECT FALSE
    EDITOR_WANTS_KEYBOARD_FOCUS FALSE
    VST3_CATEGORIES Fx Analyzer
    AU_MAIN_TYPE kAudioUnitType_Effect
    LV2URI "https://github.com/AlLayan-cyber/Freqfreaks/hub"
    FORMATS ${FREQFREAKS_FORMATS})

target_sources (FreqFreaksHub PRIVATE
    hub/HubEntry.cpp
    hub/HubProcessor.cpp
    hub/HubEditor.cpp
    ${FREQFREAKS_COMMON_SOURCES})
freqfreaks_configure (FreqFreaksHub)
