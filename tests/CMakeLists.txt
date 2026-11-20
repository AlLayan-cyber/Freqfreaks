add_executable (freqfreaks_core_tests CoreTests.cpp)
target_link_libraries (freqfreaks_core_tests PRIVATE freqfreaks_core)
add_test (NAME core COMMAND freqfreaks_core_tests)
