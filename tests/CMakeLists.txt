add_executable (freqfreaks_core_tests CoreTests.cpp)
target_link_libraries (freqfreaks_core_tests PRIVATE freqfreaks_core)
add_test (NAME core COMMAND freqfreaks_core_tests)

# End-to-end test that hosts the built VST3 plug-ins (needs JUCE).
if (FREQFREAKS_BUILD_PLUGINS)
    juce_add_console_app (freqfreaks_host_test PRODUCT_NAME "freqfreaks_host_test")
    target_sources (freqfreaks_host_test PRIVATE HostTest.cpp)
    target_compile_definitions (freqfreaks_host_test PRIVATE
        JUCE_PLUGINHOST_VST3=1
        JUCE_MODAL_LOOPS_PERMITTED=1
        JUCE_WEB_BROWSER=0
        JUCE_USE_CURL=0)
    target_link_libraries (freqfreaks_host_test PRIVATE
        freqfreaks_core
        juce::juce_audio_processors
        juce::juce_gui_basics
        juce::juce_recommended_config_flags
        juce::juce_recommended_warning_flags)
    add_dependencies (freqfreaks_host_test FreqFreaksTrack_VST3 FreqFreaksHub_VST3)

    add_test (NAME host
        COMMAND freqfreaks_host_test
            "$<TARGET_FILE_DIR:FreqFreaksTrack_VST3>/../../../FreqFreaks Track.vst3"
            "$<TARGET_FILE_DIR:FreqFreaksHub_VST3>/../../../FreqFreaks Hub.vst3")
endif()

# Renders the real editors to PNG after a short demo mix (no DAW needed).
if (FREQFREAKS_BUILD_PLUGINS)
    juce_add_console_app (freqfreaks_snapshot PRODUCT_NAME "freqfreaks_snapshot")
    target_sources (freqfreaks_snapshot PRIVATE
        Snapshot.cpp
        ${PROJECT_SOURCE_DIR}/plugins/track/TrackProcessor.cpp
        ${PROJECT_SOURCE_DIR}/plugins/track/TrackEditor.cpp
        ${PROJECT_SOURCE_DIR}/plugins/track/TimingKnob.cpp
        ${PROJECT_SOURCE_DIR}/plugins/track/HostTrackInfo.cpp
        ${PROJECT_SOURCE_DIR}/plugins/hub/HubProcessor.cpp
        ${PROJECT_SOURCE_DIR}/plugins/hub/HubEditor.cpp
        ${PROJECT_SOURCE_DIR}/plugins/common/BandViews.cpp
        ${PROJECT_SOURCE_DIR}/plugins/common/LookAndFeel.cpp
        ${PROJECT_SOURCE_DIR}/plugins/common/ScalingEditor.cpp)
    target_include_directories (freqfreaks_snapshot PRIVATE ${PROJECT_SOURCE_DIR}/plugins)
    target_compile_definitions (freqfreaks_snapshot PRIVATE JUCE_WEB_BROWSER=0 JUCE_USE_CURL=0)
    target_link_libraries (freqfreaks_snapshot PRIVATE
        freqfreaks_core
        juce::juce_audio_utils
        juce::juce_recommended_config_flags)
endif()

# Studio One's own track-name extension, with a stand-in host (needs JUCE).
if (FREQFREAKS_BUILD_PLUGINS)
    juce_add_console_app (freqfreaks_studio_one_name PRODUCT_NAME "freqfreaks_studio_one_name")
    target_sources (freqfreaks_studio_one_name PRIVATE
        StudioOneNameTest.cpp
        ${PROJECT_SOURCE_DIR}/plugins/track/TrackProcessor.cpp
        ${PROJECT_SOURCE_DIR}/plugins/track/TrackEditor.cpp
        ${PROJECT_SOURCE_DIR}/plugins/track/TimingKnob.cpp
        ${PROJECT_SOURCE_DIR}/plugins/track/HostTrackInfo.cpp
        ${PROJECT_SOURCE_DIR}/plugins/common/BandViews.cpp
        ${PROJECT_SOURCE_DIR}/plugins/common/LookAndFeel.cpp
        ${PROJECT_SOURCE_DIR}/plugins/common/ScalingEditor.cpp)
    target_include_directories (freqfreaks_studio_one_name PRIVATE ${PROJECT_SOURCE_DIR}/plugins)
    target_compile_definitions (freqfreaks_studio_one_name PRIVATE JUCE_WEB_BROWSER=0 JUCE_USE_CURL=0 JUCE_MODAL_LOOPS_PERMITTED=1)
    target_link_libraries (freqfreaks_studio_one_name PRIVATE
        freqfreaks_core
        juce::juce_audio_utils
        juce::juce_recommended_config_flags)
    add_test (NAME studio-one-name COMMAND freqfreaks_studio_one_name)
endif()
