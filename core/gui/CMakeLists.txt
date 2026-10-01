add_executable(anyps5_gui WIN32
    main.cpp
    InputFormat.cpp
)

set_target_properties(anyps5_gui PROPERTIES
    OUTPUT_NAME "AnyPS5"
)

target_include_directories(anyps5_gui PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
target_compile_definitions(anyps5_gui PRIVATE UNICODE _UNICODE)
target_link_libraries(anyps5_gui PRIVATE comdlg32 shell32)

if(BUILD_TESTING)
    add_test_executable(gui_input_format_tests
        tests/InputFormatTests.cpp
        InputFormat.cpp
    )
    target_include_directories(gui_input_format_tests PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
    add_test(NAME gui_input_format COMMAND gui_input_format_tests)
endif()
