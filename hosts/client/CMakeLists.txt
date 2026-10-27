# The desktop client: a window whose raw input plays the process's own
# player on a server over QUIC. Only where there are windows.
if(NOT TARGET rawframe_input_window)
    return()
endif()

rawframe_host(NAME client OUTPUT rawframe-client
    SOURCES src/main.cpp
    DEPS window_host host view game_content input_kest network_quic world_tooling window world_animation world_audio render_canvas world_ui render_scene world_kest world_localization
         world_replication world_runtime composition diagnostics result base
)
# On Windows the client program shows Rawframe's mark in Explorer.
if(WIN32)
    target_sources(rawframe_host_client PRIVATE resources/rawframe-client.rc)
endif()
# Where Maul RHI is built, the client draws its scene and canvas on the one
# device and shows them in its window.
set(RAWFRAME_CLIENT_DRAWS OFF)
if(TARGET rawframe_render_canvas_gpu)
    set(RAWFRAME_CLIENT_DRAWS ON)
    target_link_libraries(rawframe_host_client PRIVATE rawframe::render rawframe::render_canvas_gpu rawframe::render_scene_gpu)
endif()
# Where OpenXR is built, a headset shows it too.
set(RAWFRAME_CLIENT_HEADSET OFF)
if(TARGET rawframe_xr)
    set(RAWFRAME_CLIENT_HEADSET ON)
    target_link_libraries(rawframe_host_client PRIVATE rawframe::xr)
endif()
target_compile_definitions(rawframe_host_client PRIVATE RAWFRAME_CLIENT_DRAWS=$<BOOL:${RAWFRAME_CLIENT_DRAWS}>
                                                        RAWFRAME_CLIENT_HEADSET=$<BOOL:${RAWFRAME_CLIENT_HEADSET}>)

