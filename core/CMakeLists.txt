add_library(midipixi_core STATIC
    src/version.cpp
    src/json.cpp
    src/preset.cpp
    src/preset_json.cpp
    src/output.cpp
    src/engine.cpp
    src/protocol.cpp
)

target_include_directories(midipixi_core PUBLIC include)
target_compile_features(midipixi_core PUBLIC cxx_std_17)

if(MSVC)
    target_compile_options(midipixi_core PRIVATE /W4 /permissive- /utf-8)
    target_compile_definitions(midipixi_core PUBLIC _USE_MATH_DEFINES _CRT_SECURE_NO_WARNINGS)
else()
    target_compile_options(midipixi_core PRIVATE -Wall -Wextra -Wno-unused-parameter)
endif()

if(NOT MIDIPIXI_GEAR)
    set_target_properties(midipixi_core PROPERTIES POSITION_INDEPENDENT_CODE ON)
endif()
