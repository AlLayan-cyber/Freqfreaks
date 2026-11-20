# FreqFreaks DSP core: plain C++17, no JUCE, unit-testable.
add_library (freqfreaks_core STATIC
    src/Analyzer.cpp
    src/BellBank.cpp
    src/Fft.cpp
    src/SharedBus.cpp
    src/StereoProcessor.cpp
    src/TrackEngine.cpp
    src/Unmasking.cpp)

target_include_directories (freqfreaks_core PUBLIC include)
target_compile_features (freqfreaks_core PUBLIC cxx_std_17)
set_target_properties (freqfreaks_core PROPERTIES POSITION_INDEPENDENT_CODE ON)

if (MSVC)
    target_compile_options (freqfreaks_core PRIVATE /W4)
else()
    target_compile_options (freqfreaks_core PRIVATE -Wall -Wextra -Wshadow -Wconversion -Wno-sign-conversion)
endif()

if (UNIX AND NOT APPLE)
    target_link_libraries (freqfreaks_core PUBLIC rt)
endif()

find_package (Threads REQUIRED)
target_link_libraries (freqfreaks_core PUBLIC Threads::Threads)
