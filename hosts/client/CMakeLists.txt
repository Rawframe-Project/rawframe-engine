# A desktop client (D249): a window whose raw input plays the process's own
# player on a server over QUIC. Only where there are windows.
if(NOT TARGET rawframe_input_window)
    return()
endif()

rawframe_host(NAME client OUTPUT rawframe-client
    SOURCES src/main.cpp
    DEPS window_host host view game_content input_kest network_quic world_tooling window world_animation world_audio render_canvas world_ui render_scene world_kest world_localization
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

# A window needs a display: an X server of its own, where Xvfb can start
# one (tools/xvfb_run.sh, D428).
find_program(RAWFRAME_XVFB Xvfb)
if(RAWFRAME_BUILD_TESTS AND RAWFRAME_XVFB)
    # A dedicated server playing runners, and a client whose player joins
    # it from a real window: admitted, and, standing still, its predictions
    # confirmed all along until its iterations end.
    add_test(NAME client_plays_runners
             COMMAND ${RAWFRAME_SHELL} ${PROJECT_SOURCE_DIR}/tools/xvfb_run.sh ${RAWFRAME_SHELL} ${PROJECT_SOURCE_DIR}/hosts/bots/tests/play.sh
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
                 COMMAND ${RAWFRAME_SHELL} ${PROJECT_SOURCE_DIR}/tools/xvfb_run.sh -s "-screen 0 1280x720x24 -br" ${RAWFRAME_SHELL}
                         ${CMAKE_CURRENT_SOURCE_DIR}/tests/drawn.sh $<TARGET_FILE:rawframe_host_dedicated_server>
                         $<TARGET_FILE:rawframe_host_client> ${CMAKE_CURRENT_BINARY_DIR}/drawn.conf
                 WORKING_DIRECTORY ${PROJECT_SOURCE_DIR})
        set_tests_properties(client_drawn PROPERTIES
                             FIXTURES_REQUIRED runners_content
                             SKIP_REGULAR_EXPRESSION "skip: no device"
                             PASS_REGULAR_EXPRESSION
                             "the screen showed [1-9][0-9][0-9][0-9][0-9]+ lit pixels.*\"viewWidth\":800,\"viewHeight\":600,.*\"code\":\"frame_summary\"[^\n]*\"width\":800,\"height\":600,[^\n]*\"framesShown\":[1-9][0-9]+,.*\"reconfigured\":[2-9]")
        # Stalls clicked on its HUD's purse, a panel that takes presses
        # (D421): lighter under the mouse once it is there (D422), the UI
        # takes the click, and the owner builds no stall under it, the World
        # holding the owner's body alone. Taking nothing, the same click
        # builds one there. The press is the player's one collect command,
        # sent and taken by the server (D425).
        set(TYCOON_CONTENT ${PROJECT_BINARY_DIR}/hosts/arena/tycoon_content)
        configure_file(tests/stalls_clicked.conf.in ${CMAKE_CURRENT_BINARY_DIR}/stalls_clicked.conf @ONLY)
        add_test(NAME client_clicks_stalls
                 COMMAND ${RAWFRAME_SHELL} ${PROJECT_SOURCE_DIR}/tools/xvfb_run.sh -s "-screen 0 1280x720x24 -br" ${RAWFRAME_SHELL}
                         ${CMAKE_CURRENT_SOURCE_DIR}/tests/clicked.sh $<TARGET_FILE:rawframe_host_dedicated_server>
                         $<TARGET_FILE:rawframe_host_client> ${CMAKE_CURRENT_BINARY_DIR}/stalls_clicked.conf
                         games/tycoon/tycoon.game 16,16
                 WORKING_DIRECTORY ${PROJECT_SOURCE_DIR})
        set_tests_properties(client_clicks_stalls PROPERTIES
                             ENVIRONMENT CLICK_CURSOR=1
                             FIXTURES_REQUIRED tycoon_content
                             SKIP_REGULAR_EXPRESSION "skip: no device"
                             PASS_REGULAR_EXPRESSION
                             "at 16,16 [^\n]*: lighter.*\"commandsTaken\":1,\"commandsLimited\":0.*\"bodiesMade\":1,.*\"code\":\"ui_summary\"[^\n]*\"fontsRead\":1,[^\n]*\"presses\":1,.*\"commandsSent\":1,")
        # Stalls' sign field written in (D426): a click gives it the
        # keyboard, the keys type into it and no action, Return gives its
        # text to the sample, which sends it to the server as the owner's
        # sign, a command the server takes; the sign comes back to the client
        # and its board shows it (D427).
        add_test(NAME client_types_stalls
                 COMMAND ${RAWFRAME_SHELL} ${PROJECT_SOURCE_DIR}/tools/xvfb_run.sh -s "-screen 0 1280x720x24 -br" ${RAWFRAME_SHELL}
                         ${CMAKE_CURRENT_SOURCE_DIR}/tests/clicked.sh $<TARGET_FILE:rawframe_host_dedicated_server>
                         $<TARGET_FILE:rawframe_host_client> ${CMAKE_CURRENT_BINARY_DIR}/stalls_clicked.conf
                         games/tycoon/tycoon.game 60,95:ann
                 WORKING_DIRECTORY ${PROJECT_SOURCE_DIR})
        set_tests_properties(client_types_stalls PROPERTIES
                             FIXTURES_REQUIRED tycoon_content
                             SKIP_REGULAR_EXPRESSION "skip: no device"
                             PASS_REGULAR_EXPRESSION
                             "typed ann at 60,95.*\"commandsTaken\":1,\"commandsLimited\":0.*\"bodiesMade\":1,.*\"code\":\"ui_summary\"[^\n]*\"presses\":1,\"focused\":1,\"typed\":[4-9][0-9]*,\"submitted\":1,\"typedShown\":[1-9][0-9]*,.*\"commandsSent\":1,")
        # Stalls' HUD without the mouse (D430): Tab, the game's menu action,
        # has the UI navigated, focus on the purse; Return presses it, and
        # the server collects; Tab, now the UI's, moves on to the sign
        # field, Return gives it the keyboard, and the sign typed and given
        # by Return goes to the server and back to the board. Each step that
        # moves focus is its own group of keys, so a loaded machine's input
        # never reads the next with it, before the step took effect.
        add_test(NAME client_navigates_stalls
                 COMMAND ${RAWFRAME_SHELL} ${PROJECT_SOURCE_DIR}/tools/xvfb_run.sh -s "-screen 0 1280x720x24 -br" ${RAWFRAME_SHELL}
                         ${CMAKE_CURRENT_SOURCE_DIR}/tests/clicked.sh $<TARGET_FILE:rawframe_host_dedicated_server>
                         $<TARGET_FILE:rawframe_host_client> ${CMAKE_CURRENT_BINARY_DIR}/stalls_clicked.conf
                         games/tycoon/tycoon.game keys=Tab keys=Return keys=Tab keys=Return keys=b,o,Return
                 WORKING_DIRECTORY ${PROJECT_SOURCE_DIR})
        set_tests_properties(client_navigates_stalls PROPERTIES
                             FIXTURES_REQUIRED tycoon_content
                             SKIP_REGULAR_EXPRESSION "skip: no device"
                             PASS_REGULAR_EXPRESSION
                             "pressed b,o,Return.*\"commandsTaken\":2,\"commandsLimited\":0.*\"code\":\"ui_summary\"[^\n]*\"presses\":0,\"focused\":1,\"typed\":[3-9][0-9]*,\"submitted\":1,\"typedShown\":[1-9][0-9]*,\"navigated\":[2-9][0-9]*,\"activated\":1,")
        # The plaza, its walker's scene drawn in 3D and shown in the window
        # (D286): the scene's models drawn in frames the window showed,
        # antialiased over time, each reusing the picture before (D291), its
        # exposure metered (D293), the screen lit by them, the picture read
        # back in many shades, and the window's HDR capability record told,
        # SDR offered and used (D365).
        set(PLAZA_CONTENT ${PROJECT_BINARY_DIR}/hosts/arena/plaza_content)
        set(CAPTURE ${CMAKE_CURRENT_BINARY_DIR}/client_plaza.tga)
        configure_file(tests/plaza_drawn.conf.in ${CMAKE_CURRENT_BINARY_DIR}/plaza_drawn.conf @ONLY)
        configure_file(tests/plaza_server.conf.in ${CMAKE_CURRENT_BINARY_DIR}/plaza_server.conf @ONLY)
        add_test(NAME client_plaza_drawn
                 COMMAND ${RAWFRAME_SHELL} ${PROJECT_SOURCE_DIR}/tools/xvfb_run.sh -s "-screen 0 1280x720x24 -br" ${RAWFRAME_SHELL}
                         ${CMAKE_CURRENT_SOURCE_DIR}/tests/drawn.sh $<TARGET_FILE:rawframe_host_dedicated_server>
                         $<TARGET_FILE:rawframe_host_client> ${CMAKE_CURRENT_BINARY_DIR}/plaza_drawn.conf
                         games/plaza/plaza.game ${CMAKE_CURRENT_BINARY_DIR}/plaza_server.conf
                 WORKING_DIRECTORY ${PROJECT_SOURCE_DIR})
        set_tests_properties(client_plaza_drawn PROPERTIES
                             FIXTURES_REQUIRED plaza_content
                             SKIP_REGULAR_EXPRESSION "skip: no device"
                             PASS_REGULAR_EXPRESSION
                             "the screen showed [1-9][0-9][0-9][0-9][0-9]+ lit pixels.*\"code\":\"output_record\"[^\n]*\"revision\":1,\"offered\":\"sdr_srgb[^\"]*\",\"active\":\"sdr_srgb\".*\"code\":\"scene_drawing_summary\"[^\n]*\"frames\":[1-9][0-9]+,[^\n]*\"models\":[1-9][0-9]+,[^\n]*\"modelsLeftOut\":0,[^\n]*\"historyReused\":[1-9][0-9]+,\"framesMetered\":[1-9][0-9]+.*\"code\":\"frame_summary\"[^\n]*\"framesShown\":[1-9][0-9]+,[^\n]*\"colors\":[1-9][0-9][0-9]+,\"captured\":true")
        # The plaza previewed for an authoring session (D432, D433): the
        # client's own tooling endpoint, with the view grant, attached as a
        # scene's preview, shown the scene's views, and let go, which gives
        # the player's camera back.
        add_test(NAME client_previews_plaza
                 COMMAND ${RAWFRAME_SHELL} ${PROJECT_SOURCE_DIR}/tools/xvfb_run.sh -s "-screen 0 1280x720x24 -br" ${RAWFRAME_SHELL}
                         ${CMAKE_CURRENT_SOURCE_DIR}/tests/previewed.sh $<TARGET_FILE:rawframe_host_dedicated_server>
                         $<TARGET_FILE:rawframe_host_client> $<TARGET_FILE:rawframe_host_author>
                         ${CMAKE_CURRENT_BINARY_DIR}/plaza_drawn.conf games/plaza/plaza.game gate.scene
                         ${CMAKE_CURRENT_BINARY_DIR}/plaza_server.conf
                 WORKING_DIRECTORY ${PROJECT_SOURCE_DIR})
        set_tests_properties(client_previews_plaza PROPERTIES
                             FIXTURES_REQUIRED plaza_content
                             SKIP_REGULAR_EXPRESSION "skip: no device"
                             PASS_REGULAR_EXPRESSION
                             "\"id\":2,\"answer\":{\"kind\":\"authoring.preview\",\"reopened\":false,\"previewing\":true}.*\"id\":3,\"answer\":{\"kind\":\"authoring.view\",[^\n]*\"previewing\":true}.*\"id\":4,\"answer\":{\"kind\":\"authoring.view\",[^\n]*\"previewing\":true}.*\"id\":5,\"answer\":{\"kind\":\"authoring.preview\",\"reopened\":false,\"previewing\":false}.*\"code\":\"scene_summary\"[^\n]*\"framesPreviewed\":[1-9][0-9]*}")
    endif()
endif()
