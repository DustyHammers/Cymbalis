cmake_minimum_required(VERSION 3.22)

# Must be set before project() so it applies to every target, including JUCE's helpers.
set(CMAKE_OSX_DEPLOYMENT_TARGET "11.0" CACHE STRING "Minimum macOS version")

project(Cymbalis VERSION 0.1.0 LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# OFF builds only the DSP engine and its console tools, without fetching JUCE
# (used by the A/B render job).
option(CYMBALIS_BUILD_PLUGIN "Build the JUCE plugin (VST3/AU)" ON)

# ON (the default): Cymbalis has the Tuning window, live tuning, tuning files and tuning in
# the session (docs/TUNING.md). OFF builds a lean Cymbalis without any of the tuning code,
# under the same identity. The code tests the macro CYMBALIS_TUNING, which the engine
# library (plugin/dsp) passes on to everything that links it.
option(CYMBALIS_WITH_TUNING "Build Cymbalis with the Tuning window" ON)

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

# ---------------------------------------------------------------------------
# Packed presets. The JSON bakes in resources/presets/ (nl_amount 0, written by
# tools/bake_preset.py) are the source; at build time tools/pack_bake packs each into the
# compact form the plugin embeds (plugin/PackedBake.h, docs/BUILDS.md).
# ---------------------------------------------------------------------------
juce_add_console_app(CymbalisPackBake PRODUCT_NAME "cymbalis_pack_bake")
target_sources(CymbalisPackBake PRIVATE tools/pack_bake/main.cpp plugin/PackedBake.cpp)
target_include_directories(CymbalisPackBake PRIVATE plugin)
target_compile_definitions(CymbalisPackBake PRIVATE JUCE_WEB_BROWSER=0 JUCE_USE_CURL=0)
target_link_libraries(CymbalisPackBake
    PRIVATE
        cymbalis_engine
        juce::juce_core
    PUBLIC
        juce::juce_recommended_config_flags
        juce::juce_recommended_warning_flags
)

set(CYMBALIS_PACKED_PRESETS)
foreach(preset nova_ride smoke_flat_ride hammered_thin_ride swish raven)
    set(json "${CMAKE_CURRENT_SOURCE_DIR}/resources/presets/${preset}.bake.json")
    set(packed "${CMAKE_CURRENT_BINARY_DIR}/presets/${preset}.bake.pack")
    add_custom_command(OUTPUT "${packed}"
        COMMAND "${CMAKE_COMMAND}" -E make_directory "${CMAKE_CURRENT_BINARY_DIR}/presets"
        COMMAND CymbalisPackBake "${json}" "${packed}"
        DEPENDS CymbalisPackBake "${json}"
        COMMENT "Packing ${preset}.bake.json"
        VERBATIM)
    list(APPEND CYMBALIS_PACKED_PRESETS "${packed}")
endforeach()

# ---------------------------------------------------------------------------
# Compiled-in resources: the packed presets, and the UI artwork and font. The presets' photos
# in resources/ui/ are references only: the editor draws a wireframe cymbal instead
# (docs/UI_SPEC.md section 11). Calibri Light is used when resources/ui/calibril.ttf exists;
# Carlito Regular stands in until then.
# ---------------------------------------------------------------------------
set(CYMBALIS_RESOURCES
    ${CYMBALIS_PACKED_PRESETS}
    resources/ui/CYMBALIS_Background_logo.png
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
    plugin/PackedBake.cpp
    plugin/ui/CymbalModel.cpp
    plugin/ui/CymbalView.cpp
    plugin/ui/HitDisplay.cpp
    plugin/ui/UiAssets.cpp
)
set(CYMBALIS_INCLUDE_DIRS plugin)
if(CYMBALIS_WITH_TUNING)
    list(APPEND CYMBALIS_PLUGIN_SOURCES
        plugin/tuning/TuningFields.cpp
        plugin/tuning/TuningProcessor.cpp
        plugin/tuning/TuningWindow.cpp
    )
    if(APPLE)
        list(APPEND CYMBALIS_PLUGIN_SOURCES plugin/tuning/TuningWindowMac.mm)
    endif()
    list(APPEND CYMBALIS_INCLUDE_DIRS plugin/tuning)
endif()
target_sources(Cymbalis PRIVATE ${CYMBALIS_PLUGIN_SOURCES})
target_include_directories(Cymbalis PRIVATE ${CYMBALIS_INCLUDE_DIRS})

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
        CymbalisBinaryData
        cymbalis_fast
        juce::juce_audio_utils
        juce::juce_audio_processors
        juce::juce_dsp
        juce::juce_opengl
    PUBLIC
        juce::juce_recommended_config_flags
        juce::juce_recommended_lto_flags
        juce::juce_recommended_warning_flags
)

# ---------------------------------------------------------------------------
# CymbalisSnapshot: renders the editor to PNG, 1536x1024 and 960x640 and one 1536x1024 PNG per
# preset (not run in CI; the cymbal drawn still, without OpenGL)
# ---------------------------------------------------------------------------
juce_add_console_app(CymbalisSnapshot)
target_sources(CymbalisSnapshot PRIVATE tools/cymbalis_snapshot/main.cpp ${CYMBALIS_PLUGIN_SOURCES})
target_include_directories(CymbalisSnapshot PRIVATE ${CYMBALIS_INCLUDE_DIRS})
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
        juce::juce_opengl
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
    # Its "hits" mode times Cymbalis's own processor and editor, so their sources are
    # compiled in too (as CymbalisSnapshot does).
    juce_add_gui_app(CymbalisUiTest)
    target_sources(CymbalisUiTest PRIVATE
        tools/cymbalis_uitest/main.cpp
        tools/cymbalis_uitest/MacEvents.mm
        tools/cymbalis_uitest/HitTiming.cpp
        ${CYMBALIS_PLUGIN_SOURCES}
    )
    target_include_directories(CymbalisUiTest PRIVATE tools/cymbalis_uitest ${CYMBALIS_INCLUDE_DIRS})
    target_compile_definitions(CymbalisUiTest PRIVATE
        JucePlugin_Name="Cymbalis"
        JUCE_PLUGINHOST_VST3=1
        JUCE_PLUGINHOST_AU=1
        JUCE_WEB_BROWSER=0
        JUCE_USE_CURL=0
    )
    target_compile_options(CymbalisUiTest PRIVATE -Xarch_x86_64 -msse4.1)
    target_link_libraries(CymbalisUiTest
        PRIVATE
            CymbalisBinaryData
            cymbalis_fast
            juce::juce_audio_utils
            juce::juce_audio_processors
            juce::juce_dsp
            juce::juce_opengl
        PUBLIC
            juce::juce_recommended_config_flags
            juce::juce_recommended_warning_flags
    )
endif()
