# The dedicated server executable: a thin process entry over rawframe.host with
# the server closure's registrars (ADR-0017, SPEC-0012).
rawframe_host(NAME dedicated_server OUTPUT rawframe-server
    SOURCES src/main.cpp
    DEPS host game_content network_quic physics2d physics3d world_replication world_kest world_runtime composition diagnostics result base
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
    # Serves the Kest game over QUIC with an identity made at start.
    add_test(NAME dedicated_server_quic
             COMMAND rawframe_host_dedicated_server --config ${CMAKE_CURRENT_SOURCE_DIR}/tests/quic.conf
             WORKING_DIRECTORY ${PROJECT_SOURCE_DIR})
    set_tests_properties(dedicated_server_quic PROPERTIES
                         PASS_REGULAR_EXPRESSION "\"code\":\"ready\".*\"code\":\"listening\".*\"code\":\"stopped\"")
    # A checkpoint restores: a server restored at tick 50 and run to 100
    # captures the same bytes as the server that ran there itself.
    add_test(NAME dedicated_server_checkpoint
             COMMAND ${CMAKE_CURRENT_SOURCE_DIR}/tests/checkpoint.sh $<TARGET_FILE:rawframe_host_dedicated_server>
             WORKING_DIRECTORY ${PROJECT_SOURCE_DIR})
    set_tests_properties(dedicated_server_checkpoint PROPERTIES PASS_REGULAR_EXPRESSION "checkpoints identical")
    # Runners keeps its hall's age from one run of the server to the next.
    add_test(NAME dedicated_server_save
             COMMAND ${CMAKE_CURRENT_SOURCE_DIR}/tests/save.sh $<TARGET_FILE:rawframe_host_dedicated_server>
             WORKING_DIRECTORY ${PROJECT_SOURCE_DIR})
    set_tests_properties(dedicated_server_save PROPERTIES PASS_REGULAR_EXPRESSION "hall kept across runs")
    # A bad argument is a usage error, not a crash.
    add_test(NAME dedicated_server_usage COMMAND rawframe_host_dedicated_server --bogus)
    set_tests_properties(dedicated_server_usage PROPERTIES WILL_FAIL TRUE)
endif()
