cmake_minimum_required(VERSION 3.22)
# Deployment target and architectures must be set before project().
if(APPLE)
    set(CMAKE_OSX_DEPLOYMENT_TARGET "10.15" CACHE STRING "")
    set(CMAKE_OSX_ARCHITECTURES "arm64;x86_64" CACHE STRING "")
endif()
project(RhythmArranger VERSION 0.1.0 LANGUAGES C CXX)
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

option(RHYTHM_BUILD_PLUGIN "Build the plug-ins (needs JUCE)" ON)
set(RHYTHM_JUCE_PATH "" CACHE PATH "Local JUCE checkout; empty = download JUCE 8.0.4")

# core: no JUCE, no floating point in the rhythm rules
add_library(rhythm_core STATIC core/Rhythm.cpp core/Midi.cpp core/CatalogData.cpp)
target_include_directories(rhythm_core PUBLIC core)
set_target_properties(rhythm_core PROPERTIES POSITION_INDEPENDENT_CODE ON)
target_compile_options(rhythm_core PRIVATE -ffp-contract=off -Wall -Wextra)

add_executable(rhythm_cli cli/rhythm_main.cpp)
target_link_libraries(rhythm_cli PRIVATE rhythm_core)
add_executable(rhythm_tests tests/core_test.cpp)
target_link_libraries(rhythm_tests PRIVATE rhythm_core)

# ---- plug-ins ---------------------------------------------------------------
if(RHYTHM_BUILD_PLUGIN)
    if(RHYTHM_JUCE_PATH)
        add_subdirectory(${RHYTHM_JUCE_PATH} juce)
    else()
        include(FetchContent)
        FetchContent_Declare(juce
            GIT_REPOSITORY https://github.com/juce-framework/JUCE.git
            GIT_TAG 8.0.4
            GIT_SHALLOW ON)
        FetchContent_MakeAvailable(juce)
    endif()

    set(RHYTHM_DEFS JUCE_WEB_BROWSER=0 JUCE_USE_CURL=0 JUCE_VST3_CAN_REPLACE_VST2=0 JUCE_DISPLAY_SPLASH_SCREEN=0)

    # Rhythm Arranger: instrument (Logic: Software Instrument -> AU Instruments -> Teilormusic)
    juce_add_plugin(RhythmArranger
        COMPANY_NAME "Teilormusic"
        BUNDLE_ID "com.teilormusic.rhythmarranger"
        PLUGIN_MANUFACTURER_CODE Tlrm
        PLUGIN_CODE Rthm
        FORMATS AU VST3 Standalone
        PRODUCT_NAME "Rhythm Arranger"
        IS_SYNTH TRUE
        NEEDS_MIDI_INPUT TRUE
        NEEDS_MIDI_OUTPUT FALSE
        IS_MIDI_EFFECT FALSE
        EDITOR_WANTS_KEYBOARD_FOCUS FALSE
        COPY_PLUGIN_AFTER_BUILD FALSE
        AU_MAIN_TYPE kAudioUnitType_MusicDevice
        VST3_CATEGORIES Instrument Tools)
    target_sources(RhythmArranger PRIVATE
        plugin/Base.cpp plugin/Panel.cpp plugin/PluginProcessor.cpp plugin/PluginEditor.cpp plugin/Look.cpp)
    target_compile_definitions(RhythmArranger PUBLIC ${RHYTHM_DEFS})
    target_link_libraries(RhythmArranger
        PRIVATE rhythm_core juce::juce_audio_utils juce::juce_gui_extra
        PUBLIC juce::juce_recommended_config_flags juce::juce_recommended_lto_flags
               juce::juce_recommended_warning_flags)

    # Rhythm Arranger Live: MIDI FX (Logic: MIDI FX slot -> Audio Units -> Teilormusic)
    juce_add_plugin(RhythmArrangerLive
        COMPANY_NAME "Teilormusic"
        BUNDLE_ID "com.teilormusic.rhythmarrangerlive"
        PLUGIN_MANUFACTURER_CODE Tlrm
        PLUGIN_CODE RtLv
        FORMATS AU VST3
        PRODUCT_NAME "Rhythm Arranger Live"
        IS_SYNTH FALSE
        NEEDS_MIDI_INPUT TRUE
        NEEDS_MIDI_OUTPUT TRUE
        IS_MIDI_EFFECT TRUE
        EDITOR_WANTS_KEYBOARD_FOCUS FALSE
        COPY_PLUGIN_AFTER_BUILD FALSE
        AU_MAIN_TYPE kAudioUnitType_MIDIProcessor
        VST3_CATEGORIES Fx Tools)
    target_sources(RhythmArrangerLive PRIVATE
        plugin/Base.cpp plugin/Panel.cpp plugin/LiveProcessor.cpp plugin/LiveEditor.cpp plugin/Look.cpp)
    target_compile_definitions(RhythmArrangerLive PUBLIC ${RHYTHM_DEFS})
    target_link_libraries(RhythmArrangerLive
        PRIVATE rhythm_core juce::juce_audio_utils juce::juce_gui_extra
        PUBLIC juce::juce_recommended_config_flags juce::juce_recommended_lto_flags
               juce::juce_recommended_warning_flags)
endif()

# ---- plug-in tests: state, click preview, live modes and editor snapshots (needs a display) ----
option(RHYTHM_BUILD_TESTS "Build the plug-in test runner" OFF)
if(RHYTHM_BUILD_PLUGIN AND RHYTHM_BUILD_TESTS)
    juce_add_console_app(RhythmPluginTests PRODUCT_NAME "RhythmPluginTests")
    target_sources(RhythmPluginTests PRIVATE tests/plugin_test.cpp
        plugin/Base.cpp plugin/Panel.cpp plugin/PluginProcessor.cpp plugin/PluginEditor.cpp plugin/Look.cpp
        plugin/LiveProcessor.cpp plugin/LiveEditor.cpp)
    target_include_directories(RhythmPluginTests PRIVATE plugin)
    target_compile_definitions(RhythmPluginTests PRIVATE JUCE_WEB_BROWSER=0 JUCE_USE_CURL=0
        JucePlugin_Name="Rhythm Arranger" JUCE_STANDALONE_APPLICATION=1 JUCE_MODAL_LOOPS_PERMITTED=1
        RHYTHM_NO_PLUGIN_ENTRY=1)
    target_link_libraries(RhythmPluginTests PRIVATE rhythm_core juce::juce_audio_utils juce::juce_gui_extra
        juce::juce_recommended_config_flags)
endif()
