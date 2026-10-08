# The launcher (D395): a standalone game's dedicated server started as a
# process of its own, then its client pinned to it, after following its
# release channel where it does (D434). No runtime closure contains it.
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
    # Runners exported to follow its studio's mirror, 0.2.0 then released on
    # stable: the launcher installs it before the game plays it, finds it
    # current the next time, and with the mirror gone plays it still.
    if(TARGET rawframe_host_client AND RAWFRAME_XVFB)
        add_test(NAME play_follows
                 COMMAND ${RAWFRAME_SHELL} ${PROJECT_SOURCE_DIR}/tools/xvfb_run.sh -s "-screen 0 1280x720x24 -br" ${RAWFRAME_SHELL}
                         ${CMAKE_CURRENT_SOURCE_DIR}/tests/followed.sh $<TARGET_FILE:rawframe_host_export_tool>
                         $<TARGET_FILE:rawframe_host_cook_tool> $<TARGET_FILE:rawframe_host_build_tool>
                         $<TARGET_FILE:rawframe_host_dedicated_server> $<TARGET_FILE:rawframe_host_client>
                         $<TARGET_FILE:rawframe_host_play_tool> $<TARGET_FILE:rawframe_host_install_tool>
                         ${PROJECT_BINARY_DIR}/hosts/arena/runners_content ${CMAKE_CURRENT_BINARY_DIR}/followed
                 WORKING_DIRECTORY ${PROJECT_SOURCE_DIR})
        set_tests_properties(play_follows PROPERTIES
                             LABELS window
                             FIXTURES_REQUIRED runners_content
                             SKIP_REGULAR_EXPRESSION "skip: no device"
                             PASS_REGULAR_EXPRESSION
                             "followed: first: both played 0.2.0: yes; followed 1, current 0; said: *\n.*followed: again: both played 0.2.0: yes; followed 0, current 1; said: *\n.*followed: unreachable: both played 0.2.0: yes; followed 0, current 0; said: rawframe-play: following the channel ended with [1-9][0-9]*; playing what the library has")
    endif()
endif()
