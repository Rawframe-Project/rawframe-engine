# A desktop client (D249): a window whose raw input plays the process's own
# player on a server over QUIC. Only where there are windows.
if(NOT TARGET rawframe_input_window)
    return()
endif()

rawframe_host(NAME client OUTPUT rawframe-client
    SOURCES src/main.cpp
    DEPS window_host host game_content input_kest network_quic window world_animation world_audio render_canvas world_kest world_localization
         world_replication world_runtime composition diagnostics result base
)

# A window needs a display: an X server of its own, where xvfb-run can
# start one.
find_program(RAWFRAME_XVFB_RUN xvfb-run)
if(RAWFRAME_BUILD_TESTS AND RAWFRAME_XVFB_RUN)
    # A dedicated server playing runners, and a client whose player joins
    # it from a real window: admitted, and, standing still, its predictions
    # confirmed all along until its iterations end.
    add_test(NAME client_plays_runners
             COMMAND ${RAWFRAME_XVFB_RUN} -a ${RAWFRAME_SHELL} ${PROJECT_SOURCE_DIR}/hosts/bots/tests/play.sh
                     $<TARGET_FILE:rawframe_host_dedicated_server> $<TARGET_FILE:rawframe_host_client> 0 1 480
                     games/runners/runners.game /dev/null ${CMAKE_CURRENT_SOURCE_DIR}/tests/player.conf
             WORKING_DIRECTORY ${PROJECT_SOURCE_DIR})
    set_tests_properties(client_plays_runners PROPERTIES PASS_REGULAR_EXPRESSION "\"bots\":1,\"admitted\":1,[^\n]*\"stalled\":0,")
endif()
