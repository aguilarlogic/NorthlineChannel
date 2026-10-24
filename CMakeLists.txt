cmake_minimum_required(VERSION 3.25)

# macOS: universal binary (Apple silicon + Intel). Set before project().
set(CMAKE_OSX_DEPLOYMENT_TARGET "11.0" CACHE STRING "Minimum macOS version")
set(CMAKE_OSX_ARCHITECTURES "arm64;x86_64" CACHE STRING "macOS architectures")

project(NorthlineChannel VERSION 0.9.1 LANGUAGES C CXX)

# CLAP through free-audio/clap-juce-extensions (JUCE 9.0.3 has no CLAP client of its own).
# Enabled automatically when the checkout exists:
#   git clone --recursive https://github.com/free-audio/clap-juce-extensions external/clap-juce-extensions
set(NL_CLAP_DIR "${CMAKE_CURRENT_SOURCE_DIR}/external/clap-juce-extensions")
if(EXISTS "${NL_CLAP_DIR}/CMakeLists.txt")
    option(NL_BUILD_CLAP "Build the CLAP plug-in" ON)
else()
    option(NL_BUILD_CLAP "Build the CLAP plug-in" OFF)
endif()

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")

add_subdirectory(external/JUCE)
if(NL_BUILD_CLAP)
    add_subdirectory("${NL_CLAP_DIR}" EXCLUDE_FROM_ALL)
endif()

set(NL_FORMATS VST3 Standalone)
if(APPLE)
    list(APPEND NL_FORMATS AU)
endif()

# Sources shared by the plugin and the test/benchmark tool. New files are picked up
# automatically (re-run CMake configure after adding one).
file(GLOB_RECURSE NL_SOURCES CONFIGURE_DEPENDS Source/*.cpp)
file(GLOB NL_TOOL_SOURCES CONFIGURE_DEPENDS tools/*.cpp tools/tests/*.cpp tools/measure/*.cpp)

set(NL_DEFINITIONS
    JUCE_WEB_BROWSER=0
    JUCE_USE_CURL=0
    JUCE_VST3_CAN_REPLACE_VST2=0
    JUCE_USE_XINPUT=0)

juce_add_plugin(NorthlineChannel
    COMPANY_NAME "Northline Audio"
    COMPANY_WEBSITE "https://example.com" # TODO(release): the real domain once registered (roadmap 11.6)
    BUNDLE_ID com.northlineaudio.channel
    PLUGIN_MANUFACTURER_CODE Nrth
    PLUGIN_CODE Nlch
    FORMATS ${NL_FORMATS}
    PRODUCT_NAME "Northline Channel"
    DESCRIPTION "Zero-latency channel strip"
    IS_SYNTH FALSE
    NEEDS_MIDI_INPUT FALSE
    NEEDS_MIDI_OUTPUT FALSE
    IS_MIDI_EFFECT FALSE
    EDITOR_WANTS_KEYBOARD_FOCUS TRUE
    COPY_PLUGIN_AFTER_BUILD FALSE
    VST3_CATEGORIES Fx Dynamics EQ
    AU_MAIN_TYPE kAudioUnitType_Effect
    NEEDS_WEB_BROWSER FALSE
    NEEDS_CURL FALSE
    HARDENED_RUNTIME_ENABLED TRUE)

if(NL_BUILD_CLAP)
    clap_juce_extensions_plugin(TARGET NorthlineChannel
        CLAP_ID "com.northlineaudio.channel"
        CLAP_FEATURES audio-effect equalizer compressor limiter distortion mono stereo)
endif()

target_sources(NorthlineChannel PRIVATE ${NL_SOURCES})
target_include_directories(NorthlineChannel PRIVATE Source)
target_compile_definitions(NorthlineChannel PUBLIC ${NL_DEFINITIONS})
target_link_libraries(NorthlineChannel
    PRIVATE
        juce::juce_audio_utils
        juce::juce_dsp
    PUBLIC
        juce::juce_recommended_config_flags
        juce::juce_recommended_lto_flags
        juce::juce_recommended_warning_flags)

# Console tool: unit tests, CPU benchmark, and benchmarking of any other VST3 for comparison.
juce_add_console_app(NorthlineTool PRODUCT_NAME "NorthlineTool")
target_sources(NorthlineTool PRIVATE ${NL_TOOL_SOURCES} ${NL_SOURCES})
target_include_directories(NorthlineTool PRIVATE Source tools)
target_compile_definitions(NorthlineTool PRIVATE ${NL_DEFINITIONS} JUCE_PLUGINHOST_VST3=1 NL_TOOL_BUILD=1)
target_link_libraries(NorthlineTool
    PRIVATE
        juce::juce_audio_utils
        juce::juce_dsp
    PUBLIC
        juce::juce_recommended_config_flags
        juce::juce_recommended_lto_flags
        juce::juce_recommended_warning_flags)

# Inter (SIL Open Font License). Put Inter-Regular.ttf and Inter-Bold.ttf in Resources/fonts/ and
# re-run CMake; LookAndFeel.cpp picks them up through NorthlineFontData.h. Without them it falls back
# to the system font.
file(GLOB NL_INTER_FONTS CONFIGURE_DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/Resources/fonts/Inter-*.ttf")
if(NL_INTER_FONTS)
    juce_add_binary_data(NorthlineFonts HEADER_NAME NorthlineFontData.h NAMESPACE NorthlineFonts SOURCES ${NL_INTER_FONTS})
    target_link_libraries(NorthlineChannel PRIVATE NorthlineFonts)
    target_link_libraries(NorthlineTool PRIVATE NorthlineFonts)
endif()

# AddressSanitizer for the test tool (MSVC): configure a separate build directory with -DNL_ASAN=ON, e.g.
#   cmake -S . -B build-asan -DNL_ASAN=ON
# The sanitizer needs frame pointers (/Oy-), debug information for the reports (/Zi) and no incremental
# linking. Off by default; the plug-in is not affected.
option(NL_ASAN "Build NorthlineTool with AddressSanitizer (MSVC)" OFF)
if(NL_ASAN)
    if(NOT MSVC)
        message(FATAL_ERROR "NL_ASAN is only supported with MSVC")
    endif()
    target_compile_options(NorthlineTool PRIVATE /fsanitize=address /Oy- /Zi)
    target_link_options(NorthlineTool PRIVATE /INCREMENTAL:NO /DEBUG)
endif()
