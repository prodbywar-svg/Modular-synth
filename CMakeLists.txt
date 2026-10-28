cmake_minimum_required(VERSION 3.22)

project(ModularSynth VERSION 0.1.0 LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Static MSVC runtime so the Windows .exe runs without installing anything else.
set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")

option(MS_BUILD_VST3 "Also build the VST3 plugin (Milestone 3)" OFF)
option(MS_BUILD_TESTS "Build the engine tests" ON)

# Use a local JUCE checkout if JUCE_DIR is given, otherwise download it.
set(JUCE_DIR "" CACHE PATH "Path to a local JUCE checkout")
if(JUCE_DIR)
    add_subdirectory(${JUCE_DIR} JUCE EXCLUDE_FROM_ALL)
else()
    include(FetchContent)
    FetchContent_Declare(JUCE
        GIT_REPOSITORY https://github.com/juce-framework/JUCE.git
        GIT_TAG 8.0.10
        GIT_SHALLOW ON)
    FetchContent_MakeAvailable(JUCE)
endif()

set(ENGINE_SOURCES
    Source/engine/Params.cpp
    Source/engine/ProjectState.cpp
    Source/engine/Voice.cpp
    Source/engine/FxChain.cpp
    Source/engine/Effects.cpp
    Source/engine/Hr16Bank.cpp
    Source/engine/EngineCore.cpp
    Source/engine/StudioState.cpp
    Source/engine/StudioEngine.cpp
    Source/engine/OfflineRenderer.cpp)

# The Alesis HR-16 sounds are compiled into the app.
file(GLOB HR16_WAVS CONFIGURE_DEPENDS ${CMAKE_CURRENT_SOURCE_DIR}/assets/hr16/*.wav)
juce_add_binary_data(Hr16Samples SOURCES ${HR16_WAVS})
set_target_properties(Hr16Samples PROPERTIES POSITION_INDEPENDENT_CODE ON)

set(ENGINE_MODULES
    juce::juce_core
    juce::juce_audio_basics
    juce::juce_audio_formats
    Hr16Samples)

# ---------------------------------------------------------------- app
set(MS_FORMATS Standalone)
if(MS_BUILD_VST3)
    list(APPEND MS_FORMATS VST3)
endif()

juce_add_plugin(ModularSynth
    PRODUCT_NAME "Modular-synth"
    COMPANY_NAME "prodbywar"
    BUNDLE_ID com.prodbywar.modularsynth
    IS_SYNTH TRUE
    NEEDS_MIDI_INPUT TRUE
    NEEDS_MIDI_OUTPUT FALSE
    IS_MIDI_EFFECT FALSE
    EDITOR_WANTS_KEYBOARD_FOCUS TRUE
    PLUGIN_MANUFACTURER_CODE Pbwr
    PLUGIN_CODE Msyn
    FORMATS ${MS_FORMATS})

target_sources(ModularSynth PRIVATE
    ${ENGINE_SOURCES}
    Source/app/PluginProcessor.cpp
    Source/app/PluginEditor.cpp
    Source/app/DrumMachineView.cpp
    Source/app/Shared.cpp
    Source/app/Widgets.cpp)

target_include_directories(ModularSynth PRIVATE Source)

target_compile_definitions(ModularSynth PUBLIC
    JUCE_WEB_BROWSER=0
    JUCE_USE_CURL=0
    JUCE_VST3_CAN_REPLACE_VST2=0
    JUCE_DISPLAY_SPLASH_SCREEN=0)

target_link_libraries(ModularSynth
    PRIVATE
        ${ENGINE_MODULES}
        juce::juce_audio_utils
        juce::juce_gui_basics
    PUBLIC
        juce::juce_recommended_config_flags
        juce::juce_recommended_lto_flags
        juce::juce_recommended_warning_flags)

# ---------------------------------------------------------------- tests
if(MS_BUILD_TESTS)
    juce_add_console_app(EngineTests PRODUCT_NAME "EngineTests")
    target_sources(EngineTests PRIVATE ${ENGINE_SOURCES} tests/EngineTests.cpp)
    target_include_directories(EngineTests PRIVATE Source)
    target_compile_definitions(EngineTests PRIVATE
        JUCE_WEB_BROWSER=0
        JUCE_USE_CURL=0
        JUCE_STANDALONE_APPLICATION=1)
    target_link_libraries(EngineTests
        PRIVATE ${ENGINE_MODULES}
        PUBLIC juce::juce_recommended_config_flags juce::juce_recommended_warning_flags)
endif()
