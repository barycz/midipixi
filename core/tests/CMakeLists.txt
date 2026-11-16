add_executable(midipixi_core_tests
    main.cpp
    test_version.cpp
)
target_link_libraries(midipixi_core_tests PRIVATE midipixi_core)
target_compile_definitions(midipixi_core_tests PRIVATE MPX_SOURCE_DIR="${PROJECT_SOURCE_DIR}")
add_test(NAME midipixi_core_tests COMMAND midipixi_core_tests)
