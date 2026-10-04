# The launcher (D395): a standalone game's dedicated server started as a
# process of its own, then its client pinned to it. No runtime closure
# contains it.
rawframe_host(NAME play_tool OUTPUT rawframe-play
    SOURCES src/main.cpp
    DEPS process composition result base
)

if(RAWFRAME_BUILD_TESTS)
    add_test(NAME play_usage COMMAND rawframe_host_play_tool --config)
    set_tests_properties(play_usage PROPERTIES WILL_FAIL TRUE)
    # The arena game played through the launcher: the server started and
    # listening, the client (a bots process) admitted and ending on its own,
    # and the server then asked to stop, stopping cleanly.
    add_test(NAME play_launched
             COMMAND ${RAWFRAME_SHELL} ${CMAKE_CURRENT_SOURCE_DIR}/tests/launched.sh
                     $<TARGET_FILE:rawframe_host_play_tool> $<TARGET_FILE:rawframe_host_dedicated_server>
                     $<TARGET_FILE:rawframe_host_bots> games/arena/arena.game
             WORKING_DIRECTORY ${PROJECT_SOURCE_DIR})
    set_tests_properties(play_launched PROPERTIES
                         PASS_REGULAR_EXPRESSION "\"bots\":1,\"admitted\":1,.*\"exit\":\"clean_stop\".*play: exit 0")
endif()
