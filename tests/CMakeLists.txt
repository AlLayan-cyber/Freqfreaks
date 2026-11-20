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
