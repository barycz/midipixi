add_executable(midipixi_core_tests
    main.cpp
    test_version.cpp
    test_json.cpp
    test_preset.cpp
    test_engine.cpp
)
target_link_libraries(midipixi_core_tests PRIVATE midipixi_core)
target_compile_definitions(midipixi_core_tests PRIVATE MPX_SOURCE_DIR="${PROJECT_SOURCE_DIR}")
if(MSVC)
    target_compile_options(midipixi_core_tests PRIVATE /utf-8)
endif()
add_test(NAME midipixi_core_tests COMMAND midipixi_core_tests)
