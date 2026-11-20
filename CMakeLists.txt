cmake_minimum_required (VERSION 3.22)

project (FreqFreaks VERSION 0.1.0 LANGUAGES C CXX)

set (CMAKE_CXX_STANDARD 17)
set (CMAKE_CXX_STANDARD_REQUIRED ON)

option (FREQFREAKS_BUILD_PLUGINS "Build the Track and Hub plug-ins (needs JUCE)" ON)
option (FREQFREAKS_BUILD_TESTS "Build unit and integration tests" ON)
set (FREQFREAKS_JUCE_PATH "" CACHE PATH "Path to a local JUCE 8 checkout (fetched from GitHub when empty)")

add_subdirectory (core)

if (FREQFREAKS_BUILD_PLUGINS)
    if (FREQFREAKS_JUCE_PATH)
        add_subdirectory ("${FREQFREAKS_JUCE_PATH}" "${CMAKE_BINARY_DIR}/JUCE" EXCLUDE_FROM_ALL)
    else()
        include (FetchContent)
        FetchContent_Declare (JUCE
            GIT_REPOSITORY https://github.com/juce-framework/JUCE.git
            GIT_TAG 8.0.9
            GIT_SHALLOW TRUE)
        FetchContent_MakeAvailable (JUCE)
    endif()

    add_subdirectory (plugins)
endif()

if (FREQFREAKS_BUILD_TESTS)
    enable_testing()
    add_subdirectory (tests)
endif()
