cmake_minimum_required(VERSION 3.22)

project(VocalChopStudio VERSION 1.6.0 LANGUAGES C CXX)

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

# Header-only: SOURCE_SUBDIR points at a non-existent dir so
# FetchContent_MakeAvailable only downloads the sources without calling
# add_subdirectory (we just need the include paths). This also avoids the
# FetchContent_Populate deprecation error on CMake >= 3.30.
#
# signalsmith-stretch depends on the separate signalsmith-linear library via
# #include "signalsmith-linear/stft.h", so we fetch that too and check it out
# into a directory literally named "signalsmith-linear" whose PARENT we put on
# the include path.
set(SIGNALSMITH_LINEAR_TAG "main" CACHE STRING "signalsmith linear git tag/commit")
set(SIGNALSMITH_INCLUDE_ROOT ${CMAKE_BINARY_DIR}/_deps/signalsmith-include)

FetchContent_Declare(
    signalsmith_stretch
    GIT_REPOSITORY https://github.com/Signalsmith-Audio/signalsmith-stretch.git
    GIT_TAG        ${SIGNALSMITH_STRETCH_TAG}
    SOURCE_SUBDIR  cmake_subdir_intentionally_unused)
FetchContent_Declare(
    signalsmith_linear
    GIT_REPOSITORY https://github.com/Signalsmith-Audio/linear.git
    GIT_TAG        ${SIGNALSMITH_LINEAR_TAG}
    SOURCE_DIR     ${SIGNALSMITH_INCLUDE_ROOT}/signalsmith-linear
    SOURCE_SUBDIR  cmake_subdir_intentionally_unused)
FetchContent_MakeAvailable(signalsmith_stretch signalsmith_linear)

# It's a MIDI-triggered sampler, so it must be an INSTRUMENT (generator), not an
# audio effect — otherwise FL Studio / Ableton load it on the mixer and never
# send it piano-roll notes. IS_SYNTH TRUE also sets the VST3 "Instrument"
# category and the AU 'aumu' type.
juce_add_plugin(VocalChopStudio
    COMPANY_NAME "VocalChop Labs"
    BUNDLE_ID com.vocalchoplabs.vocalchopstudio
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

# Embed the demo vocal (one-click first-run sound) and the two full-bleed
# skin artworks (drawn as theme backdrops).
juce_add_binary_data(VocalChopData SOURCES
    examples/vocal_chop_demo.wav
    examples/vox_air.wav
    examples/vox_rage.wav
    examples/vox_beatbox.wav
    assets/skin_neon_rider.png
    assets/skin_neo_seoul.png)

target_sources(VocalChopStudio PRIVATE
    Source/PluginProcessor.cpp
    Source/PluginEditor.cpp
    Source/AudioEngine/SampleLoader.cpp
    Source/AudioEngine/SliceEngine.cpp
    Source/AudioEngine/PitchFormant.cpp
    Source/AudioEngine/GranularEngine.cpp
    Source/AudioEngine/SynthEngine.cpp
    Source/AudioEngine/FXChain.cpp
    Source/AudioEngine/VoicePool.cpp
    Source/UI/ThemeManager.cpp
    Source/UI/AppleLookAndFeel.cpp
    Source/UI/MeterComponent.cpp
    Source/UI/WaveformView.cpp
    Source/UI/KnobComponent.cpp
    Source/UI/SliceGrid.cpp
    Source/UI/LooperPanel.cpp
    Source/UI/ChordBar.cpp
    Source/UI/FXRack.cpp)

target_include_directories(VocalChopStudio PRIVATE
    Source
    ${signalsmith_stretch_SOURCE_DIR}
    ${SIGNALSMITH_INCLUDE_ROOT}
    ${signalsmith_linear_SOURCE_DIR})

target_compile_definitions(VocalChopStudio PRIVATE
    JUCE_WEB_BROWSER=0
    JUCE_USE_CURL=0
    JUCE_VST3_CAN_REPLACE_VST2=0
    JUCE_DISPLAY_SPLASH_SCREEN=0
    JUCE_MODAL_LOOPS_PERMITTED=1)

target_link_libraries(VocalChopStudio
    PRIVATE
        VocalChopData
        juce::juce_audio_utils
        juce::juce_dsp
        juce::juce_gui_basics
        juce::juce_gui_extra
    PUBLIC
        juce::juce_recommended_config_flags
        juce::juce_recommended_lto_flags
        juce::juce_recommended_warning_flags)