# A window needs a display: an X server of its own, where Xvfb can start
# one (tools/xvfb_run.sh). Each test plays the real client beside a real
# server, so they are the integration tests, kept to one scenario for each
# path a player takes through the client: what a module's own tests cannot
# reach.
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
    if(RAWFRAME_CLIENT_DRAWS)
        set(CONTENT ${PROJECT_BINARY_DIR}/hosts/arena/runners_content)
        set(CAPTURE ${CMAKE_CURRENT_BINARY_DIR}/client_drawn.tga)
        configure_file(tests/drawn.conf.in ${CMAKE_CURRENT_BINARY_DIR}/drawn.conf @ONLY)
        # The same in 2D, drawn on the one device and shown in the window:
        # its frames shown, and the screen lit by what they drew. The window
        # is resized while it plays, and the surface, the picture and the
        # view follow it.
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
        # Runners' HUD as a screen reader reads it: on a private
        # accessibility bus, the client's AT-SPI tree walked while it plays,
        # its window a frame holding the crosshair's image and the points'
        # label.
        add_test(NAME client_reads_runners_aloud
                 COMMAND ${RAWFRAME_SHELL} ${PROJECT_SOURCE_DIR}/tools/xvfb_run.sh -s "-screen 0 1280x720x24 -br" ${RAWFRAME_SHELL}
                         ${CMAKE_CURRENT_SOURCE_DIR}/tests/read_aloud.sh $<TARGET_FILE:rawframe_host_dedicated_server>
                         $<TARGET_FILE:rawframe_host_client> ${CMAKE_CURRENT_BINARY_DIR}/drawn.conf
                         games/runners/runners.game
                 WORKING_DIRECTORY ${PROJECT_SOURCE_DIR})
        set_tests_properties(client_reads_runners_aloud PROPERTIES
                             FIXTURES_REQUIRED runners_content
                             SKIP_REGULAR_EXPRESSION "skip: no device"
                             PASS_REGULAR_EXPRESSION
                             "a11y: 1 frame [^\n]*\na11y: 2 image *\na11y: 2 label 0 points\n.*\"code\":\"accessibility_ready\"")
        # Stalls' sign field written in: a click gives it the keyboard, the
        # keys type into it and no action, Return gives its text to the
        # game, which sends it to the server as the owner's sign, a command
        # the server takes; the sign comes back to the client and its board
        # shows it.
        set(TYCOON_CONTENT ${PROJECT_BINARY_DIR}/hosts/arena/tycoon_content)
        configure_file(tests/stalls_clicked.conf.in ${CMAKE_CURRENT_BINARY_DIR}/stalls_clicked.conf @ONLY)
        add_test(NAME client_types_stalls
                 COMMAND ${RAWFRAME_SHELL} ${PROJECT_SOURCE_DIR}/tools/xvfb_run.sh -s "-screen 0 1280x720x24 -br" ${RAWFRAME_SHELL}
                         ${CMAKE_CURRENT_SOURCE_DIR}/tests/clicked.sh $<TARGET_FILE:rawframe_host_dedicated_server>
                         $<TARGET_FILE:rawframe_host_client> ${CMAKE_CURRENT_BINARY_DIR}/stalls_clicked.conf
                         games/tycoon/tycoon.game 60,95:ann until=ui_submitted
                 WORKING_DIRECTORY ${PROJECT_SOURCE_DIR})
        set_tests_properties(client_types_stalls PROPERTIES
                             FIXTURES_REQUIRED tycoon_content
                             SKIP_REGULAR_EXPRESSION "skip: no device"
                             PASS_REGULAR_EXPRESSION
                             "typed ann at 60,95.*\"commandsTaken\":1,\"commandsLimited\":0.*\"bodiesMade\":1,.*\"code\":\"ui_summary\"[^\n]*\"presses\":1,\"focused\":1,\"typed\":[4-9][0-9]*,\"submitted\":1,\"typedShown\":[1-9][0-9]*,.*\"commandsSent\":1,")
        # The plaza, its walker's scene drawn in 3D and shown in the window:
        # the scene's models drawn in frames the window showed, antialiased
        # over time, each reusing the picture before, its exposure metered,
        # the picture read back in many shades, and the window's HDR
        # capability told, SDR offered and used.
        set(PLAZA_CONTENT ${PROJECT_BINARY_DIR}/hosts/arena/plaza_content)
        set(CAPTURE ${CMAKE_CURRENT_BINARY_DIR}/client_plaza.tga)
        configure_file(tests/plaza_drawn.conf.in ${CMAKE_CURRENT_BINARY_DIR}/plaza_drawn.conf @ONLY)
        add_test(NAME client_plaza_drawn
                 COMMAND ${RAWFRAME_SHELL} ${PROJECT_SOURCE_DIR}/tools/xvfb_run.sh -s "-screen 0 1280x720x24 -br" ${RAWFRAME_SHELL}
                         ${CMAKE_CURRENT_SOURCE_DIR}/tests/drawn.sh $<TARGET_FILE:rawframe_host_dedicated_server>
                         $<TARGET_FILE:rawframe_host_client> ${CMAKE_CURRENT_BINARY_DIR}/plaza_drawn.conf
                         games/plaza/plaza.game ${PROJECT_BINARY_DIR}/hosts/arena/plaza_server.conf
                 WORKING_DIRECTORY ${PROJECT_SOURCE_DIR})
        set_tests_properties(client_plaza_drawn PROPERTIES
                             FIXTURES_REQUIRED plaza_content
                             SKIP_REGULAR_EXPRESSION "skip: no device"
                             PASS_REGULAR_EXPRESSION
                             "the screen showed [1-9][0-9][0-9][0-9][0-9]+ lit pixels.*\"code\":\"output_record\"[^\n]*\"revision\":1,\"offered\":\"sdr_srgb[^\"]*\",\"active\":\"sdr_srgb\".*\"code\":\"scene_drawing_summary\"[^\n]*\"frames\":[1-9][0-9]+,[^\n]*\"models\":[1-9][0-9]+,[^\n]*\"modelsLeftOut\":0,[^\n]*\"historyReused\":[1-9][0-9]+,\"framesMetered\":[1-9][0-9]+.*\"code\":\"frame_summary\"[^\n]*\"framesShown\":[1-9][0-9]+,[^\n]*\"colors\":[1-9][0-9][0-9]+,\"captured\":true")
        # The plaza previewed for an authoring session: the client's own
        # tooling endpoint, with the view grant, attached as a scene's
        # preview, shown the scene's views, split four ways, and let go,
        # which gives the player's camera back. Four views draw the scene
        # four times into one frame, past Maul RHI's default frame limits.
        add_test(NAME client_previews_plaza
                 COMMAND ${RAWFRAME_SHELL} ${PROJECT_SOURCE_DIR}/tools/xvfb_run.sh -s "-screen 0 1280x720x24 -br" ${RAWFRAME_SHELL}
                         ${CMAKE_CURRENT_SOURCE_DIR}/tests/previewed.sh $<TARGET_FILE:rawframe_host_dedicated_server>
                         $<TARGET_FILE:rawframe_host_client> $<TARGET_FILE:rawframe_host_author>
                         ${CMAKE_CURRENT_BINARY_DIR}/plaza_drawn.conf games/plaza/plaza.game gate.scene
                         ${PROJECT_BINARY_DIR}/hosts/arena/plaza_server.conf
                 WORKING_DIRECTORY ${PROJECT_SOURCE_DIR})
        set_tests_properties(client_previews_plaza PROPERTIES
                             FIXTURES_REQUIRED plaza_content
                             SKIP_REGULAR_EXPRESSION "skip: no device"
                             PASS_REGULAR_EXPRESSION
                             "\"id\":2,\"answer\":{\"kind\":\"authoring.preview\",\"reopened\":false,\"previewing\":true}.*\"id\":3,\"answer\":{\"kind\":\"authoring.view\",[^\n]*\"previewing\":true}.*\"id\":4,\"answer\":{\"kind\":\"authoring.view\",[^\n]*\"previewing\":true}.*\"id\":5,\"answer\":{\"kind\":\"authoring.split\",\"views\":4,\"shown\":true}.*\"id\":6,\"answer\":{\"kind\":\"authoring.preview\",\"reopened\":false,\"previewing\":false}.*\"code\":\"scene_summary\"[^\n]*\"framesPreviewed\":[1-9][0-9]*,")
        # The plaza shown on a headset, under Monado's simulated one
        # (tools/xr_run.sh): drawn for both its eyes, its simulated
        # controllers read in both hands, located, and fed to the player's
        # mapper as the client's controller device. A flat bot plays beside
        # it in the client's process: flat and headset players share one
        # World and one admission path.
        if(RAWFRAME_CLIENT_HEADSET AND RAWFRAME_MONADO_SERVICE AND NOT WIN32)
            configure_file(tests/plaza_headset.conf.in ${CMAKE_CURRENT_BINARY_DIR}/plaza_headset.conf @ONLY)
            add_test(NAME client_plaza_headset
                     COMMAND ${RAWFRAME_SHELL} ${PROJECT_SOURCE_DIR}/tools/xvfb_run.sh -s "-screen 0 1280x720x24 -br" ${RAWFRAME_SHELL}
                             ${PROJECT_SOURCE_DIR}/tools/xr_run.sh ${RAWFRAME_SHELL} ${CMAKE_CURRENT_SOURCE_DIR}/tests/drawn.sh
                             $<TARGET_FILE:rawframe_host_dedicated_server> $<TARGET_FILE:rawframe_host_client>
                             ${CMAKE_CURRENT_BINARY_DIR}/plaza_headset.conf games/plaza/plaza.game
                             ${PROJECT_BINARY_DIR}/hosts/arena/plaza_server.conf 1
                     WORKING_DIRECTORY ${PROJECT_SOURCE_DIR})
            set_tests_properties(client_plaza_headset PROPERTIES
                                 FIXTURES_REQUIRED plaza_content
                                 PROCESSORS 2
                                 PASS_REGULAR_EXPRESSION
                                 "\"code\":\"xr_profiles\"[^\n]*\"right\":\"/interaction_profiles/khr/simple_controller\".*\"code\":\"xr_session_state\"[^\n]*\"state\":\"focused\".*\"code\":\"scene_eyes_drawing_summary\"[^\n]*\"eyes\":2,\"frames\":[1-9][0-9]+}.*\"code\":\"bots_summary\"[^\n]*\"bots\":2,\"admitted\":2,.*\"code\":\"xr_summary\"[^\n]*\"framesSubmitted\":[1-9][0-9]+,[^\n]*\"framesHandLocated\":[1-9][0-9]+,\"controllerEvents\":[1-9][0-9]+,\"handsPointing\":[1-9][0-9]+,\"hapticsFelt\":0,\"hapticsUnfelt\":0,\"away\":[1-9][0-9]*,\"failed\":false}.*\"code\":\"input_summary\"[^\n]*\"player\":0,[^\n]*\"controllerEvents\":[1-9][0-9]+,")
        endif()
        # Each draws a window on lavapipe beside a server, two of a preset's
        # processors each, so a check starts fewer at once and none is
        # starved past its input.
        set_tests_properties(client_plays_runners client_drawn client_reads_runners_aloud client_types_stalls client_plaza_drawn client_previews_plaza
                             PROPERTIES PROCESSORS 2)
    endif()
    # A record a test waited for and never saw fails it at once, and so
    # does a frame the client could not make: its pattern may already have
    # matched before the frames stopped.
    get_property(client_tests DIRECTORY PROPERTY TESTS)
    set_tests_properties(${client_tests} PROPERTIES LABELS integration FAIL_REGULAR_EXPRESSION
                                                   "never saw;\"code\":\"frame_failed\"")
endif()
