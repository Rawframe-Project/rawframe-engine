# Child processes a launcher starts (ADR-0084, D395): a program run as it is
# named, its exit code asked without waiting, a stop request, a kill.
# Launcher and packaging tooling; never part of a dedicated server, and
# never on the web, which starts no processes.
rawframe_module(NAME process
    SOURCES src/child.cpp
    DEPS result base
)

rawframe_module_tests(NAME process SOURCES tests/child_test.cpp)
if(TARGET rawframe_process_tests)
    # The program the tests start.
    add_executable(rawframe_process_child tests/child.cpp)
    target_compile_definitions(rawframe_process_tests PRIVATE
        RAWFRAME_PROCESS_CHILD="$<TARGET_FILE:rawframe_process_child>")
    add_dependencies(rawframe_process_tests rawframe_process_child)
endif()
