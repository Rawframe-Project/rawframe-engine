# Headless bots against a server in another process over QUIC (Tool role).
rawframe_host(NAME bots OUTPUT rawframe-bots
    SOURCES src/main.cpp
    DEPS host input_kest network_quic world_replication world_kest world_runtime composition diagnostics result base
)

if(RAWFRAME_BUILD_TESTS)
    # A dedicated server and a bots process on this machine over QUIC: the
    # server makes its identity, the bots pin it, and every bot is admitted.
    add_test(NAME bots_over_quic
             COMMAND ${CMAKE_CURRENT_SOURCE_DIR}/tests/play.sh
                     $<TARGET_FILE:rawframe_host_dedicated_server> $<TARGET_FILE:rawframe_host_bots> 8 1 480
             WORKING_DIRECTORY ${PROJECT_SOURCE_DIR})
    set_tests_properties(bots_over_quic PROPERTIES PASS_REGULAR_EXPRESSION "\"bots\":8,\"admitted\":8")
endif()
