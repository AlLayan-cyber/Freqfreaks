cmake_minimum_required (VERSION 3.22)

# Must be set before project(); ignored on other platforms.
set (CMAKE_OSX_DEPLOYMENT_TARGET "10.13" CACHE STRING "Minimum macOS version")

project (FreqFreaks VERSION 0.3.0 LANGUAGES C CXX)

set (CMAKE_CXX_STANDARD 17)
set (CMAKE_CXX_STANDARD_REQUIRED ON)

# Plug-ins must not depend on the VC++ redistributable being installed.
set (CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")

# Sources are UTF-8. Without this MSVC turns non-ASCII escapes such as the minus sign
# into "?" (the dB scale showed "?3 ?6 ?12" on Windows).
if (MSVC)
    add_compile_options (/utf-8)
endif()

add_compile_definitions (FREQFREAKS_VERSION="${PROJECT_VERSION}")

option (FREQFREAKS_BUILD_PLUGINS "Build the Track and Hub plug-ins (needs JUCE)" ON)
option (FREQFREAKS_BUILD_TESTS "Build unit and integration tests" ON)
set (FREQFREAKS_JUCE_PATH "" CACHE PATH "Path to a local JUCE 8 checkout (fetched from GitHub when empty)")

add_subdirectory (core)

if (FREQFREAKS_BUILD_PLUGINS)
    include (cmake/PatchJuce.cmake)
    if (FREQFREAKS_JUCE_PATH)
        freqfreaks_patch_juce ("${FREQFREAKS_JUCE_PATH}")
        add_subdirectory ("${FREQFREAKS_JUCE_PATH}" "${CMAKE_BINARY_DIR}/JUCE" EXCLUDE_FROM_ALL)
    else()
        include (FetchContent)
        FetchContent_Declare (JUCE
            GIT_REPOSITORY https://github.com/juce-framework/JUCE.git
            GIT_TAG 8.0.9
            GIT_SHALLOW TRUE)
        FetchContent_MakeAvailable (JUCE)
        freqfreaks_patch_juce ("${juce_SOURCE_DIR}")
    endif()

    add_subdirectory (plugins)
endif()

if (FREQFREAKS_BUILD_TESTS)
    enable_testing()
    add_subdirectory (tests)
endif()
