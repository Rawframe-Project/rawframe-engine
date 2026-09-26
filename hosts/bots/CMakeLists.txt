# Headless bots against a server in another process over QUIC (Tool role).
rawframe_host(NAME bots OUTPUT rawframe-bots
    SOURCES src/main.cpp
    DEPS host game_content input_kest world_audio world_localization network_quic world_replication world_kest world_runtime composition diagnostics result base
)

if(RAWFRAME_BUILD_TESTS)
    # A dedicated server and a bots process on this machine over QUIC: the
    # server makes its identity, the bots pin it, and every bot is admitted.
    add_test(NAME bots_over_quic
             COMMAND ${RAWFRAME_SHELL} ${CMAKE_CURRENT_SOURCE_DIR}/tests/play.sh
                     $<TARGET_FILE:rawframe_host_dedicated_server> $<TARGET_FILE:rawframe_host_bots> 8 1 480
             WORKING_DIRECTORY ${PROJECT_SOURCE_DIR})
    set_tests_properties(bots_over_quic PROPERTIES PASS_REGULAR_EXPRESSION "\"bots\":8,\"admitted\":8")
    # Two bots processes, one bot each, asking for the same session:
    # whichever is second is told that player is already playing.
    add_test(NAME bots_same_session
             COMMAND ${RAWFRAME_SHELL} ${CMAKE_CURRENT_SOURCE_DIR}/tests/play.sh
                     $<TARGET_FILE:rawframe_host_dedicated_server> $<TARGET_FILE:rawframe_host_bots> 1 2 480
                     games/arena/arena.game /dev/null ${CMAKE_CURRENT_SOURCE_DIR}/tests/same_session.conf
             WORKING_DIRECTORY ${PROJECT_SOURCE_DIR})
    set_tests_properties(bots_same_session PROPERTIES
                         PASS_REGULAR_EXPRESSION "\"bots\":1,\"admitted\":1,.*\"bots\":1,\"admitted\":0,\"unavailable\":1|\"bots\":1,\"admitted\":0,\"unavailable\":1,.*\"bots\":1,\"admitted\":1,")
    # A server asked to stop drains: it refuses new bots as unavailable and
    # stops once the bots it serves have left. Asked with SIGTERM, which a
    # Windows process does not receive; its console control event waits for
    # a test of its own (D237).
    if(NOT WIN32)
        add_test(NAME bots_drain
                 COMMAND ${RAWFRAME_SHELL} ${CMAKE_CURRENT_SOURCE_DIR}/tests/drain.sh
                         $<TARGET_FILE:rawframe_host_dedicated_server> $<TARGET_FILE:rawframe_host_bots>
                 WORKING_DIRECTORY ${PROJECT_SOURCE_DIR})
        set_tests_properties(bots_drain PROPERTIES PASS_REGULAR_EXPRESSION "drained in order" TIMEOUT 120)
    endif()
    # Bots agreeing with the server on runners' Composition are admitted,
    # and bots naming another Composition of the same Build are not.
    add_test(NAME bots_composition
             COMMAND ${RAWFRAME_SHELL} ${CMAKE_CURRENT_SOURCE_DIR}/tests/composition.sh
                     $<TARGET_FILE:rawframe_host_dedicated_server> $<TARGET_FILE:rawframe_host_bots>
                     $<TARGET_FILE:rawframe_host_build_tool> ${PROJECT_BINARY_DIR}/hosts/arena/runners_content
                     ${CMAKE_CURRENT_BINARY_DIR}/bots_composition
             WORKING_DIRECTORY ${PROJECT_SOURCE_DIR})
    set_tests_properties(bots_composition PROPERTIES
                         FIXTURES_REQUIRED runners_content
                         PASS_REGULAR_EXPRESSION "\"bots\":2,\"admitted\":2[^\n]*\n[^\n]*\"bots\":2,\"admitted\":0")
endif()
