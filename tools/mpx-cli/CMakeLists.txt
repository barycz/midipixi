add_executable(mpx-cli
    main.cpp
    preset_io.cpp
    gear_commands.cpp
    smf.cpp
    png.cpp
)
target_link_libraries(mpx-cli PRIVATE midipixi_core)
if(MSVC)
    target_compile_options(mpx-cli PRIVATE /W4 /utf-8)
    target_compile_definitions(mpx-cli PRIVATE _CRT_SECURE_NO_WARNINGS)
endif()
