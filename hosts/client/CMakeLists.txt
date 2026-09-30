# A desktop client (D249): a window whose raw input plays the process's own
# player on a server over QUIC. Only where there are windows.
if(NOT TARGET rawframe_input_window)
    return()
endif()

rawframe_host(NAME client OUTPUT rawframe-client
    SOURCES src/main.cpp
    DEPS window_host host game_content input_kest network_quic window world_animation world_audio render_canvas render_scene world_kest world_localization
         world_replication world_runtime composition diagnostics result base
)
# Where Maul RHI is built, the client draws its scene and canvas on the one
# device and shows them in its window (D280, D286).
set(RAWFRAME_CLIENT_DRAWS OFF)
if(TARGET rawframe_render_canvas_gpu)
    set(RAWFRAME_CLIENT_DRAWS ON)
    target_link_libraries(rawframe_host_client PRIVATE rawframe::render rawframe::render_canvas_gpu rawframe::render_scene_gpu)
endif()
target_compile_definitions(rawframe_host_client PRIVATE RAWFRAME_CLIENT_DRAWS=$<BOOL:${RAWFRAME_CLIENT_DRAWS}>)

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
    # The same, drawn on the one device and shown in the window (D280): its
    # frames shown, and the screen lit by what they drew. The window is
    # resized while it plays, and the surface, the picture, and the view
    # follow it (D281).
    if(RAWFRAME_CLIENT_DRAWS)
        set(CONTENT ${PROJECT_BINARY_DIR}/hosts/arena/runners_content)
        set(CAPTURE ${CMAKE_CURRENT_BINARY_DIR}/client_drawn.tga)
        configure_file(tests/drawn.conf.in ${CMAKE_CURRENT_BINARY_DIR}/drawn.conf @ONLY)
        add_test(NAME client_drawn
                 COMMAND ${RAWFRAME_XVFB_RUN} -a -s "-screen 0 1280x720x24 -br" ${RAWFRAME_SHELL}
                         ${CMAKE_CURRENT_SOURCE_DIR}/tests/drawn.sh $<TARGET_FILE:rawframe_host_dedicated_server>
                         $<TARGET_FILE:rawframe_host_client> ${CMAKE_CURRENT_BINARY_DIR}/drawn.conf
                 WORKING_DIRECTORY ${PROJECT_SOURCE_DIR})
        set_tests_properties(client_drawn PROPERTIES
                             FIXTURES_REQUIRED runners_content
                             SKIP_REGULAR_EXPRESSION "skip: no device"
                             PASS_REGULAR_EXPRESSION
                             "the screen showed [1-9][0-9][0-9][0-9][0-9]+ lit pixels.*\"viewWidth\":800,\"viewHeight\":600,.*\"code\":\"frame_summary\"[^\n]*\"width\":800,\"height\":600,[^\n]*\"framesShown\":[1-9][0-9]+,.*\"reconfigured\":[2-9]")
        if(RAWFRAME_SANITIZE STREQUAL "thread")
            set_tests_properties(client_drawn PROPERTIES ENVIRONMENT
                                 "TSAN_OPTIONS=suppressions=${PROJECT_SOURCE_DIR}/tools/lavapipe.tsan")
        endif()
        # The plaza, its walker's scene drawn in 3D and shown in the window
        # (D286): the scene's models drawn in frames the window showed,
        # antialiased over time, each reusing the picture before (D291), its
        # exposure metered (D293), the screen lit by them, and the picture
        # read back in many shades.
        set(PLAZA_CONTENT ${PROJECT_BINARY_DIR}/hosts/arena/plaza_content)
        set(CAPTURE ${CMAKE_CURRENT_BINARY_DIR}/client_plaza.tga)
        configure_file(tests/plaza_drawn.conf.in ${CMAKE_CURRENT_BINARY_DIR}/plaza_drawn.conf @ONLY)
        configure_file(tests/plaza_server.conf.in ${CMAKE_CURRENT_BINARY_DIR}/plaza_server.conf @ONLY)
        add_test(NAME client_plaza_drawn
                 COMMAND ${RAWFRAME_XVFB_RUN} -a -s "-screen 0 1280x720x24 -br" ${RAWFRAME_SHELL}
                         ${CMAKE_CURRENT_SOURCE_DIR}/tests/drawn.sh $<TARGET_FILE:rawframe_host_dedicated_server>
                         $<TARGET_FILE:rawframe_host_client> ${CMAKE_CURRENT_BINARY_DIR}/plaza_drawn.conf
                         games/plaza/plaza.game ${CMAKE_CURRENT_BINARY_DIR}/plaza_server.conf
                 WORKING_DIRECTORY ${PROJECT_SOURCE_DIR})
        set_tests_properties(client_plaza_drawn PROPERTIES
                             FIXTURES_REQUIRED plaza_content
                             SKIP_REGULAR_EXPRESSION "skip: no device"
                             PASS_REGULAR_EXPRESSION
                             "the screen showed [1-9][0-9][0-9][0-9][0-9]+ lit pixels.*\"code\":\"scene_drawing_summary\"[^\n]*\"frames\":[1-9][0-9]+,[^\n]*\"models\":[1-9][0-9]+,[^\n]*\"modelsLeftOut\":0,[^\n]*\"historyReused\":[1-9][0-9]+,\"framesMetered\":[1-9][0-9]+.*\"code\":\"frame_summary\"[^\n]*\"framesShown\":[1-9][0-9]+,[^\n]*\"colors\":[1-9][0-9][0-9]+,\"captured\":true")
        if(RAWFRAME_SANITIZE STREQUAL "thread")
            set_tests_properties(client_plaza_drawn PROPERTIES ENVIRONMENT
                                 "TSAN_OPTIONS=suppressions=${PROJECT_SOURCE_DIR}/tools/lavapipe.tsan")
        endif()
    endif()
endif()
