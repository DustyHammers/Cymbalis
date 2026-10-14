cmake_minimum_required(VERSION 3.22)

# Must be set before project() so it applies to every target, including JUCE's helpers.
set(CMAKE_OSX_DEPLOYMENT_TARGET "11.0" CACHE STRING "Minimum macOS version")

project(Cymbalis VERSION 0.1.0 LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# OFF builds only the DSP engine and its console tools, without fetching JUCE
# (used by the A/B render job).
option(CYMBALIS_BUILD_PLUGIN "Build the JUCE plugin (VST3/AU)" ON)

# ---------------------------------------------------------------------------
# DSP engine (C++ port of the Python V19 engine) and the A/B console renderer
# ---------------------------------------------------------------------------
add_subdirectory(plugin/dsp)
add_subdirectory(tools/cymbalis_render)
add_subdirectory(tools/cymbalis_bench)

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

juce_add_plugin(Cymbalis
    COMPANY_NAME                "DHammers"
    BUNDLE_ID                   "com.dhammers.cymbalis"
    PLUGIN_MANUFACTURER_CODE    Dham
    PLUGIN_CODE                 Cymb
    PRODUCT_NAME                "Cymbalis"
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

target_sources(Cymbalis PRIVATE
    plugin/PluginProcessor.cpp
    plugin/PluginEditor.cpp
)

target_include_directories(Cymbalis PRIVATE plugin)

target_compile_definitions(Cymbalis PUBLIC
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
    target_compile_options(Cymbalis PRIVATE -Xarch_x86_64 -msse4.1)
endif()

target_link_libraries(Cymbalis
    PRIVATE
        juce::juce_audio_utils
        juce::juce_audio_processors
        juce::juce_dsp
    PUBLIC
        juce::juce_recommended_config_flags
        juce::juce_recommended_lto_flags
        juce::juce_recommended_warning_flags
)
