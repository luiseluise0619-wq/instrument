cmake_minimum_required(VERSION 3.24)
project(VocalChopStudio VERSION 3.3.5 LANGUAGES C CXX)
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

option(SLYCE_TESTS_ONLY "Build the native regression tests without the plugin SDK" OFF)
if(SLYCE_TESTS_ONLY)
    enable_testing()
    add_subdirectory(tests)
    return()
endif()
if(MSVC)
    set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")
    add_compile_options(/utf-8)
endif()
include(FetchContent)
set(JUCE_SOURCE_DIR "" CACHE PATH "Existing JUCE source checkout (expected 8.0.4)")
if(JUCE_SOURCE_DIR)
    add_subdirectory("${JUCE_SOURCE_DIR}" "${CMAKE_BINARY_DIR}/JUCE")
else()
    FetchContent_Declare(JUCE GIT_REPOSITORY https://github.com/juce-framework/JUCE.git
                         GIT_TAG 8.0.4 GIT_SHALLOW TRUE)
    FetchContent_MakeAvailable(JUCE)
endif()

# Preserve the original stretch API. The upstream branch is NOT a verified
# immutable pin. Supply the exact commits used by your release pipeline.
set(SIGNALSMITH_STRETCH_TAG "main" CACHE STRING "Stretch commit or tag")
set(SIGNALSMITH_LINEAR_TAG "0.3.1" CACHE STRING "Linear commit or tag")
set(SIGNALSMITH_STRETCH_SOURCE_DIR "" CACHE PATH "Local stretch checkout")
set(SIGNALSMITH_LINEAR_SOURCE_DIR "" CACHE PATH "Local linear checkout")
if(SIGNALSMITH_STRETCH_SOURCE_DIR)
    set(signalsmith_stretch_SOURCE_DIR "${SIGNALSMITH_STRETCH_SOURCE_DIR}")
else()
    FetchContent_Declare(signalsmith_stretch
        GIT_REPOSITORY https://github.com/Signalsmith-Audio/signalsmith-stretch.git
        GIT_TAG "${SIGNALSMITH_STRETCH_TAG}" SOURCE_SUBDIR cmake_not_used)
    FetchContent_MakeAvailable(signalsmith_stretch)
endif()
if(SIGNALSMITH_LINEAR_SOURCE_DIR)
    set(signalsmith_linear_SOURCE_DIR "${SIGNALSMITH_LINEAR_SOURCE_DIR}")
else()
    FetchContent_Declare(signalsmith_linear
        GIT_REPOSITORY https://github.com/Signalsmith-Audio/linear.git
        GIT_TAG "${SIGNALSMITH_LINEAR_TAG}"
        SOURCE_DIR "${CMAKE_BINARY_DIR}/_deps/signalsmith-include/signalsmith-linear"
        SOURCE_SUBDIR cmake_not_used)
    FetchContent_MakeAvailable(signalsmith_linear)
endif()
get_filename_component(SLYCE_LINEAR_PARENT "${signalsmith_linear_SOURCE_DIR}" DIRECTORY)
set(SLYCE_DSP_INCLUDE_DIRS
    "${signalsmith_stretch_SOURCE_DIR}"
    "${signalsmith_stretch_SOURCE_DIR}/include"
    "${signalsmith_stretch_SOURCE_DIR}/include/signalsmith-stretch"
    "${signalsmith_linear_SOURCE_DIR}" "${signalsmith_linear_SOURCE_DIR}/include"
    "${SLYCE_LINEAR_PARENT}")
include(cmake/VocalAssets.cmake)
juce_add_binary_data(VocalChopData SOURCES ${SLYCE_VOCAL_FILES})
# A separate binary-data target prevents skins colliding with audio symbols.
juce_add_binary_data(SlyceReferenceSkin
    HEADER_NAME ReferenceSkinData.h NAMESPACE ReferenceSkinData
    SOURCES
        assets/reference/reference_chrome_light.png
        assets/reference/reference_chrome_dark.png
        assets/reference/reference_knob_light.png
        assets/reference/reference_knob_dark.png)
set(SLYCE_FORMATS VST3 Standalone)
if(APPLE)
    list(APPEND SLYCE_FORMATS AU)
endif()
# AAX is intentionally opt-in because the Avid SDK is not redistributable.
# Set SLYCE_AAX_SDK_PATH (or AAX_SDK_PATH) to a locally downloaded AAX SDK
# to produce an unsigned developer/test .aaxplugin alongside VST3.
set(SLYCE_AAX_SDK_PATH "" CACHE PATH "Local Avid AAX SDK path (optional; never bundled)")
if(NOT SLYCE_AAX_SDK_PATH AND DEFINED ENV{AAX_SDK_PATH})
    set(SLYCE_AAX_SDK_PATH "$ENV{AAX_SDK_PATH}")
endif()
if(SLYCE_AAX_SDK_PATH)
    if(EXISTS "${SLYCE_AAX_SDK_PATH}")
        juce_set_aax_sdk_path("${SLYCE_AAX_SDK_PATH}")
        list(APPEND SLYCE_FORMATS AAX)
        message(STATUS "SLYCE: unsigned developer AAX target enabled")
    else()
        message(FATAL_ERROR "SLYCE_AAX_SDK_PATH does not exist: ${SLYCE_AAX_SDK_PATH}")
    endif()
