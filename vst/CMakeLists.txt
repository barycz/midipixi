# midipixi VST3 + standalone (JUCE 8, AGPLv3)

# --- JUCE ---------------------------------------------------------------------------
set(MIDIPIXI_JUCE_DIR "" CACHE PATH "Local JUCE 8 checkout (empty: .tools/JUCE or download)")
if(NOT MIDIPIXI_JUCE_DIR AND EXISTS ${PROJECT_SOURCE_DIR}/.tools/JUCE/modules)
    set(MIDIPIXI_JUCE_DIR ${PROJECT_SOURCE_DIR}/.tools/JUCE)
endif()
if(NOT MIDIPIXI_JUCE_DIR)
    include(FetchContent)
    # SOURCE_SUBDIR points nowhere: download only, JUCE is wired up below.
    FetchContent_Declare(juce
        GIT_REPOSITORY https://github.com/juce-framework/JUCE.git
        GIT_TAG 8.0.15
        GIT_SHALLOW TRUE
        SOURCE_SUBDIR do-not-add)
    FetchContent_MakeAvailable(juce)
    set(MIDIPIXI_JUCE_DIR ${juce_SOURCE_DIR})
endif()

# JUCE builds a helper tool (juceaide) at configure time and runs it during the
# build. Locked-down Windows machines (Smart App Control) refuse to run freshly
# built unsigned executables, so by default the modules are loaded directly and
# juceaide is replaced by a Python script (cmake/juceaide.py).
option(MIDIPIXI_PYTHON_JUCEAIDE "Use the Python juceaide replacement" ON)

if(MIDIPIXI_PYTHON_JUCEAIDE)
    find_package(Python3 REQUIRED COMPONENTS Interpreter)
    set(JUCE_MODULES_DIR "${MIDIPIXI_JUCE_DIR}/modules" CACHE INTERNAL "The path to JUCE modules")
    include(${MIDIPIXI_JUCE_DIR}/extras/Build/CMake/JUCEModuleSupport.cmake)
    include(${MIDIPIXI_JUCE_DIR}/extras/Build/CMake/JUCEUtils.cmake)
    foreach(module
            juce_core juce_events juce_data_structures juce_graphics juce_gui_basics juce_gui_extra
            juce_audio_basics juce_audio_devices juce_audio_formats juce_audio_processors
            juce_audio_processors_headless juce_audio_utils juce_audio_plugin_client juce_dsp
            juce_midi_ci juce_animation juce_javascript juce_opengl juce_cryptography juce_osc
            juce_analytics juce_box2d juce_product_unlocking juce_video)
        juce_add_module(${JUCE_MODULES_DIR}/${module} ALIAS_NAMESPACE juce)
    endforeach()
    if(WIN32)
        set(juceaide_wrapper ${CMAKE_CURRENT_BINARY_DIR}/juceaide.cmd)
        file(TO_NATIVE_PATH "${Python3_EXECUTABLE}" py_native)
        file(TO_NATIVE_PATH "${CMAKE_CURRENT_LIST_DIR}/cmake/juceaide.py" script_native)
        file(WRITE ${juceaide_wrapper} "@\"${py_native}\" \"${script_native}\" %*\r\n")
    else()
        set(juceaide_wrapper ${CMAKE_CURRENT_BINARY_DIR}/juceaide.sh)
        file(WRITE ${juceaide_wrapper} "#!/bin/sh\nexec \"${Python3_EXECUTABLE}\" \"${CMAKE_CURRENT_LIST_DIR}/cmake/juceaide.py\" \"$@\"\n")
        file(CHMOD ${juceaide_wrapper} PERMISSIONS OWNER_READ OWNER_WRITE OWNER_EXECUTE)
    endif()
    add_executable(juceaide IMPORTED GLOBAL)
    set_target_properties(juceaide PROPERTIES IMPORTED_LOCATION ${juceaide_wrapper})
    add_executable(juce::juceaide ALIAS juceaide)
else()
    add_subdirectory(${MIDIPIXI_JUCE_DIR} ${CMAKE_CURRENT_BINARY_DIR}/juce)
endif()

# --- Plugin -------------------------------------------------------------------------------
include(${PROJECT_SOURCE_DIR}/cmake/EmbedPresets.cmake)
mpx_embed_presets(${CMAKE_CURRENT_BINARY_DIR}/generated/factory_presets.h ${MPX_FACTORY_PRESETS})

juce_add_plugin(MidipixiVST
    PRODUCT_NAME "midipixi"
    COMPANY_NAME "midipixi"
    COMPANY_COPYRIGHT "AGPL-3.0-or-later"
    BUNDLE_ID org.midipixi.vst
    DESCRIPTION "MIDI driven LED strip visual effects"
    VERSION ${PROJECT_VERSION}
    PLUGIN_MANUFACTURER_CODE Mpxi
    PLUGIN_CODE Mpx1
    FORMATS VST3 Standalone
    IS_SYNTH TRUE
    NEEDS_MIDI_INPUT TRUE
    NEEDS_MIDI_OUTPUT FALSE
    IS_MIDI_EFFECT FALSE
    EDITOR_WANTS_KEYBOARD_FOCUS TRUE
    VST3_CATEGORIES Instrument Generator
    VST3_AUTO_MANIFEST FALSE
    COPY_PLUGIN_AFTER_BUILD FALSE)

target_sources(MidipixiVST PRIVATE
    src/PluginProcessor.cpp
    src/PluginEditor.cpp
    src/GearLink.cpp
    src/ui/Theme.cpp
    src/ui/LedStripView.cpp
    src/ui/ParamKnob.cpp
    src/ui/LayerList.cpp
    src/ui/LayerEditor.cpp
    src/ui/GearPanel.cpp
    src/ui/ModKnob.cpp
    src/ui/ModMatrixView.cpp
)

target_include_directories(MidipixiVST PRIVATE
    src
    ${CMAKE_CURRENT_BINARY_DIR}/generated
)

target_compile_definitions(MidipixiVST PUBLIC
    JUCE_WEB_BROWSER=0
    JUCE_USE_CURL=0
    JUCE_VST3_CAN_REPLACE_VST2=0
    JUCE_DISPLAY_SPLASH_SCREEN=0
    JUCE_REPORT_APP_USAGE=0
    JUCE_STRICT_REFCOUNTEDPOINTER=1
)

target_link_libraries(MidipixiVST
    PRIVATE
        midipixi_core
        midipixi_host
        juce::juce_audio_utils
        juce::juce_gui_extra
    PUBLIC
        juce::juce_recommended_config_flags
        juce::juce_recommended_lto_flags
        juce::juce_recommended_warning_flags)

if(MSVC)
    target_compile_options(MidipixiVST PRIVATE /utf-8)
endif()
