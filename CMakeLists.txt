cmake_minimum_required(VERSION 3.22)

# One root for every target. Desktop (core, host, tools, tests, VST) by default;
# configure with -DMIDIPIXI_GEAR=ON (see CMakePresets.json) to build the firmware.
option(MIDIPIXI_GEAR "Build the gear firmware (requires pico-sdk and arm-none-eabi-gcc)" OFF)

if(MIDIPIXI_GEAR)
    if(NOT PICO_SDK_PATH AND DEFINED ENV{PICO_SDK_PATH})
        set(PICO_SDK_PATH $ENV{PICO_SDK_PATH})
    endif()
    if(NOT PICO_SDK_PATH AND EXISTS ${CMAKE_CURRENT_LIST_DIR}/.tools/pico-sdk)
        set(PICO_SDK_PATH ${CMAKE_CURRENT_LIST_DIR}/.tools/pico-sdk)
    endif()
    if(NOT PICO_SDK_PATH)
        message(FATAL_ERROR "Set PICO_SDK_PATH (environment or -D) to a pico-sdk 2.x checkout")
    endif()
    set(PICO_SDK_PATH ${PICO_SDK_PATH} CACHE PATH "pico-sdk location")
    if(NOT PICO_BOARD)
        set(PICO_BOARD pico2)
    endif()
    include(${PICO_SDK_PATH}/external/pico_sdk_import.cmake)
endif()

project(midipixi VERSION 0.1.0 LANGUAGES C CXX ASM)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

if(MIDIPIXI_GEAR)
    # Fastest officially supported clock: RP2040 200 MHz instead of 125 (the SDK
    # raises the core voltage to match), RP2350 stays at 150 MHz.
    if(NOT DEFINED PICO_USE_FASTEST_SUPPORTED_CLOCK)
        set(PICO_USE_FASTEST_SUPPORTED_CLOCK ON)
    endif()
    pico_sdk_init()
    add_subdirectory(core)
    add_subdirectory(gear)
    return()
endif()

option(MIDIPIXI_BUILD_TESTS "Build core unit tests" ON)
option(MIDIPIXI_BUILD_TOOLS "Build mpx-cli" ON)
option(MIDIPIXI_BUILD_VST "Build the JUCE plugin (downloads JUCE)" ON)

add_subdirectory(core)

if(MIDIPIXI_BUILD_TESTS)
    enable_testing()
    add_subdirectory(core/tests)
endif()

if(MIDIPIXI_BUILD_TOOLS OR MIDIPIXI_BUILD_VST)
    add_subdirectory(host)
endif()

if(MIDIPIXI_BUILD_TOOLS)
    add_subdirectory(tools/mpx-cli)
endif()

if(MIDIPIXI_BUILD_VST)
    add_subdirectory(vst)
endif()

