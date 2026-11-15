cmake_minimum_required(VERSION 3.22)

project(AnvilFX VERSION 0.1.0)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Universal binary on Mac (Apple Silicon + Intel)
if(APPLE)
    set(CMAKE_OSX_ARCHITECTURES "arm64;x86_64" CACHE STRING "" FORCE)
    set(CMAKE_OSX_DEPLOYMENT_TARGET "10.13" CACHE STRING "" FORCE)
endif()

# Use a local JUCE checkout if given (-DJUCE_DIR=...), otherwise download it.
if(DEFINED JUCE_DIR)
    add_subdirectory(${JUCE_DIR} JUCE)
else()
    include(FetchContent)
    FetchContent_Declare(
        JUCE
        GIT_REPOSITORY https://github.com/juce-framework/JUCE.git
        GIT_TAG 8.0.4
        GIT_SHALLOW TRUE)
    FetchContent_MakeAvailable(JUCE)
endif()

set(ANVIL_FORMATS VST3)
if(APPLE)
    list(APPEND ANVIL_FORMATS AU)
endif()
if(DEFINED ANVIL_EXTRA_FORMATS)
    list(APPEND ANVIL_FORMATS ${ANVIL_EXTRA_FORMATS})
endif()

juce_add_plugin(AnvilFX
    COMPANY_NAME "Anzyeity"
    COMPANY_WEBSITE "https://anzyeity.com"
    BUNDLE_ID com.anzyeity.anvilfx
    PLUGIN_MANUFACTURER_CODE Anzy
    PLUGIN_CODE Anvf
    FORMATS ${ANVIL_FORMATS}
    PRODUCT_NAME "Anvil FX"
    IS_SYNTH FALSE
    NEEDS_MIDI_INPUT FALSE
    NEEDS_MIDI_OUTPUT FALSE
    IS_MIDI_EFFECT FALSE
    EDITOR_WANTS_KEYBOARD_FOCUS FALSE
    COPY_PLUGIN_AFTER_BUILD FALSE
    VST3_CATEGORIES Fx Modulation
    AU_MAIN_TYPE kAudioUnitType_Effect)

juce_generate_juce_header(AnvilFX)

target_sources(AnvilFX PRIVATE
    Source/PluginProcessor.cpp
    Source/PluginEditor.cpp
    Source/Engine.cpp
    Source/Presets.cpp)

# Anvil character frames (PNG files in Assets/anvil). Optional.
file(GLOB ANVIL_ASSETS CONFIGURE_DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/Assets/anvil/*.png")
if(ANVIL_ASSETS)
    juce_add_binary_data(AnvilAssets SOURCES ${ANVIL_ASSETS})
    target_link_libraries(AnvilFX PRIVATE AnvilAssets)
    target_compile_definitions(AnvilFX PUBLIC ANVIL_HAS_ASSETS=1)
else()
    target_compile_definitions(AnvilFX PUBLIC ANVIL_HAS_ASSETS=0)
endif()

target_compile_definitions(AnvilFX PUBLIC
    JUCE_WEB_BROWSER=0
    JUCE_USE_CURL=0
    JUCE_VST3_CAN_REPLACE_VST2=0
    JUCE_DISPLAY_SPLASH_SCREEN=0)

target_link_libraries(AnvilFX
    PRIVATE
        juce::juce_audio_processors
        juce::juce_gui_extra
        juce::juce_audio_formats
        juce::juce_dsp
    PUBLIC
        juce::juce_recommended_config_flags
        juce::juce_recommended_lto_flags
        juce::juce_recommended_warning_flags)

# Linux boxes without the X11 extension headers (only used for local testing)
if(UNIX AND NOT APPLE)
    target_compile_definitions(AnvilFX PUBLIC JUCE_USE_XRANDR=0 JUCE_USE_XINERAMA=0 JUCE_USE_XCURSOR=0 JUCE_USE_XRENDER=0)
endif()

# Offline tests: cmake -DANVIL_BUILD_TESTS=ON
option(ANVIL_BUILD_TESTS "Build the offline audio tests" OFF)
if(ANVIL_BUILD_TESTS)
    juce_add_console_app(AnvilTests PRODUCT_NAME "AnvilTests")
    juce_generate_juce_header(AnvilTests)
    target_sources(AnvilTests PRIVATE
        Tests/Main.cpp
        Source/PluginProcessor.cpp
        Source/PluginEditor.cpp
        Source/Engine.cpp
        Source/Presets.cpp)
    target_include_directories(AnvilTests PRIVATE Source)
    target_compile_definitions(AnvilTests PRIVATE
        ANVIL_HAS_ASSETS=0 JUCE_WEB_BROWSER=0 JUCE_USE_CURL=0
        JUCE_USE_XRANDR=0 JUCE_USE_XINERAMA=0 JUCE_USE_XCURSOR=0 JUCE_USE_XRENDER=0)
    target_link_libraries(AnvilTests PRIVATE
        juce::juce_audio_processors juce::juce_gui_extra juce::juce_audio_formats juce::juce_dsp
        juce::juce_recommended_config_flags)
endif()
