# The debug adapter for a running game (ADR-0066's debugger bridge, D461):
# the Debug Adapter Protocol on standard input and output, attached to a
# game's tooling endpoint. Tooling only; no client or server closure
# contains it.
rawframe_host(NAME debug OUTPUT rawframe-debug
    SOURCES src/main.cpp src/adapter.cpp
    DEPS authoring_session document result base
)

if(RAWFRAME_BUILD_TESTS)
    # An editor debugging the plaza (D461): attached to its server's
    # endpoint, a function breakpoint at stroll stops the game, the editor is
    # told, reads the frame and its arguments, carries on, and disconnects,
    # leaving the game running.
    add_test(NAME debug_attaches
             COMMAND ${RAWFRAME_SHELL} ${CMAKE_CURRENT_SOURCE_DIR}/tests/debug_attaches.sh $<TARGET_FILE:rawframe_host_debug>
                     $<TARGET_FILE:rawframe_host_dedicated_server> ${PROJECT_SOURCE_DIR}
                     ${PROJECT_BINARY_DIR}/hosts/client/plaza_server.conf ${CMAKE_CURRENT_BINARY_DIR}/debug_attaches)
    set_tests_properties(debug_attaches PROPERTIES FIXTURES_REQUIRED plaza_content
                         PASS_REGULAR_EXPRESSION "debugged the plaza through the debug adapter")
endif()
