add_executable(anyps5_gui WIN32
    main.cpp
)

set_target_properties(anyps5_gui PROPERTIES
    OUTPUT_NAME "AnyPS5"
)

target_compile_definitions(anyps5_gui PRIVATE UNICODE _UNICODE)
target_link_libraries(anyps5_gui PRIVATE comdlg32 shell32)
