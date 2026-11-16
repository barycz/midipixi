add_library(midipixi_core STATIC
    src/version.cpp
)

target_include_directories(midipixi_core PUBLIC include)
target_compile_features(midipixi_core PUBLIC cxx_std_17)

if(MSVC)
    target_compile_options(midipixi_core PRIVATE /W4 /permissive-)
    target_compile_definitions(midipixi_core PUBLIC _USE_MATH_DEFINES)
else()
    target_compile_options(midipixi_core PRIVATE -Wall -Wextra -Wno-unused-parameter)
endif()

if(NOT MIDIPIXI_GEAR)
    set_target_properties(midipixi_core PROPERTIES POSITION_INDEPENDENT_CODE ON)
endif()
