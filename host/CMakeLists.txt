# Desktop helpers shared by mpx-cli and the VST: serial port, gear discovery, gear client.
add_library(midipixi_host STATIC
    src/serial_port.cpp
    src/gear_client.cpp
)
target_include_directories(midipixi_host PUBLIC include)
target_link_libraries(midipixi_host PUBLIC midipixi_core)
set_target_properties(midipixi_host PROPERTIES POSITION_INDEPENDENT_CODE ON)
if(WIN32)
    target_link_libraries(midipixi_host PRIVATE setupapi advapi32)
endif()
if(MSVC)
    target_compile_options(midipixi_host PRIVATE /W4 /utf-8)
    target_compile_definitions(midipixi_host PRIVATE _CRT_SECURE_NO_WARNINGS)
endif()
