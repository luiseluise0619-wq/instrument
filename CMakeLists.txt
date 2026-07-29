cmake_minimum_required(VERSION 3.22)

project(VocalChopStudio VERSION 3.1.0 LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Link the MSVC runtime statically: user machines without the VC++
# redistributable would otherwise fail to load the plugin with no error
# message at all - the #1 cause of "the DAW can't see it" reports.
if(MSVC)
    set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")
endif()

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
    BUNDLE_ID com.vocalchoplabs.slyce
    PLUGIN_MANUFACTURER_CODE VCSL
    PLUGIN_CODE VCS0
    FORMATS VST3 AU Standalone
    IS_SYNTH TRUE
    NEEDS_MIDI_INPUT TRUE
    NEEDS_MIDI_OUTPUT FALSE
    VST3_CATEGORIES "Instrument" "Synth" "Sampler"
    COPY_PLUGIN_AFTER_BUILD FALSE
    PRODUCT_NAME "Slyce")

juce_generate_juce_header(VocalChopStudio)

# Embed the demo vocals (one-click first-run sound). The themes draw their
# backdrops procedurally now, so no artwork ships in the binary.
# The built-in chop sources. Mono on purpose: the plugin has its own width and
# chorus, and every one of these ships inside the binary, so stereo would
# double the download for a placement the user overrides anyway.
juce_add_binary_data(VocalChopData SOURCES
    examples/vocal_chop_demo.wav
    examples/vox_air.wav
    examples/vox_rage.wav
    examples/vox_beatbox.wav
    examples/vox_chant.wav
    examples/vox_hook.wav
    examples/vox_choir.wav
    examples/vox_whisper.wav
    examples/vox_diva.wav
    examples/vox_stabs.wav
    # Second wave: twenty-six more, grouped in the picker by what you would
    # reach for them FOR - chops, hooks, textures, percussion, characters.
    examples/vox_triplet.wav
    examples/vox_halftime.wav
    examples/vox_garage.wav
    examples/vox_amen.wav
    examples/vox_stutter.wav
    examples/vox_revchant.wav
    examples/vox_topline.wav
    examples/vox_trapmel.wav
    examples/vox_afro.wav
    examples/vox_drill.wav
    examples/vox_ballad.wav
    examples/vox_vowelpad.wav
    examples/vox_nasal.wav
    examples/vox_hum.wav
    examples/vox_gospel.wav
    examples/vox_vocoder.wav
    examples/vox_mouthperc.wav
    examples/vox_clicks.wav
    examples/vox_breath.wav
    examples/vox_gasp.wav
    examples/vox_riser.wav
    examples/vox_downlifter.wav
    examples/vox_kids.wav
    examples/vox_deep.wav
    examples/vox_robot.wav
    examples/vox_opera.wav)

target_sources(VocalChopStudio PRIVATE
    Source/PluginProcessor.cpp
    Source/PluginEditor.cpp
    Source/AudioEngine/SampleLoader.cpp
    Source/AudioEngine/SliceEngine.cpp
    Source/AudioEngine/PitchFormant.cpp
    Source/AudioEngine/GranularEngine.cpp
    Source/AudioEngine/SynthEngine.cpp
    Source/AudioEngine/SamplerEngine.cpp
    Source/AudioEngine/FXChain.cpp
    Source/AudioEngine/VoicePool.cpp
    Source/UI/ThemeManager.cpp
    Source/UI/AppleLookAndFeel.cpp
    Source/UI/MeterComponent.cpp
    Source/UI/ScopePanel.cpp
    Source/UI/SegmentedControl.cpp
    Source/UI/WaveformView.cpp
    Source/UI/KnobComponent.cpp
    Source/UI/SliceGrid.cpp
    Source/UI/LooperPanel.cpp
    Source/UI/ChordBar.cpp
    Source/UI/AmbientPanel.cpp
    Source/UI/FXRack.cpp)

target_include_directories(VocalChopStudio PRIVATE
    Source
    ${signalsmith_stretch_SOURCE_DIR}
    ${SIGNALSMITH_INCLUDE_ROOT}
    ${signalsmith_linear_SOURCE_DIR})