else()
    message(STATUS "SLYCE: AAX target disabled (set SLYCE_AAX_SDK_PATH to enable)")
endif()
juce_add_plugin(VocalChopStudio
    COMPANY_NAME "VocalChop Labs" BUNDLE_ID com.vocalchoplabs.slyce
    PLUGIN_MANUFACTURER_CODE VCSL PLUGIN_CODE VCS0
    FORMATS ${SLYCE_FORMATS}
    IS_SYNTH TRUE NEEDS_MIDI_INPUT TRUE NEEDS_MIDI_OUTPUT FALSE
    VST3_CATEGORIES "Instrument" "Synth" "Sampler"
    VST3_AUTO_MANIFEST FALSE COPY_PLUGIN_AFTER_BUILD FALSE PRODUCT_NAME "Slyce")
juce_generate_juce_header(VocalChopStudio)
set(SLYCE_CORE_SOURCES
Source/PluginProcessor.cpp
    Source/UI/Reference/ReferenceEditor.cpp
    Source/AudioEngine/SampleLoader.cpp
    Source/AudioEngine/SliceEngine.cpp
    Source/AudioEngine/PitchFormant.cpp
    Source/AudioEngine/GranularEngine.cpp
    Source/AudioEngine/SynthEngine.cpp
    Source/AudioEngine/SamplerEngine.cpp
    Source/AudioEngine/VocalKitEngine.cpp
    Source/AudioEngine/AirVocalEngine.cpp
    Source/AudioEngine/FXChain.cpp
    Source/AudioEngine/VoicePool.cpp
    Source/UI/ThemeManager.cpp
    Source/UI/AppleLookAndFeel.cpp
    Source/UI/KnobComponent.cpp
    Source/UI/LooperPanel.cpp
    Source/UI/ChordBar.cpp
)
target_sources(VocalChopStudio PRIVATE ${SLYCE_CORE_SOURCES})
target_include_directories(VocalChopStudio PRIVATE Source ${SLYCE_DSP_INCLUDE_DIRS})
option(SLYCE_DEMO_GATE "Enable periodic demo mute and the owner's activation flow" ON)
set(SLYCE_LICENSE_API_URL "https://instrument-nu.vercel.app/api/license/activate" CACHE STRING "Owner's activation endpoint")
target_compile_definitions(VocalChopStudio PRIVATE
    SLYCE_DEMO_GATE=$<BOOL:${SLYCE_DEMO_GATE}>
    SLYCE_LICENSE_API_URL="${SLYCE_LICENSE_API_URL}"
    JUCE_WEB_BROWSER=0 JUCE_USE_CURL=1 JUCE_LOAD_CURL_SYMBOLS_LAZILY=1
    JUCE_VST3_CAN_REPLACE_VST2=0 JUCE_MODAL_LOOPS_PERMITTED=1)
# JUCE licensing is independent of plugin activation. A splash flag is NOT
# proof of distribution rights. Consult the license for the SDK you use.
target_link_libraries(VocalChopStudio PRIVATE VocalChopData SlyceReferenceSkin
    juce::juce_audio_utils juce::juce_cryptography juce::juce_dsp
    juce::juce_gui_basics juce::juce_gui_extra
    PUBLIC juce::juce_recommended_config_flags juce::juce_recommended_warning_flags)

option(SLYCE_BUILD_TOOLS "Build full-JUCE integration/render tools" OFF)
if(SLYCE_BUILD_TOOLS)
    function(slyce_add_tool target source)
        juce_add_console_app(${target} PRODUCT_NAME "${target}")
        juce_generate_juce_header(${target})
        target_sources(${target} PRIVATE ${source} ${SLYCE_CORE_SOURCES})
        target_compile_definitions(${target} PRIVATE
            JUCE_STANDALONE_APPLICATION=1 SLYCE_DEMO_GATE=0 JUCE_USE_CURL=0
            JUCE_WEB_BROWSER=0 JUCE_MODAL_LOOPS_PERMITTED=1 JUCE_VST3_CAN_REPLACE_VST2=0
            JucePlugin_VersionString="${PROJECT_VERSION}")
        target_include_directories(${target} PRIVATE Source ${SLYCE_DSP_INCLUDE_DIRS})
        target_link_libraries(${target} PRIVATE VocalChopData SlyceReferenceSkin juce::juce_audio_utils
            juce::juce_cryptography juce::juce_dsp juce::juce_gui_basics juce::juce_gui_extra
            juce::juce_recommended_config_flags)
    endfunction()
    slyce_add_tool(SlyceRenderTest tools/render_test.cpp)
    slyce_add_tool(SlyceDemoRender tools/demo_render.cpp)
    slyce_add_tool(SlyceAirVocalDemo tools/air_vocal_demo.cpp)
    slyce_add_tool(SlyceUISnapshot tools/ui_snapshot.cpp)
endif()
