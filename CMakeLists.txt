cmake_minimum_required(VERSION 3.22)

# Must be set before project() so it applies to every target, including JUCE's helpers.
set(CMAKE_OSX_DEPLOYMENT_TARGET "11.0" CACHE STRING "Minimum macOS version")

project(Cymbalis VERSION 0.1.0 LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# OFF builds only the DSP engine and its console tools, without fetching JUCE
# (used by the A/B render job).
option(CYMBALIS_BUILD_PLUGIN "Build the JUCE plugin (VST3/AU)" ON)

# ON builds "Cymbalis Tuning" instead of "Cymbalis": the same plugin with the hidden Tuning
# window (docs/TUNING.md), under its own identity so both can be installed side by side.
# The normal build contains none of the tuning code.
option(CYMBALIS_TUNING "Build Cymbalis Tuning (the plugin with the tuning window)" OFF)

# ---------------------------------------------------------------------------
# DSP engine (C++ port of the Python V19 engine) and the A/B console renderer
# ---------------------------------------------------------------------------
add_subdirectory(plugin/dsp)
add_subdirectory(tools/cymbalis_render)
add_subdirectory(tools/cymbalis_bench)
add_subdirectory(tools/cymbalis_trims)

if(NOT CYMBALIS_BUILD_PLUGIN)
    return()
endif()

# ---------------------------------------------------------------------------
# JUCE (pinned)
# ---------------------------------------------------------------------------
include(FetchContent)
FetchContent_Declare(
    JUCE
    GIT_REPOSITORY https://github.com/juce-framework/JUCE.git
    GIT_TAG        7.0.12
    GIT_SHALLOW    TRUE
)
FetchContent_MakeAvailable(JUCE)

# ---------------------------------------------------------------------------
# Plugin identity
#   Codes are 4 characters. AU rules: manufacturer code needs an upper-case
#   first letter; plugin code needs exactly one upper-case letter.
#   Changing either code changes the plugin's ID as seen by DAWs.
# ---------------------------------------------------------------------------
if(APPLE)
    set(CYMBALIS_FORMATS VST3 AU)
else()
    set(CYMBALIS_FORMATS VST3)   # Linux is only used for local compile checks
endif()

if(CYMBALIS_TUNING)
    set(CYMBALIS_TARGET      CymbalisTuning)
    set(CYMBALIS_BUNDLE_ID   "com.dhammers.cymbalistuning")
    set(CYMBALIS_PLUGIN_CODE CymT)
    set(CYMBALIS_PRODUCT     "Cymbalis Tuning")
    set(CYMBALIS_DSP         cymbalis_fast_tuning)
else()
    set(CYMBALIS_TARGET      Cymbalis)
    set(CYMBALIS_BUNDLE_ID   "com.dhammers.cymbalis")
    set(CYMBALIS_PLUGIN_CODE Cymb)
    set(CYMBALIS_PRODUCT     "Cymbalis")
    set(CYMBALIS_DSP         cymbalis_fast)
endif()

juce_add_plugin(${CYMBALIS_TARGET}
    COMPANY_NAME                "DHammers"
    BUNDLE_ID                   "${CYMBALIS_BUNDLE_ID}"
    PLUGIN_MANUFACTURER_CODE    Dham
    PLUGIN_CODE                 ${CYMBALIS_PLUGIN_CODE}
    PRODUCT_NAME                "${CYMBALIS_PRODUCT}"
    FORMATS                     ${CYMBALIS_FORMATS}
    IS_SYNTH                    TRUE
    NEEDS_MIDI_INPUT            TRUE
    NEEDS_MIDI_OUTPUT           FALSE
    IS_MIDI_EFFECT              FALSE
    EDITOR_WANTS_KEYBOARD_FOCUS FALSE
    COPY_PLUGIN_AFTER_BUILD     FALSE
    VST3_CATEGORIES             Instrument Drum
    AU_MAIN_TYPE                kAudioUnitType_MusicDevice
)

# ---------------------------------------------------------------------------
# Compiled-in resources: the baked presets (nl_amount 0, written by tools/bake_preset.py),
# their photos, and the UI artwork and font. Calibri Light is used when
# resources/ui/calibril.ttf exists; Carlito Regular stands in until then.
# ---------------------------------------------------------------------------
set(CYMBALIS_RESOURCES
    resources/presets/nova_ride.bake.json
    resources/presets/smoke_flat_ride.bake.json
    resources/presets/hammered_thin_ride.bake.json
    resources/presets/swish.bake.json
    resources/ui/CYMBALIS_Background_logo.png
    resources/ui/Nova.png
    resources/ui/SmokeFlatRide.png
    resources/ui/HammeredThinRide.png
    resources/ui/Swish.png
    resources/ui/knob.png
    resources/ui/Carlito-Regular.ttf
)
if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/resources/ui/calibril.ttf")
    list(APPEND CYMBALIS_RESOURCES resources/ui/calibril.ttf)
endif()
juce_add_binary_data(CymbalisBinaryData
    HEADER_NAME BinaryData.h
    NAMESPACE BinaryData
    SOURCES ${CYMBALIS_RESOURCES}
)
set_target_properties(CymbalisBinaryData PROPERTIES POSITION_INDEPENDENT_CODE ON)

set(CYMBALIS_PLUGIN_SOURCES
    plugin/PluginProcessor.cpp
    plugin/PluginEditor.cpp
    plugin/ui/UiAssets.cpp
)
target_sources(${CYMBALIS_TARGET} PRIVATE ${CYMBALIS_PLUGIN_SOURCES})
if(CYMBALIS_TUNING)
    target_sources(${CYMBALIS_TARGET} PRIVATE
        plugin/tuning/TuningFields.cpp
        plugin/tuning/TuningProcessor.cpp
        plugin/tuning/TuningWindow.cpp
    )
    if(APPLE)
        target_sources(${CYMBALIS_TARGET} PRIVATE plugin/tuning/TuningWindowMac.mm)
    endif()
    target_include_directories(${CYMBALIS_TARGET} PRIVATE plugin/tuning)
endif()

target_include_directories(${CYMBALIS_TARGET} PRIVATE plugin)

target_compile_definitions(${CYMBALIS_TARGET} PUBLIC
    JUCE_WEB_BROWSER=0
    JUCE_USE_CURL=0
    JUCE_VST3_CAN_REPLACE_VST2=0
)

# ---------------------------------------------------------------------------
# Architecture flags
#   x86_64 slice is capped at SSE4.1 so the plugin never emits AVX
#   (port spec risk: AVX under Rosetta 2 = hard crash).
#   -Xarch_x86_64 applies the flag to the Intel slice only in universal builds.
# ---------------------------------------------------------------------------
if(APPLE)
    target_compile_options(${CYMBALIS_TARGET} PRIVATE -Xarch_x86_64 -msse4.1)
endif()

target_link_libraries(${CYMBALIS_TARGET}
    PRIVATE
        CymbalisBinaryData
        ${CYMBALIS_DSP}
        juce::juce_audio_utils
        juce::juce_audio_processors
        juce::juce_dsp
    PUBLIC
        juce::juce_recommended_config_flags
        juce::juce_recommended_lto_flags
        juce::juce_recommended_warning_flags
)

# The editor snapshot and the host mouse test are tools of the normal build.
if(CYMBALIS_TUNING)
    return()
endif()

# ---------------------------------------------------------------------------
# CymbalisSnapshot: renders the editor to PNG (CI uploads 1536x1024 and 960x640, and one
# 1536x1024 PNG per preset)
# ---------------------------------------------------------------------------
juce_add_console_app(CymbalisSnapshot)
target_sources(CymbalisSnapshot PRIVATE tools/cymbalis_snapshot/main.cpp ${CYMBALIS_PLUGIN_SOURCES})
target_include_directories(CymbalisSnapshot PRIVATE plugin)
target_compile_definitions(CymbalisSnapshot PRIVATE
    JucePlugin_Name="Cymbalis"
    JUCE_WEB_BROWSER=0
    JUCE_USE_CURL=0
)
if(APPLE)
    target_compile_options(CymbalisSnapshot PRIVATE -Xarch_x86_64 -msse4.1)
endif()
target_link_libraries(CymbalisSnapshot
    PRIVATE
        CymbalisBinaryData
        cymbalis_fast
        juce::juce_audio_utils
        juce::juce_audio_processors
    PUBLIC
        juce::juce_recommended_config_flags
        juce::juce_recommended_warning_flags
)

# ---------------------------------------------------------------------------
# CymbalisUiTest (macOS): hosts the built AU or VST3 with JUCE's hosting code,
# posts real mouse events to its editor window and checks the result in the
# plugin's saved state. CI runs it on both formats.
# ---------------------------------------------------------------------------
if(APPLE)
    juce_add_gui_app(CymbalisUiTest)
    target_sources(CymbalisUiTest PRIVATE
        tools/cymbalis_uitest/main.cpp
        tools/cymbalis_uitest/MacEvents.mm
    )
    target_include_directories(CymbalisUiTest PRIVATE tools/cymbalis_uitest)
    target_compile_definitions(CymbalisUiTest PRIVATE
        JUCE_PLUGINHOST_VST3=1
        JUCE_PLUGINHOST_AU=1
        JUCE_WEB_BROWSER=0
        JUCE_USE_CURL=0
    )
    target_link_libraries(CymbalisUiTest
        PRIVATE
            juce::juce_audio_utils
            juce::juce_audio_processors
        PUBLIC
            juce::juce_recommended_config_flags
            juce::juce_recommended_warning_flags
    )
endif()
