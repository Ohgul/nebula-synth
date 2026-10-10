cmake_minimum_required(VERSION 3.22)

# En Mac: genera un binario universal (Apple Silicon + Intel)
set(CMAKE_OSX_ARCHITECTURES "arm64;x86_64" CACHE STRING "")
set(CMAKE_OSX_DEPLOYMENT_TARGET "11.0" CACHE STRING "")

project(NebulaSynth VERSION 3.0.0)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# JUCE se descarga automáticamente al compilar
include(FetchContent)
FetchContent_Declare(
    JUCE
    GIT_REPOSITORY https://github.com/juce-framework/JUCE.git
    GIT_TAG        8.0.9
    GIT_SHALLOW    TRUE
)
FetchContent_MakeAvailable(JUCE)

set(NEBULA_SOURCES
    Source/PluginProcessor.cpp
    Source/PluginEditor.cpp
    Source/GardenView.cpp
    Source/Views.cpp
)

juce_add_plugin(NebulaSynth
    COMPANY_NAME                "Remacdent"
    BUNDLE_ID                   "com.remacdent.nebulasynth"
    PLUGIN_MANUFACTURER_CODE    "Rmcd"
    PLUGIN_CODE                 "Nbl1"
    PRODUCT_NAME                "Nebula"
    FORMATS                     AU VST3 Standalone
    IS_SYNTH                    TRUE
    NEEDS_MIDI_INPUT            TRUE
    NEEDS_MIDI_OUTPUT           FALSE
    IS_MIDI_EFFECT              FALSE
    EDITOR_WANTS_KEYBOARD_FOCUS FALSE
    COPY_PLUGIN_AFTER_BUILD     FALSE
    VST3_CATEGORIES             Instrument Synth
    AU_MAIN_TYPE                kAudioUnitType_MusicDevice
)

target_sources(NebulaSynth PRIVATE ${NEBULA_SOURCES})

target_compile_definitions(NebulaSynth PUBLIC
    JUCE_WEB_BROWSER=0
    JUCE_USE_CURL=0
    JUCE_VST3_CAN_REPLACE_VST2=0
    JUCE_DISPLAY_SPLASH_SCREEN=0
)

target_link_libraries(NebulaSynth
    PRIVATE
        juce::juce_audio_utils
        juce::juce_dsp
    PUBLIC
        juce::juce_recommended_config_flags
        juce::juce_recommended_lto_flags
        juce::juce_recommended_warning_flags
)

# ---- Prueba opcional (renderiza audio a WAV sin abrir un DAW) ----
option(NEBULA_BUILD_TESTS "Compilar las pruebas sin DAW" OFF)
if(NEBULA_BUILD_TESTS)
    foreach(t Render Imitate V2 V21 V3 Ui)
        juce_add_console_app(Nebula${t}Test PRODUCT_NAME "Nebula${t}Test")
        target_sources(Nebula${t}Test PRIVATE ${NEBULA_SOURCES} tests/${t}Test.cpp)
        target_compile_definitions(Nebula${t}Test PRIVATE
            JUCE_WEB_BROWSER=0 JUCE_USE_CURL=0 JUCE_DISPLAY_SPLASH_SCREEN=0 JUCE_MODAL_LOOPS_PERMITTED=1)
        target_link_libraries(Nebula${t}Test PRIVATE
            juce::juce_audio_utils juce::juce_dsp juce::juce_recommended_config_flags)
    endforeach()
endif()
