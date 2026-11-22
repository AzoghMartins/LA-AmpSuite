cmake_minimum_required(VERSION 3.22)

project(LAAmpSuite VERSION 1.0.0 LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_POSITION_INDEPENDENT_CODE ON)

option(LAAS_BUILD_TESTS "Build the DSP and plugin tests" ON)
option(LAAS_ENGINE_ONLY "Only build the DSP engine and engine tests (no JUCE)" OFF)
option(LAAS_COPY_AFTER_BUILD "Install plugins into ~/.vst3 and ~/.lv2 after building" OFF)
set(LAAS_JUCE_DIR "" CACHE PATH "Path to a local JUCE checkout (fetched from GitHub when empty)")

# ---------------------------------------------------------------------------------------------
# JUCE
if(LAAS_ENGINE_ONLY)
    # build only the DSP engine and its tests (no JUCE needed)
elseif(LAAS_JUCE_DIR)
    add_subdirectory(${LAAS_JUCE_DIR} ${CMAKE_BINARY_DIR}/JUCE EXCLUDE_FROM_ALL)
else()
    include(FetchContent)
    FetchContent_Declare(JUCE
        GIT_REPOSITORY https://github.com/juce-framework/JUCE.git
        GIT_TAG 8.0.4
        GIT_SHALLOW ON)
    FetchContent_MakeAvailable(JUCE)
endif()

# ---------------------------------------------------------------------------------------------
# DSP engine (no JUCE dependency, unit tested separately)
add_library(laas_engine STATIC
    Source/dsp/ToneStack.cpp
    Source/dsp/Amp.cpp
    Source/dsp/Pedals.cpp
    Source/dsp/Cabinet.cpp
    Source/dsp/Modules.cpp
    Source/dsp/Effects.cpp
    Source/dsp/AmpEngine.cpp)
target_include_directories(laas_engine PUBLIC Source)
target_compile_options(laas_engine PRIVATE $<$<CXX_COMPILER_ID:GNU,Clang>:-Wall -Wextra -O3>)

if(NOT LAAS_ENGINE_ONLY)
set(LAAS_PLUGIN_SOURCES
    Source/Parameters.cpp
    Source/PluginProcessor.cpp
    Source/gui/Theme.cpp
    Source/gui/Widgets.cpp
    Source/gui/Displays.cpp
    Source/gui/PluginEditor.cpp)

# ---------------------------------------------------------------------------------------------
# Plugin
juce_add_plugin(LAAmpSuite
    PRODUCT_NAME "LA-AmpSuite"
    COMPANY_NAME "LA Audio"
    BUNDLE_ID "com.la-audio.laampsuite"
    PLUGIN_MANUFACTURER_CODE LaAu
    PLUGIN_CODE LaAs
    FORMATS VST3 LV2 Standalone
    IS_SYNTH FALSE
    NEEDS_MIDI_INPUT FALSE
    NEEDS_MIDI_OUTPUT FALSE
    IS_MIDI_EFFECT FALSE
    EDITOR_WANTS_KEYBOARD_FOCUS FALSE
    VST3_CATEGORIES Fx Distortion Reverb Delay
    LV2URI "https://github.com/AzoghMartins/LA-AmpSuite"
    COPY_PLUGIN_AFTER_BUILD ${LAAS_COPY_AFTER_BUILD})

target_sources(LAAmpSuite PRIVATE ${LAAS_PLUGIN_SOURCES})

target_compile_definitions(LAAmpSuite PUBLIC
    JUCE_WEB_BROWSER=0
    JUCE_USE_CURL=0
    JUCE_VST3_CAN_REPLACE_VST2=0)

target_link_libraries(LAAmpSuite
    PRIVATE
        laas_engine
        juce::juce_audio_utils
        juce::juce_audio_processors
        juce::juce_dsp
    PUBLIC
        juce::juce_recommended_config_flags
        juce::juce_recommended_lto_flags
        juce::juce_recommended_warning_flags)

endif()

# ---------------------------------------------------------------------------------------------
if(LAAS_BUILD_TESTS)
    enable_testing()
    add_executable(laas_tests tests/EngineTests.cpp)
    target_link_libraries(laas_tests PRIVATE laas_engine)
    target_compile_options(laas_tests PRIVATE $<$<CXX_COMPILER_ID:GNU,Clang>:-O2>)
    add_test(NAME engine COMMAND laas_tests)
    if(NOT LAAS_ENGINE_ONLY)

    # Drives the real processor + editor like a host (needs an X display, e.g. xvfb-run).
    juce_add_console_app(laas_integration PRODUCT_NAME "LA-AmpSuite Integration Test")
    target_sources(laas_integration PRIVATE tests/PluginIntegrationTest.cpp ${LAAS_PLUGIN_SOURCES})
    target_compile_definitions(laas_integration PRIVATE
        JucePlugin_Name="LA-AmpSuite"
        JUCE_WEB_BROWSER=0
        JUCE_USE_CURL=0)
    target_link_libraries(laas_integration PRIVATE
        laas_engine
        juce::juce_audio_utils
        juce::juce_audio_processors
        juce::juce_dsp
        juce::juce_recommended_config_flags)
    add_test(NAME integration COMMAND laas_integration)
    endif()
endif()
