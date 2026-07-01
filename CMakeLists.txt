cmake_minimum_required(VERSION 3.22)

project(VocalChopStudio VERSION 1.0.0 LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# ---------------------------------------------------------------------------
# JUCE
#
# By default we pull JUCE via FetchContent so a fresh checkout builds with a
# single `cmake -B build` invocation. If you already have JUCE checked out,
# pass -DJUCE_SOURCE_DIR=/path/to/JUCE to use it instead of downloading.
# ---------------------------------------------------------------------------
set(JUCE_SOURCE_DIR "" CACHE PATH "Path to an existing JUCE source tree")

include(FetchContent)

if(JUCE_SOURCE_DIR)
    add_subdirectory(${JUCE_SOURCE_DIR} ${CMAKE_BINARY_DIR}/JUCE)
else()
    FetchContent_Declare(
        JUCE
        GIT_REPOSITORY https://github.com/juce-framework/JUCE.git
        GIT_TAG        8.0.4
        GIT_SHALLOW    TRUE)
    FetchContent_MakeAvailable(JUCE)
endif()

# ---------------------------------------------------------------------------
# Signalsmith Stretch — high-quality pitch / formant shifting.
#
# Header-only, MIT-licensed, with a `dsp/` git submodule (fetched
# automatically). Used by Source/AudioEngine/PitchFormant for transparent,
# formant-preserving pitch shifting. Pin SIGNALSMITH_STRETCH_TAG to a specific
# commit for reproducible builds; `main` is used by default.
# ---------------------------------------------------------------------------
set(SIGNALSMITH_STRETCH_TAG "main" CACHE STRING "signalsmith-stretch git tag/commit")

FetchContent_Declare(
    signalsmith_stretch
    GIT_REPOSITORY https://github.com/Signalsmith-Audio/signalsmith-stretch.git
    GIT_TAG        ${SIGNALSMITH_STRETCH_TAG})

# Header-only: populate the source but do not add_subdirectory (no build step).
FetchContent_GetProperties(signalsmith_stretch)
if(NOT signalsmith_stretch_POPULATED)
    FetchContent_Populate(signalsmith_stretch)
endif()

# It's a MIDI-triggered sampler, so it must be an INSTRUMENT (generator), not an
# audio effect — otherwise FL Studio / Ableton load it on the mixer and never
# send it piano-roll notes. IS_SYNTH TRUE also sets the VST3 "Instrument"
# category and the AU 'aumu' type.
juce_add_plugin(VocalChopStudio
    COMPANY_NAME "VocalChop Labs"
    PLUGIN_MANUFACTURER_CODE VCSL
    PLUGIN_CODE VCS0
    FORMATS VST3 AU Standalone
    IS_SYNTH TRUE
    NEEDS_MIDI_INPUT TRUE
    NEEDS_MIDI_OUTPUT FALSE
    VST3_CATEGORIES "Instrument" "Synth" "Sampler"
    COPY_PLUGIN_AFTER_BUILD FALSE
    PRODUCT_NAME "VocalChop Studio")

juce_generate_juce_header(VocalChopStudio)

target_sources(VocalChopStudio PRIVATE
    Source/PluginProcessor.cpp
    Source/PluginEditor.cpp
    Source/AudioEngine/SampleLoader.cpp
    Source/AudioEngine/SliceEngine.cpp
    Source/AudioEngine/PitchFormant.cpp
    Source/AudioEngine/GranularEngine.cpp
    Source/AudioEngine/FXChain.cpp
    Source/AudioEngine/VoicePool.cpp
    Source/UI/ThemeManager.cpp
    Source/UI/AppleLookAndFeel.cpp
    Source/UI/WaveformView.cpp
    Source/UI/KnobComponent.cpp
    Source/UI/SliceGrid.cpp
    Source/UI/FXRack.cpp)

target_include_directories(VocalChopStudio PRIVATE
    Source
    ${signalsmith_stretch_SOURCE_DIR})

target_compile_definitions(VocalChopStudio PRIVATE
    JUCE_WEB_BROWSER=0
    JUCE_USE_CURL=0
    JUCE_VST3_CAN_REPLACE_VST2=0
    JUCE_DISPLAY_SPLASH_SCREEN=0
    JUCE_MODAL_LOOPS_PERMITTED=1)

target_link_libraries(VocalChopStudio
    PRIVATE
        juce::juce_audio_utils
        juce::juce_dsp
        juce::juce_gui_basics
        juce::juce_gui_extra
    PUBLIC
        juce::juce_recommended_config_flags
        juce::juce_recommended_lto_flags
        juce::juce_recommended_warning_flags)
