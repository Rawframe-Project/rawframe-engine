# Headless bots against a server in another process over QUIC (Tool role).
rawframe_host(NAME bots OUTPUT rawframe-bots
    SOURCES src/main.cpp
    DEPS host game_content input_kest world_audio network_quic world_replication world_kest world_runtime composition diagnostics result base
)

if(RAWFRAME_BUILD_TESTS)
    # A dedicated server and a bots process on this machine over QUIC: the
    # server makes its identity, the bots pin it, and every bot is admitted.
    add_test(NAME bots_over_quic
             COMMAND ${CMAKE_CURRENT_SOURCE_DIR}/tests/play.sh
                     $<TARGET_FILE:rawframe_host_dedicated_server> $<TARGET_FILE:rawframe_host_bots> 8 1 480
             WORKING_DIRECTORY ${PROJECT_SOURCE_DIR})
    set_tests_properties(bots_over_quic PROPERTIES PASS_REGULAR_EXPRESSION "\"bots\":8,\"admitted\":8")
    # Bots agreeing with the server on runners' Composition are admitted,
    # and bots naming another Composition of the same Build are not.
    add_test(NAME bots_composition
             COMMAND ${CMAKE_CURRENT_SOURCE_DIR}/tests/composition.sh
                     $<TARGET_FILE:rawframe_host_dedicated_server> $<TARGET_FILE:rawframe_host_bots>
                     $<TARGET_FILE:rawframe_host_build_tool> ${PROJECT_BINARY_DIR}/hosts/arena/runners_content
                     ${CMAKE_CURRENT_BINARY_DIR}/bots_composition
             WORKING_DIRECTORY ${PROJECT_SOURCE_DIR})
    set_tests_properties(bots_composition PROPERTIES
                         FIXTURES_REQUIRED runners_content
                         PASS_REGULAR_EXPRESSION "\"bots\":2,\"admitted\":2[^\n]*\n[^\n]*\"bots\":2,\"admitted\":0")
endif()
