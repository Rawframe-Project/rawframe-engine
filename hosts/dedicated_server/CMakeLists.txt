# The dedicated server executable: a thin process entry over rawframe.host with
# the server closure's registrars (ADR-0017, SPEC-0012).
rawframe_host(NAME dedicated_server OUTPUT rawframe-server
    SOURCES src/main.cpp
    DEPS host world_replication world_kest world_runtime composition diagnostics result base
)

if(RAWFRAME_BUILD_TESTS)
    # Starts, runs its iterations, and stops in order.
    add_test(NAME dedicated_server_smoke
             COMMAND rawframe_host_dedicated_server --config ${CMAKE_CURRENT_SOURCE_DIR}/tests/smoke.conf)
    set_tests_properties(dedicated_server_smoke PROPERTIES PASS_REGULAR_EXPRESSION "\"code\":\"stopped\"")
    # Loads a Kest-scripted game, ticks it, and stops. Paths in the
    # configuration are relative to the repository root.
    add_test(NAME dedicated_server_kest_game
             COMMAND rawframe_host_dedicated_server --config ${CMAKE_CURRENT_SOURCE_DIR}/tests/kest_game.conf
             WORKING_DIRECTORY ${PROJECT_SOURCE_DIR})
    set_tests_properties(dedicated_server_kest_game PROPERTIES
                         PASS_REGULAR_EXPRESSION "\"code\":\"game_loaded\".*\"code\":\"stopped\"")
    # A bad argument is a usage error, not a crash.
    add_test(NAME dedicated_server_usage COMMAND rawframe_host_dedicated_server --bogus)
    set_tests_properties(dedicated_server_usage PROPERTIES WILL_FAIL TRUE)
endif()
