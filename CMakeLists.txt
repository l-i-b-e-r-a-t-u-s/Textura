cmake_minimum_required(VERSION 3.22)

project(Textura VERSION 1.0.1 LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_OSX_DEPLOYMENT_TARGET "11.0" CACHE STRING "Minimum macOS version")
set(CMAKE_OSX_ARCHITECTURES "arm64;x86_64" CACHE STRING "Build for Apple silicon and Intel")
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)
set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")

# JUCE 9.0.2: an installed copy if there is one, else a source tree, else fetched.
set(JUCE_SOURCE_DIR "$ENV{HOME}/JUCE" CACHE PATH "JUCE source tree")
if(NOT WIN32 AND NOT CMAKE_CROSSCOMPILING)
    find_package(JUCE 9.0.2 CONFIG QUIET HINTS "$ENV{HOME}/JUCE/install")
endif()
if(NOT JUCE_FOUND)
    if(EXISTS "${JUCE_SOURCE_DIR}/CMakeLists.txt")
        add_subdirectory("${JUCE_SOURCE_DIR}" JUCE)
    else()
        include(FetchContent)
        FetchContent_Declare(JUCE GIT_REPOSITORY https://github.com/juce-framework/JUCE.git GIT_TAG 9.0.2 GIT_SHALLOW TRUE)
        FetchContent_MakeAvailable(JUCE)
    endif()
endif()

# Keep the build machine's folder names out of the shipped binaries.
if(NOT MSVC)
    add_compile_options("-ffile-prefix-map=${CMAKE_SOURCE_DIR}=liberatus" "-ffile-prefix-map=$ENV{HOME}=~")
endif()

juce_add_binary_data(TexturaAssets SOURCES assets/Inter-Bold.ttf assets/creature.png)
set_target_properties(TexturaAssets PROPERTIES POSITION_INDEPENDENT_CODE ON)

if(APPLE)
    set(LIBERATUS_FORMATS AU VST3 Standalone)
else()
    set(LIBERATUS_FORMATS VST3 Standalone)
endif()

set(LIBERATUS_SOURCES
    Source/PluginProcessor.cpp
    Source/PluginEditor.cpp)

juce_add_plugin(Textura
    COMPANY_NAME "Liberatus"
    BUNDLE_ID org.liberatus.textura
    PLUGIN_MANUFACTURER_CODE Lbts
    PLUGIN_CODE Txtr
    FORMATS ${LIBERATUS_FORMATS}
    PRODUCT_NAME "Textura"
    ICON_BIG "${CMAKE_CURRENT_SOURCE_DIR}/assets/icon.png"
    IS_SYNTH TRUE
    NEEDS_MIDI_INPUT TRUE
    NEEDS_MIDI_OUTPUT FALSE
    IS_MIDI_EFFECT FALSE
    EDITOR_WANTS_KEYBOARD_FOCUS TRUE
    COPY_PLUGIN_AFTER_BUILD FALSE   # install explicitly with tools/install.sh
    VST3_CATEGORIES Instrument Synth
    AU_MAIN_TYPE kAudioUnitType_MusicDevice)

target_sources(Textura PRIVATE ${LIBERATUS_SOURCES})

target_compile_definitions(Textura PUBLIC
    JUCE_WEB_BROWSER=0
    JUCE_USE_CURL=0
    JUCE_VST3_CAN_REPLACE_VST2=0)

target_link_libraries(Textura
    PRIVATE
        TexturaAssets
        juce::juce_audio_utils
        juce::juce_dsp
    PUBLIC
        juce::juce_recommended_config_flags
        juce::juce_recommended_lto_flags
        juce::juce_recommended_warning_flags)

# Offline harness: sweeps a fake pointer across the wordmark, writes the editor as a PNG and
# the audio as a WAV. Lets us test without a DAW or an audio device.
juce_add_console_app(TexturaSnapshot PRODUCT_NAME "TexturaSnapshot")

target_sources(TexturaSnapshot PRIVATE tools/Snapshot.cpp ${LIBERATUS_SOURCES})

target_compile_definitions(TexturaSnapshot PRIVATE
    LIBERATUS_HEADLESS=1
    JUCE_WEB_BROWSER=0
    JUCE_USE_CURL=0)

target_link_libraries(TexturaSnapshot
    PRIVATE
        TexturaAssets
        juce::juce_audio_utils
        juce::juce_dsp
    PUBLIC
        juce::juce_recommended_config_flags
        juce::juce_recommended_warning_flags)