# JUCE_DISPLAY_SPLASH_SCREEN=1 is what makes commercial sales legal on the
# free JUCE Personal tier (splash shown, revenue under the tier's cap).
# Only set it back to 0 after buying a JUCE Indie/Pro licence.
# SLYCE_DEMO_GATE=1 turns on the demo mute + unlock flow for the paid
# release. 0 ships fully unlocked (current default while testing/free).
target_compile_definitions(VocalChopStudio PRIVATE
    SLYCE_DEMO_GATE=1
    JUCE_WEB_BROWSER=0
    JUCE_USE_CURL=1
    JUCE_LOAD_CURL_SYMBOLS_LAZILY=1
    JUCE_VST3_CAN_REPLACE_VST2=0
    JUCE_DISPLAY_SPLASH_SCREEN=1
    JUCE_MODAL_LOOPS_PERMITTED=1)

target_link_libraries(VocalChopStudio
    PRIVATE
        VocalChopData
        juce::juce_audio_utils
        juce::juce_cryptography
        juce::juce_dsp
        juce::juce_gui_basics
        juce::juce_gui_extra
    PUBLIC
        juce::juce_recommended_config_flags
        juce::juce_recommended_lto_flags
        juce::juce_recommended_warning_flags)

# --- offline core for the developer tools ---------------------------------
# The shipping library is compiled with SLYCE_DEMO_GATE=1, which mutes two
# seconds every minute. The render tools bounce two-minute arrangements, so
# linking the shipping library punched a hole in every track at 0:58. They get
# their own copy of the sources with the gate compiled OUT - the product build
# is untouched.
set(SLYCE_CORE_SOURCES
    Source/PluginProcessor.cpp
    Source/PluginEditor.cpp
    Source/AudioEngine/SampleLoader.cpp
    Source/AudioEngine/SliceEngine.cpp
    Source/AudioEngine/PitchFormant.cpp
    Source/AudioEngine/GranularEngine.cpp
    Source/AudioEngine/SynthEngine.cpp
    Source/AudioEngine/SamplerEngine.cpp
    Source/AudioEngine/FXChain.cpp
    Source/AudioEngine/VoicePool.cpp
    Source/UI/ThemeManager.cpp
    Source/UI/AppleLookAndFeel.cpp
    Source/UI/MeterComponent.cpp
    Source/UI/ScopePanel.cpp
    Source/UI/SegmentedControl.cpp
    Source/UI/WaveformView.cpp
    Source/UI/KnobComponent.cpp
    Source/UI/SliceGrid.cpp
    Source/UI/LooperPanel.cpp
    Source/UI/ChordBar.cpp
    Source/UI/AmbientPanel.cpp
    Source/UI/FXRack.cpp)

function(slyce_add_tool target source)
    juce_add_console_app(${target} PRODUCT_NAME "${target}")
    target_sources(${target} PRIVATE ${source} ${SLYCE_CORE_SOURCES})
    target_compile_definitions(${target} PRIVATE
        JUCE_STANDALONE_APPLICATION=1 SLYCE_DEMO_GATE=0 JUCE_USE_CURL=0
        JUCE_WEB_BROWSER=0 JUCE_MODAL_LOOPS_PERMITTED=1
        JUCE_DISPLAY_SPLASH_SCREEN=0 JUCE_VST3_CAN_REPLACE_VST2=0
        # The editor prints the build version in its footer; outside the
        # plugin wrapper that macro does not exist.
        JucePlugin_VersionString="${PROJECT_VERSION}")
    add_dependencies(${target} VocalChopStudio)
    target_link_libraries(${target} PRIVATE
        VocalChopData
        juce::juce_audio_utils
        juce::juce_cryptography
        juce::juce_dsp
        juce::juce_gui_basics
        juce::juce_gui_extra
        juce::juce_audio_plugin_client
        juce::juce_recommended_config_flags)
    target_include_directories(${target} PRIVATE
        Source
        ${CMAKE_CURRENT_BINARY_DIR}/VocalChopStudio_artefacts/JuceLibraryCode
        ${CMAKE_CURRENT_BINARY_DIR}/juce_binarydata_VocalChopData/JuceLibraryCode
        ${signalsmith_stretch_SOURCE_DIR}
        ${SIGNALSMITH_INCLUDE_ROOT}
        ${signalsmith_linear_SOURCE_DIR})
endfunction()

# --- offline render harness (developer tool; not part of the plugin) -------
if (EXISTS ${CMAKE_CURRENT_SOURCE_DIR}/tools/render_test.cpp)
    slyce_add_tool(SlyceRenderTest tools/render_test.cpp)
endif()

# --- offline arrangement renderer: builds the demo tracks for the store page -
if (EXISTS ${CMAKE_CURRENT_SOURCE_DIR}/tools/demo_render.cpp)
    slyce_add_tool(SlyceDemoRender tools/demo_render.cpp)
endif()
