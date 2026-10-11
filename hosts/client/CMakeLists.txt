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
# Where OpenXR is built, a headset shows it too (D593).
set(RAWFRAME_CLIENT_HEADSET OFF)
if(TARGET rawframe_xr)
    set(RAWFRAME_CLIENT_HEADSET ON)
    target_link_libraries(rawframe_host_client PRIVATE rawframe::xr)
endif()
target_compile_definitions(rawframe_host_client PRIVATE RAWFRAME_CLIENT_DRAWS=$<BOOL:${RAWFRAME_CLIENT_DRAWS}>
                                                        RAWFRAME_CLIENT_HEADSET=$<BOOL:${RAWFRAME_CLIENT_HEADSET}>)

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
        # Runners' player presses L, and the HUD's words are given in the
        # game's next locale, Turkish, while it plays (D539).
        add_test(NAME client_speaks_runners
                 COMMAND ${RAWFRAME_SHELL} ${PROJECT_SOURCE_DIR}/tools/xvfb_run.sh -s "-screen 0 1280x720x24 -br" ${RAWFRAME_SHELL}
                         ${CMAKE_CURRENT_SOURCE_DIR}/tests/clicked.sh $<TARGET_FILE:rawframe_host_dedicated_server>
                         $<TARGET_FILE:rawframe_host_client> ${CMAKE_CURRENT_BINARY_DIR}/drawn.conf
                         games/runners/runners.game keys=l
                 WORKING_DIRECTORY ${PROJECT_SOURCE_DIR})
        set_tests_properties(client_speaks_runners PROPERTIES
                             FIXTURES_REQUIRED runners_content
                             SKIP_REGULAR_EXPRESSION "skip: no device"
                             PASS_REGULAR_EXPRESSION
                             "\"code\":\"input_seen\"[^\n]*\"device\":\"keyboard\".*\"code\":\"text_summary\"[^\n]*\"locale\":\"tr\",\"offered\":2,\"changes\":1}")
        # Runners' HUD as a screen reader reads it (D571): on a private
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
                             LABELS window
                             FIXTURES_REQUIRED runners_content
                             SKIP_REGULAR_EXPRESSION "skip: no device"
                             PASS_REGULAR_EXPRESSION
                             "a11y: 1 frame [^\n]*\na11y: 2 image *\na11y: 2 label 0 points\n.*\"code\":\"accessibility_ready\"")
        # Stalls' HUD read aloud and its purse pressed by a screen reader
        # (D572): a push button named by its coins and the sign's entry, and
        # AT-SPI's DoAction on the button reaching the game as a press, the
        # player's one collect command sent and taken by the server. Read
        # without pressing, it sends none.
        add_test(NAME client_presses_stalls_aloud
                 COMMAND ${RAWFRAME_SHELL} ${PROJECT_SOURCE_DIR}/tools/xvfb_run.sh -s "-screen 0 1280x720x24 -br" ${RAWFRAME_SHELL}
                         ${CMAKE_CURRENT_SOURCE_DIR}/tests/read_aloud.sh $<TARGET_FILE:rawframe_host_dedicated_server>
                         $<TARGET_FILE:rawframe_host_client> ${CMAKE_CURRENT_BINARY_DIR}/stalls_clicked.conf
                         games/tycoon/tycoon.game press
                 WORKING_DIRECTORY ${PROJECT_SOURCE_DIR})
        set_tests_properties(client_presses_stalls_aloud PROPERTIES
                             FIXTURES_REQUIRED tycoon_content
                             SKIP_REGULAR_EXPRESSION "skip: no device"
                             PASS_REGULAR_EXPRESSION
                             "a11y: 2 push button [0-9]+ coins\na11y: 2 entry Write your sign\na11y: 2 slider Zoom\na11y: 2 toggle button Top view\na11y: pressed [^\n]*: b true.*\"commandsTaken\":1,.*\"commandsSent\":1,")
        # Stalls' zoom slider set by a screen reader (D621): read as "slider
        # Zoom" at 100, set to 120 through AT-SPI's Value as the tree's own
        # range does it, read back at 120, and the knob the present system
        # places from it moved.
        add_test(NAME client_slides_stalls_aloud
                 COMMAND ${RAWFRAME_SHELL} ${PROJECT_SOURCE_DIR}/tools/xvfb_run.sh -s "-screen 0 1280x720x24 -br" ${RAWFRAME_SHELL}
                         ${CMAKE_CURRENT_SOURCE_DIR}/tests/read_aloud.sh $<TARGET_FILE:rawframe_host_dedicated_server>
                         $<TARGET_FILE:rawframe_host_client> ${CMAKE_CURRENT_BINARY_DIR}/stalls_clicked.conf
                         games/tycoon/tycoon.game slide 120
                 WORKING_DIRECTORY ${PROJECT_SOURCE_DIR})
        set_tests_properties(client_slides_stalls_aloud PROPERTIES
                             FIXTURES_REQUIRED tycoon_content
                             SKIP_REGULAR_EXPRESSION "skip: no device"
                             PASS_REGULAR_EXPRESSION
                             "a11y: 2 slider Zoom\na11y: 2 toggle button Top view\na11y: slid [^\n]*: done\na11y: the slider reads 120\n.*\"code\":\"ui_summary\"[^\n]*\"changed\":([2-9]|[1-9][0-9]+),")
        # Stalls' top view switch pressed by a screen reader (D622): read as
        # a toggle button named Top view, pressed through AT-SPI's Action,
        # checked after, and its knob, placed from `ui.value`, moved.
        add_test(NAME client_toggles_stalls_aloud
                 COMMAND ${RAWFRAME_SHELL} ${PROJECT_SOURCE_DIR}/tools/xvfb_run.sh -s "-screen 0 1280x720x24 -br" ${RAWFRAME_SHELL}
                         ${CMAKE_CURRENT_SOURCE_DIR}/tests/read_aloud.sh $<TARGET_FILE:rawframe_host_dedicated_server>
                         $<TARGET_FILE:rawframe_host_client> ${CMAKE_CURRENT_BINARY_DIR}/stalls_clicked.conf
                         games/tycoon/tycoon.game toggle
                 WORKING_DIRECTORY ${PROJECT_SOURCE_DIR})
        set_tests_properties(client_toggles_stalls_aloud PROPERTIES
                             FIXTURES_REQUIRED tycoon_content
                             SKIP_REGULAR_EXPRESSION "skip: no device"
                             PASS_REGULAR_EXPRESSION
                             "a11y: 2 toggle button Top view\na11y: toggled [^\n]*: b true\na11y: the toggle button reads checked\n.*\"code\":\"ui_summary\"[^\n]*\"changed\":([1-9]|[1-9][0-9]+),[^\n]*\"toggled\":1}")
        # Stalls' top view switch clicked (D622): the click checks it, as
        # the summary counts, and the zoom slider, disabled while it is on
        # (D624), takes nothing of a drag of its knob.
        add_test(NAME client_toggles_stalls
                 COMMAND ${RAWFRAME_SHELL} ${PROJECT_SOURCE_DIR}/tools/xvfb_run.sh -s "-screen 0 1280x720x24 -br" ${RAWFRAME_SHELL}
                         ${CMAKE_CURRENT_SOURCE_DIR}/tests/clicked.sh $<TARGET_FILE:rawframe_host_dedicated_server>
                         $<TARGET_FILE:rawframe_host_client> ${CMAKE_CURRENT_BINARY_DIR}/stalls_clicked.conf
                         games/tycoon/tycoon.game 30,221 drag=92,180,132,180
                 WORKING_DIRECTORY ${PROJECT_SOURCE_DIR})
        set_tests_properties(client_toggles_stalls PROPERTIES
                             FIXTURES_REQUIRED tycoon_content
                             SKIP_REGULAR_EXPRESSION "skip: no device"
                             PASS_REGULAR_EXPRESSION
                             "\"code\":\"ui_summary\"[^\n]*\"ranged\":0,\"toggled\":1}")
        # Stalls' purse tooltip (D623): the mouse brought over the purse
        # and away; the tooltip given its words while it is over it and
        # emptied after, as the present system says, beside the purse (a
        # run without it gives four nodes words, this one five).
        add_test(NAME client_tips_stalls
                 COMMAND ${RAWFRAME_SHELL} ${PROJECT_SOURCE_DIR}/tools/xvfb_run.sh -s "-screen 0 1280x720x24 -br" ${RAWFRAME_SHELL}
                         ${CMAKE_CURRENT_SOURCE_DIR}/tests/clicked.sh $<TARGET_FILE:rawframe_host_dedicated_server>
                         $<TARGET_FILE:rawframe_host_client> ${CMAKE_CURRENT_BINARY_DIR}/stalls_clicked.conf
                         games/tycoon/tycoon.game move=30,30 wait=2 move=600,600 wait=2
                 WORKING_DIRECTORY ${PROJECT_SOURCE_DIR})
        set_tests_properties(client_tips_stalls PROPERTIES
                             FIXTURES_REQUIRED tycoon_content
                             SKIP_REGULAR_EXPRESSION "skip: no device"
                             PASS_REGULAR_EXPRESSION
                             "\"code\":\"ui_summary\"[^\n]*\"changed\":([2-9]|[1-9][0-9]+),[^\n]*\"texts\":([5-9]|[1-9][0-9]+),")
        # Stalls' sign typed by a screen reader (D613): its entry, named by
        # its label's words and read as empty while it shows them, takes
        # text inserted through AT-SPI's EditableText as the field's own
        # typing, and reads it back.
        add_test(NAME client_types_stalls_aloud
                 COMMAND ${RAWFRAME_SHELL} ${PROJECT_SOURCE_DIR}/tools/xvfb_run.sh -s "-screen 0 1280x720x24 -br" ${RAWFRAME_SHELL}
                         ${CMAKE_CURRENT_SOURCE_DIR}/tests/read_aloud.sh $<TARGET_FILE:rawframe_host_dedicated_server>
                         $<TARGET_FILE:rawframe_host_client> ${CMAKE_CURRENT_BINARY_DIR}/stalls_clicked.conf
                         games/tycoon/tycoon.game type ann
                 WORKING_DIRECTORY ${PROJECT_SOURCE_DIR})
        set_tests_properties(client_types_stalls_aloud PROPERTIES
                             FIXTURES_REQUIRED tycoon_content
                             SKIP_REGULAR_EXPRESSION "skip: no device"
                             PASS_REGULAR_EXPRESSION
                             "a11y: 2 entry [^\n]+\n.*a11y: typed into [^ ]+: b true\na11y: the entry reads ann\n.*\"code\":\"ui_summary\"[^\n]*\"typed\":[1-9]")
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
        # Stalls' zoom slider dragged (D621): its knob, at the middle of the
        # track under the board, carried to the right; the range takes the
        # press, the drag, and the release, and the knob, which the present
        # system places from the value it reads, moves with it (a run
        # without the drag changes one node, the purse).
        add_test(NAME client_slides_stalls
                 COMMAND ${RAWFRAME_SHELL} ${PROJECT_SOURCE_DIR}/tools/xvfb_run.sh -s "-screen 0 1280x720x24 -br" ${RAWFRAME_SHELL}
                         ${CMAKE_CURRENT_SOURCE_DIR}/tests/clicked.sh $<TARGET_FILE:rawframe_host_dedicated_server>
                         $<TARGET_FILE:rawframe_host_client> ${CMAKE_CURRENT_BINARY_DIR}/stalls_clicked.conf
                         games/tycoon/tycoon.game drag=92,180,132,180
                 WORKING_DIRECTORY ${PROJECT_SOURCE_DIR})
        set_tests_properties(client_slides_stalls PROPERTIES
                             FIXTURES_REQUIRED tycoon_content
                             SKIP_REGULAR_EXPRESSION "skip: no device"
                             PASS_REGULAR_EXPRESSION
                             "\"code\":\"ui_summary\"[^\n]*\"changed\":([2-9]|[1-9][0-9]+),[^\n]*\"ranged\":([3-9]|[1-9][0-9]+),")
        # Stalls' sign field written in (D426): a click gives it the
        # keyboard, the keys type into it and no action, Return gives its
        # text to the sample, which sends it to the server as the owner's
        # sign, a command the server takes; the sign comes back to the client
        # and its board shows it (D427).
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
        # The same, the click and every key read in one frame as a slow
        # machine's (D477): the client stopped from the click to the Return,
        # the press gives the field the keyboard as the window host reads
        # it, so the keys after it still reach the field. It waits until
        # navigation reaches the purse and the field, which are then drawn.
        add_test(NAME client_types_stalls_frozen
                 COMMAND ${RAWFRAME_SHELL} ${PROJECT_SOURCE_DIR}/tools/xvfb_run.sh -s "-screen 0 1280x720x24 -br" ${RAWFRAME_SHELL}
                         ${CMAKE_CURRENT_SOURCE_DIR}/tests/clicked.sh $<TARGET_FILE:rawframe_host_dedicated_server>
                         $<TARGET_FILE:rawframe_host_client> ${CMAKE_CURRENT_BINARY_DIR}/stalls_clicked.conf
                         games/tycoon/tycoon.game until=ui_reachable,reachable=2 frozenclick=60,95:ann until=ui_submitted
                 WORKING_DIRECTORY ${PROJECT_SOURCE_DIR})
        set_tests_properties(client_types_stalls_frozen PROPERTIES
                             FIXTURES_REQUIRED tycoon_content
                             SKIP_REGULAR_EXPRESSION "skip: no device"
                             PASS_REGULAR_EXPRESSION
                             "clicked frozen at 60,95.*typed ann at 60,95.*\"commandsTaken\":1,\"commandsLimited\":0.*\"bodiesMade\":1,.*\"code\":\"ui_summary\"[^\n]*\"presses\":1,\"focused\":1,\"typed\":[4-9][0-9]*,\"submitted\":1,\"typedShown\":[1-9][0-9]*,.*\"commandsSent\":1,")
        # Stalls' HUD without the mouse (D430): Tab, the game's menu action,
        # has the UI navigated, focus on the purse; Return presses it, and
        # the server collects; Tab, now the UI's, moves on to the sign
        # field, Return gives it the keyboard, and the sign typed and given
        # by Return goes to the server and back to the board. Each step that
        # moves focus is its own group of keys, so a loaded machine's input
        # never reads the next with it, before the step took effect; but the
        # first Tab and Return come a few milliseconds apart, as a quick
        # player's, within one world tick, and Return still finds the purse
        # (D506).
        add_test(NAME client_navigates_stalls
                 COMMAND ${RAWFRAME_SHELL} ${PROJECT_SOURCE_DIR}/tools/xvfb_run.sh -s "-screen 0 1280x720x24 -br" ${RAWFRAME_SHELL}
                         ${CMAKE_CURRENT_SOURCE_DIR}/tests/clicked.sh $<TARGET_FILE:rawframe_host_dedicated_server>
                         $<TARGET_FILE:rawframe_host_client> ${CMAKE_CURRENT_BINARY_DIR}/stalls_clicked.conf
                         games/tycoon/tycoon.game until=ui_reachable,reachable=2 quick=Tab,Return keys=Tab keys=Return keys=b,o,Return until=ui_submitted
                 WORKING_DIRECTORY ${PROJECT_SOURCE_DIR})
        set_tests_properties(client_navigates_stalls PROPERTIES
                             FIXTURES_REQUIRED tycoon_content
                             SKIP_REGULAR_EXPRESSION "skip: no device"
                             PASS_REGULAR_EXPRESSION
                             "pressed b,o,Return.*\"commandsTaken\":2,\"commandsLimited\":0.*\"code\":\"ui_summary\"[^\n]*\"presses\":0,\"focused\":1,\"typed\":[3-9][0-9]*,\"submitted\":1,\"typedShown\":[1-9][0-9]*,\"navigated\":[2-9][0-9]*,\"activated\":1,")
        # The same keys, groups read in one frame as a slow machine's: the
        # client stopped while Tab enters navigation and Return presses the
        # purse (D476), and while Return gives the sign field the keyboard
        # and the sign is typed and given (D458), every key still reaches
        # what it was pressed for. Both wait until navigation reaches the
        # purse and the field, so a loaded machine does not Tab before the
        # purse is there (D476); the frozen one waits until the sign is
        # given before it stops the client, which on the slowest tree at
        # load drew a frame a second (D489).
        # Whether a frame shows the words before they are submitted is not
        # held here: read in one frame, the field takes the keyboard, the
        # words, and Return together, and a frame between them is the
        # machine's to give (D505). That none is lost is what this holds.
        add_test(NAME client_navigates_stalls_frozen
                 COMMAND ${RAWFRAME_SHELL} ${PROJECT_SOURCE_DIR}/tools/xvfb_run.sh -s "-screen 0 1280x720x24 -br" ${RAWFRAME_SHELL}
                         ${CMAKE_CURRENT_SOURCE_DIR}/tests/clicked.sh $<TARGET_FILE:rawframe_host_dedicated_server>
                         $<TARGET_FILE:rawframe_host_client> ${CMAKE_CURRENT_BINARY_DIR}/stalls_clicked.conf
                         games/tycoon/tycoon.game until=ui_reachable,reachable=2 frozen=Tab,Return keys=Tab frozen=Return,b,o,Return
                         until=ui_submitted
                 WORKING_DIRECTORY ${PROJECT_SOURCE_DIR})
        set_tests_properties(client_navigates_stalls_frozen PROPERTIES
                             FIXTURES_REQUIRED tycoon_content
                             SKIP_REGULAR_EXPRESSION "skip: no device"
                             PASS_REGULAR_EXPRESSION
                             "pressed frozen Return,b,o,Return.*\"commandsTaken\":2,\"commandsLimited\":0.*\"code\":\"ui_summary\"[^\n]*\"presses\":0,\"focused\":1,\"typed\":3,\"submitted\":1,\"typedShown\":[0-9]+,\"navigated\":[2-9][0-9]*,\"activated\":1,")
        # The plaza, its walker's scene drawn in 3D and shown in the window
        # (D286): the scene's models drawn in frames the window showed,
        # antialiased over time, each reusing the picture before (D291), its
        # exposure metered (D293), the screen lit by them, the picture read
        # back in many shades, and the window's HDR capability record told,
        # SDR offered and used (D365).
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
        # The plaza's view turned as its player drags with the right mouse
        # button held (D521): the scene's last view looks more than a radian
        # away from where it started. The turn is the server's, so it needs
        # its input to arrive, which a client drawing under ten frames a
        # second does not manage (D521): played by the fast check with no
        # other test beside it, four seconds after admission, once its input
        # is paced.
        add_test(NAME client_turns_plaza
                 COMMAND ${RAWFRAME_SHELL} ${PROJECT_SOURCE_DIR}/tools/xvfb_run.sh -s "-screen 0 1280x720x24 -br" ${RAWFRAME_SHELL}
                         ${CMAKE_CURRENT_SOURCE_DIR}/tests/clicked.sh $<TARGET_FILE:rawframe_host_dedicated_server>
                         $<TARGET_FILE:rawframe_host_client> ${CMAKE_CURRENT_BINARY_DIR}/plaza_drawn.conf
                         games/plaza/plaza.game wait=4 orbit=900,360,300,360
                 WORKING_DIRECTORY ${PROJECT_SOURCE_DIR})
        set_tests_properties(client_turns_plaza PROPERTIES
                             FIXTURES_REQUIRED plaza_content
                             SKIP_REGULAR_EXPRESSION "skip: no device"
                             PASS_REGULAR_EXPRESSION "\"code\":\"scene_summary\"[^\n]*\"yaw\":-?[1-3](\\.[0-9]+)?}")
        # The plaza previewed for an authoring session (D432, D433): the
        # client's own tooling endpoint, with the view grant, attached as a
        # scene's preview, shown the scene's views, and let go, which gives
        # the player's camera back.
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
                             "\"id\":2,\"answer\":{\"kind\":\"authoring.preview\",\"reopened\":false,\"previewing\":true}.*\"id\":3,\"answer\":{\"kind\":\"authoring.view\",[^\n]*\"previewing\":true}.*\"id\":4,\"answer\":{\"kind\":\"authoring.view\",[^\n]*\"previewing\":true}.*\"id\":5,\"answer\":{\"kind\":\"authoring.preview\",\"reopened\":false,\"previewing\":false}.*\"code\":\"scene_summary\"[^\n]*\"framesPreviewed\":[1-9][0-9]*,")
        # The plaza shown on a headset (D596), under Monado's simulated one
        # (tools/xr_run.sh): drawn for both its eyes, its simulated
        # controllers read in both hands, located, and fed to the player's
        # mapper as the client's controller device, both aiming at the
        # panel (D598). A flat bot plays beside it in the client's process:
        # ADR-0081's cross-play, one World and one admission path for both
        # (D606).
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
                                 "\"code\":\"xr_session_state\"[^\n]*\"state\":\"focused\".*\"code\":\"scene_eyes_drawing_summary\"[^\n]*\"eyes\":2,\"frames\":[1-9][0-9]+}.*\"code\":\"bots_summary\"[^\n]*\"bots\":2,\"admitted\":2,.*\"code\":\"xr_summary\"[^\n]*\"framesSubmitted\":[1-9][0-9]+,[^\n]*\"framesHandLocated\":[1-9][0-9]+,\"controllerEvents\":[1-9][0-9]+,\"handsPointing\":[1-9][0-9]+,\"hapticsFelt\":0,\"hapticsUnfelt\":0,\"failed\":false}.*\"code\":\"input_summary\"[^\n]*\"player\":0,[^\n]*\"controllerEvents\":[1-9][0-9]+,")
        endif()
        # Stalls on a headset (D605): its HUD on the panel, the hands' aims
        # the UI's pointer, a cursor drawn where they point.
        if(RAWFRAME_CLIENT_HEADSET AND RAWFRAME_MONADO_SERVICE AND NOT WIN32)
            configure_file(tests/stalls_headset.conf.in ${CMAKE_CURRENT_BINARY_DIR}/stalls_headset.conf @ONLY)
            # The server plays Stalls from its cooked content, as the client
            # does (D512).
            file(WRITE ${CMAKE_CURRENT_BINARY_DIR}/stalls_server.conf "content.root = ${TYCOON_CONTENT}\n")
            add_test(NAME client_stalls_headset
                     COMMAND ${RAWFRAME_SHELL} ${PROJECT_SOURCE_DIR}/tools/xvfb_run.sh -s "-screen 0 1280x720x24 -br" ${RAWFRAME_SHELL}
                             ${PROJECT_SOURCE_DIR}/tools/xr_run.sh ${RAWFRAME_SHELL} ${CMAKE_CURRENT_SOURCE_DIR}/tests/drawn.sh
                             $<TARGET_FILE:rawframe_host_dedicated_server> $<TARGET_FILE:rawframe_host_client>
                             ${CMAKE_CURRENT_BINARY_DIR}/stalls_headset.conf games/tycoon/tycoon.game
                             ${CMAKE_CURRENT_BINARY_DIR}/stalls_server.conf
                     WORKING_DIRECTORY ${PROJECT_SOURCE_DIR})
            set_tests_properties(client_stalls_headset PROPERTIES
                                 FIXTURES_REQUIRED tycoon_content
                                 PROCESSORS 2
                                 PASS_REGULAR_EXPRESSION
                                 "\"code\":\"ui_drawing_summary\"[^\n]*\"frames\":[1-9][0-9]+,\"framesOnPanel\":[1-9][0-9]+,[^\n]*\"glyphRuns\":[1-9][0-9]+,.*\"code\":\"ui_summary\"[^\n]*\"framesPointed\":[1-9][0-9]+,.*\"code\":\"xr_summary\"[^\n]*\"framesPanelSubmitted\":[1-9][0-9]+,[^\n]*\"handsPointing\":[1-9][0-9]+,[^\n]*\"failed\":false")
        endif()
        # Each draws a window on lavapipe beside a server, and bots or a
        # second client in some: two of a preset's processors each, so a full
        # check, eight trees at once, starts fewer at once and none is starved
        # past its input (D491), as Studio's played tests (D467, D496).
        set_tests_properties(client_plays_runners client_drawn client_speaks_runners client_clicks_stalls client_types_stalls client_types_stalls_frozen client_navigates_stalls client_navigates_stalls_frozen client_plaza_drawn client_previews_plaza
                             client_turns_plaza
                             PROPERTIES PROCESSORS 2)
    endif()
    # Every client test here draws a window, the content it plays cooked
    # first: labeled so the check can leave them out of a tree they find
    # nothing in (D493).
    get_property(window_tests DIRECTORY PROPERTY TESTS)
    list(FILTER window_tests INCLUDE REGEX "^client_")
    set_tests_properties(${window_tests} PROPERTIES LABELS window)
    if(TEST client_turns_plaza)
        set_tests_properties(client_turns_plaza PROPERTIES LABELS "window;alone" RUN_SERIAL TRUE)
    endif()
    # Stalls' headset panel is played where the headset tests are, by the
    # fast check's clang-development: the plaza's headset client covers the
    # client's headset path under the sanitizers (D602), and this one took
    # 66 s in each instrumented tree, their longest (D616).
    if(TEST client_stalls_headset)
        set_tests_properties(client_stalls_headset PROPERTIES LABELS "window;headset")
    endif()
endif()
