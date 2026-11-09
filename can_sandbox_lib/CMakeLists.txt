define_lib(can_sandbox_lib)

add_dependencies(can_sandbox_lib can_streams can_reader)

target_link_libraries(can_sandbox_lib can_streams can_reader j1939_parser ${CMAKE_THREAD_LIBS_INIT})
