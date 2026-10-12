cmake_minimum_required(VERSION 3.22)

# En Mac: genera un binario universal (Apple Silicon + Intel)
set(CMAKE_OSX_ARCHITECTURES "arm64;x86_64" CACHE STRING "")
set(CMAKE_OSX_DEPLOYMENT_TARGET "11.0" CACHE STRING "")

project(NebulaSynth VERSION 3.1.0)

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
    NEBULA_VERSION_STRING="${PROJECT_VERSION}"
    JUCE_WEB_BROWSER=0
    JUCE_USE_CURL=0
    JUCE_VST3_CAN_REPLACE_VST2=0
    JUCE_DISPLAY_SPLASH_SCREEN=0
)

# MSVC: las fuentes están en UTF-8 (comentarios en español)
if(MSVC)
    target_compile_options(NebulaSynth PRIVATE /utf-8 /bigobj)
endif()

# FORGE: base de patches nativos con su huella sonora (generada con "NebulaTests ForgeDb generar")
set(NEBULA_FORGE_DATA "${CMAKE_CURRENT_SOURCE_DIR}/Resources/forge/nebula_dna.ndb")
if(EXISTS "${NEBULA_FORGE_DATA}")
    juce_add_binary_data(NebulaForgeData SOURCES "${NEBULA_FORGE_DATA}")
    set_target_properties(NebulaForgeData PROPERTIES POSITION_INDEPENDENT_CODE ON)
    target_link_libraries(NebulaSynth PRIVATE NebulaForgeData)
    target_compile_definitions(NebulaSynth PRIVATE NEBULA_HAS_FORGE_DATA=1)
endif()

target_link_libraries(NebulaSynth
    PRIVATE
        juce::juce_audio_utils
        juce::juce_dsp
    PUBLIC
        juce::juce_recommended_config_flags
        juce::juce_recommended_lto_flags
        juce::juce_recommended_warning_flags
)

# ---- Pruebas sin DAW ----
# Todas las pruebas van en un solo ejecutable (NebulaTests) para compilar el plugin y JUCE una vez:
#   NebulaTests <Nombre> [argumentos]      p. ej.  NebulaTests Stereo   /   NebulaTests Bench salida
option(NEBULA_BUILD_TESTS "Compilar las pruebas sin DAW" OFF)
if(NEBULA_BUILD_TESTS)
    set(NEBULA_TEST_NAMES "")
    set(NEBULA_TEST_FILES "")
    foreach(t Render Imitate V2 V21 V3 Ui Bench Stereo Determinism Midi Dna Transform Forge ForgeDb Genome Realtime)
        if(NOT EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/tests/${t}Test.cpp")
            message(STATUS "Prueba no encontrada (se omite): ${t}Test.cpp")
            continue()
        endif()
        list(APPEND NEBULA_TEST_NAMES ${t})
        list(APPEND NEBULA_TEST_FILES tests/${t}Test.cpp)
        set_source_files_properties(tests/${t}Test.cpp PROPERTIES COMPILE_DEFINITIONS "NEBULA_TEST_ENTRY=nebulaTest_${t}")
    endforeach()
    set(NEBULA_TEST_LIST "")
    foreach(t ${NEBULA_TEST_NAMES})
        string(APPEND NEBULA_TEST_LIST "NEBULA_TEST(${t})\n")
    endforeach()
    file(WRITE "${CMAKE_CURRENT_BINARY_DIR}/nebula_test_list.h" "${NEBULA_TEST_LIST}")

    juce_add_console_app(NebulaTests PRODUCT_NAME "NebulaTests")
    target_sources(NebulaTests PRIVATE ${NEBULA_SOURCES} ${NEBULA_TEST_FILES} tests/TestMain.cpp)
    target_include_directories(NebulaTests PRIVATE "${CMAKE_CURRENT_BINARY_DIR}")
    target_compile_definitions(NebulaTests PRIVATE
        NEBULA_VERSION_STRING="${PROJECT_VERSION}"
        JUCE_WEB_BROWSER=0 JUCE_USE_CURL=0 JUCE_DISPLAY_SPLASH_SCREEN=0 JUCE_MODAL_LOOPS_PERMITTED=1)
    target_link_libraries(NebulaTests PRIVATE
        juce::juce_audio_utils juce::juce_dsp juce::juce_recommended_config_flags)
    if(MSVC)
        target_compile_options(NebulaTests PRIVATE /utf-8 /bigobj)
    endif()
    if(TARGET NebulaForgeData)
        target_link_libraries(NebulaTests PRIVATE NebulaForgeData)
        target_compile_definitions(NebulaTests PRIVATE NEBULA_HAS_FORGE_DATA=1)
    endif()
endif()
