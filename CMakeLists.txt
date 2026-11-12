cmake_minimum_required(VERSION 3.22)
set(FINDNOTE_VERSION "1.0.0" CACHE STRING "Version embedded in the plugin")
project(FindNote VERSION ${FINDNOTE_VERSION} LANGUAGES C CXX)
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_OSX_DEPLOYMENT_TARGET "11.0" CACHE STRING "Minimum macOS")

include(CTest)
add_library(findnote_pitch Source/PitchDetector.cpp)
target_include_directories(findnote_pitch PUBLIC Source)
if(BUILD_TESTING)
    add_executable(pitch_tests Tests/PitchTests.cpp)
    target_link_libraries(pitch_tests PRIVATE findnote_pitch)
    add_test(NAME pitch_detection COMMAND pitch_tests)
endif()

option(FINDNOTE_BUILD_PLUGIN "Build JUCE audio plugins" ON)
option(FINDNOTE_BUILD_VST2 "Build legacy VST2 using a separately supplied SDK" OFF)
option(FINDNOTE_BUILD_AAX "Build experimental AAX (PACE signing is separate)" OFF)
set(FINDNOTE_VST2_SDK_PATH "" CACHE PATH "VST2 SDK root containing pluginterfaces/vst2.x")
if(FINDNOTE_BUILD_VST2 AND (NOT EXISTS "${FINDNOTE_VST2_SDK_PATH}/pluginterfaces/vst2.x/aeffect.h"
        OR NOT EXISTS "${FINDNOTE_VST2_SDK_PATH}/pluginterfaces/vst2.x/aeffectx.h"))
    message(FATAL_ERROR "VST2 requires FINDNOTE_VST2_SDK_PATH with a legally obtained legacy SDK. VST3 does not require it.")
endif()
if(FINDNOTE_BUILD_PLUGIN)
    include(FetchContent)
    # JUCE 8.0.17-security, fixed revision for reproducible builds.
    FetchContent_Declare(JUCE
        GIT_REPOSITORY https://github.com/juce-framework/JUCE.git
        GIT_TAG dabd5ad13cd869cba41046947fffc8e5ce5cd662)
    FetchContent_MakeAvailable(JUCE)
    set(findnote_formats VST3)
    if(APPLE)
        list(APPEND findnote_formats AU)
    endif()
    if(FINDNOTE_BUILD_VST2)
        juce_set_vst2_sdk_path("${FINDNOTE_VST2_SDK_PATH}")
        list(APPEND findnote_formats VST)
    endif()
    if(FINDNOTE_BUILD_AAX)
        # This pinned JUCE revision already bundles the AAX SDK.
        list(APPEND findnote_formats AAX)
    endif()
    juce_add_binary_data(FindNoteAssets SOURCES
        assets/cuphead.jpeg
        assets/fonts/LuckiestGuy-Regular.ttf)
    juce_add_plugin(FindNote
        COMPANY_NAME "FindNote"
        BUNDLE_ID "audio.findnote.tuner"
        PLUGIN_MANUFACTURER_CODE Fdnt
        PLUGIN_CODE Fnt1
        FORMATS ${findnote_formats}
        PRODUCT_NAME "FindNote"
        IS_SYNTH FALSE
        NEEDS_MIDI_INPUT FALSE
        NEEDS_MIDI_OUTPUT FALSE
        IS_MIDI_EFFECT FALSE
        COPY_PLUGIN_AFTER_BUILD FALSE
        VST3_CATEGORIES Fx Analyzer)
    if(APPLE)
        # Embed the font once; copy only its license and attribution as resources.
        foreach(format IN LISTS findnote_formats)
            target_sources(FindNote_${format} PRIVATE assets/fonts/LICENSE.txt assets/fonts/NOTICE.txt)
        endforeach()
        set_source_files_properties(assets/fonts/LICENSE.txt assets/fonts/NOTICE.txt PROPERTIES
            HEADER_FILE_ONLY TRUE MACOSX_PACKAGE_LOCATION Resources/licenses)
    endif()
    target_sources(FindNote PRIVATE Source/PluginProcessor.cpp Source/PluginEditor.cpp)
    target_compile_definitions(FindNote PUBLIC
        JUCE_WEB_BROWSER=0 JUCE_USE_CURL=0 JUCE_VST3_CAN_REPLACE_VST2=0)
    target_link_libraries(FindNote PRIVATE findnote_pitch FindNoteAssets
        juce::juce_audio_utils
        PUBLIC juce::juce_recommended_config_flags juce::juce_recommended_warning_flags)
    if(BUILD_TESTING)
        add_executable(processor_tests Tests/ProcessorTests.cpp)
        target_include_directories(processor_tests PRIVATE Source $<TARGET_PROPERTY:FindNote,INCLUDE_DIRECTORIES>)
        target_compile_definitions(processor_tests PRIVATE $<TARGET_PROPERTY:FindNote,COMPILE_DEFINITIONS>)
        target_link_libraries(processor_tests PRIVATE FindNote)
        add_test(NAME processor_integration COMMAND processor_tests)
    endif()
endif()
