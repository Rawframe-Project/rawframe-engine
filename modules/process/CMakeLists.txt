# This process and the children it starts (ADR-0084, D395): its own
# executable, and a program run as it is named, its exit code asked without
# waiting, a stop request, a kill. Tooling and launchers; never part of a
# dedicated server, and never on the web, which starts no processes.
rawframe_module(NAME process
    SOURCES src/child.cpp src/self.cpp
    DEPS result base
)

rawframe_module_tests(NAME process SOURCES tests/child_test.cpp)
if(TARGET rawframe_process_tests)
    # The program the tests start, under the engine's language policy (on
    # Windows, clang-cl's default is older than C++17).
    add_executable(rawframe_process_child tests/child.cpp)
    target_link_libraries(rawframe_process_child PRIVATE rawframe_policy)
    rawframe_test_program(rawframe_child rawframe_process_child)
    target_compile_definitions(rawframe_process_tests PRIVATE
        RAWFRAME_PROCESS_CHILD="${rawframe_child}")
    add_dependencies(rawframe_process_tests rawframe_process_child)
endif()
