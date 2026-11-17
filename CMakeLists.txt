cmake_minimum_required(VERSION 3.22)
# Deployment target and architectures must be set before project().
if(APPLE)
    set(CMAKE_OSX_DEPLOYMENT_TARGET "10.15" CACHE STRING "")
    set(CMAKE_OSX_ARCHITECTURES "arm64;x86_64" CACHE STRING "")
endif()
project(RhythmArranger VERSION 0.1.0 LANGUAGES C CXX)
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# core: no JUCE, no floating point in the rhythm rules
add_library(rhythm_core STATIC core/Rhythm.cpp core/Midi.cpp core/CatalogData.cpp)
target_include_directories(rhythm_core PUBLIC core)
set_target_properties(rhythm_core PROPERTIES POSITION_INDEPENDENT_CODE ON)
target_compile_options(rhythm_core PRIVATE -ffp-contract=off -Wall -Wextra)

add_executable(rhythm_cli cli/rhythm_main.cpp)
target_link_libraries(rhythm_cli PRIVATE rhythm_core)
add_executable(rhythm_tests tests/core_test.cpp)
target_link_libraries(rhythm_tests PRIVATE rhythm_core)
