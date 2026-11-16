# midipixi gear firmware (configured from the root with -DMIDIPIXI_GEAR=ON)

# Factory presets are embedded and written to slots 0..N-1 on first boot.
include(${PROJECT_SOURCE_DIR}/cmake/EmbedPresets.cmake)
mpx_embed_presets(${CMAKE_CURRENT_BINARY_DIR}/generated/factory_presets.h ${MPX_FACTORY_PRESETS})

add_executable(midipixi_gear
    src/main.cpp
    src/render.cpp
    src/storage.cpp
    src/ws2812_output.cpp
    src/usb_descriptors.c
)

target_include_directories(midipixi_gear PRIVATE
    ${CMAKE_CURRENT_LIST_DIR}/src
    ${CMAKE_CURRENT_BINARY_DIR}/generated
)

target_compile_definitions(midipixi_gear PRIVATE
    MPX_BOARD_NAME="${PICO_BOARD}"
    PICO_FLASH_ASSERT_ON_UNSAFE=0
)

target_compile_options(midipixi_gear PRIVATE -Wall -Wextra -Wno-unused-parameter)

target_link_libraries(midipixi_gear PRIVATE
    midipixi_core
    pico_stdlib
    pico_multicore
    pico_flash
    pico_unique_id
    pico_util
    hardware_pio
    hardware_dma
    hardware_flash
    tinyusb_device
    tinyusb_board
)

pico_set_program_name(midipixi_gear "midipixi gear")
pico_set_program_version(midipixi_gear "${PROJECT_VERSION}")
pico_enable_stdio_usb(midipixi_gear 0)
pico_enable_stdio_uart(midipixi_gear 0)
pico_add_extra_outputs(midipixi_gear)  # .bin .hex .dis .map (+ .uf2 when picotool is used)

# Host binaries built during the build (picotool, pioasm) may be blocked on locked
# down Windows machines, so the presets set PICO_NO_PICOTOOL and the UF2 is made
# by a Python script instead.
if(PICO_NO_PICOTOOL)
    find_package(Python3 REQUIRED COMPONENTS Interpreter)
    if(PICO_RP2040)
        set(MPX_UF2_FAMILY rp2040)
    else()
        set(MPX_UF2_FAMILY rp2350)
    endif()
    add_custom_command(TARGET midipixi_gear POST_BUILD
        COMMAND ${Python3_EXECUTABLE} ${PROJECT_SOURCE_DIR}/scripts/bin2uf2.py
                $<TARGET_FILE_DIR:midipixi_gear>/midipixi_gear.bin
                $<TARGET_FILE_DIR:midipixi_gear>/midipixi_gear.uf2
                --family ${MPX_UF2_FAMILY}
        VERBATIM)
endif()
