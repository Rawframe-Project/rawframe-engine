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
        configure_file(tests/runners_clicked.conf.in ${CMAKE_CURRENT_BINARY_DIR}/runners_clicked.conf @ONLY)
        # Runners' effects volume slider dragged from its top to its least
        # (D625): the present system sets the player's effects volume from
        # its value, and the client's sound, played to no device, takes it;
        # the two summaries come in either order as the client stops.
        # The same slider dragged with the mouse played as a finger (D809):
        # the game's input sees a touch and no mouse, and the slider moves
        # as a finger moves it.
        configure_file(tests/runners_touched.conf.in ${CMAKE_CURRENT_BINARY_DIR}/runners_touched.conf @ONLY)
        add_test(NAME client_touches_with_mouse
                 COMMAND ${RAWFRAME_SHELL} ${PROJECT_SOURCE_DIR}/tools/xvfb_run.sh -s "-screen 0 1280x720x24 -br" ${RAWFRAME_SHELL}
                         ${CMAKE_CURRENT_SOURCE_DIR}/tests/clicked.sh $<TARGET_FILE:rawframe_host_dedicated_server>
                         $<TARGET_FILE:rawframe_host_client> ${CMAKE_CURRENT_BINARY_DIR}/runners_touched.conf
                         games/runners/runners.game drag=1261,19,1131,19
                 WORKING_DIRECTORY ${PROJECT_SOURCE_DIR})
        set_tests_properties(client_touches_with_mouse PROPERTIES
                             FIXTURES_REQUIRED runners_content
                             SKIP_REGULAR_EXPRESSION "skip: no device"
                             FAIL_REGULAR_EXPRESSION "\"device\":\"mouse\""
                             PASS_REGULAR_EXPRESSION
                             "\"code\":\"input_seen\"[^\n]*\"device\":\"touch\".*\"code\":\"ui_summary\"[^\n]*\"ranged\":([3-9]|[1-9][0-9]+),")
        add_test(NAME client_quiets_runners
                 COMMAND ${RAWFRAME_SHELL} ${PROJECT_SOURCE_DIR}/tools/xvfb_run.sh -s "-screen 0 1280x720x24 -br" ${RAWFRAME_SHELL}
                         ${CMAKE_CURRENT_SOURCE_DIR}/tests/clicked.sh $<TARGET_FILE:rawframe_host_dedicated_server>
                         $<TARGET_FILE:rawframe_host_client> ${CMAKE_CURRENT_BINARY_DIR}/runners_clicked.conf
                         games/runners/runners.game drag=1261,19,1131,19
                 WORKING_DIRECTORY ${PROJECT_SOURCE_DIR})
        set_tests_properties(client_quiets_runners PROPERTIES
                             FIXTURES_REQUIRED runners_content
                             SKIP_REGULAR_EXPRESSION "skip: no device"
                             PASS_REGULAR_EXPRESSION
                             "(\"code\":\"ui_summary\"[^\n]*\"ranged\":([3-9]|[1-9][0-9]+),.*\"what one client played\"[^\n]*\"volumesSet\":[1-9]|\"what one client played\"[^\n]*\"volumesSet\":[1-9].*\"code\":\"ui_summary\"[^\n]*\"ranged\":([3-9]|[1-9][0-9]+),)")
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
                             "a11y: 2 push button [0-9,]+ of 1,000 coins\na11y: 2 entry Write your sign\na11y: 2 slider Zoom\na11y: 2 toggle button Top view\na11y: pressed [^\n]*: b true.*\"commandsTaken\":1,.*\"commandsSent\":1,")
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
                             "a11y: 2 toggle button Top view\na11y: toggled [^\n]*: b true\na11y: the toggle button reads checked\n.*\"code\":\"ui_summary\"[^\n]*\"changed\":([1-9]|[1-9][0-9]+),[^\n]*\"toggled\":1,")
        # Stalls' top view switch clicked (D622): the click checks it, as
        # the summary counts. The switch is kept (D626): written to the
        # client's settings as it changes, it starts checked when the client
        # plays again, which disables the zoom slider (D624) from the first
        # frame, so a drag of its knob takes nothing. A drag in the first
        # run raced the present system disabling the slider a tick after the
        # click, under load (D628).
        add_test(NAME client_keeps_stalls
                 COMMAND ${RAWFRAME_SHELL} ${PROJECT_SOURCE_DIR}/tools/xvfb_run.sh -s "-screen 0 1280x720x24 -br" ${RAWFRAME_SHELL}
                         ${CMAKE_CURRENT_SOURCE_DIR}/tests/kept.sh $<TARGET_FILE:rawframe_host_dedicated_server>
                         $<TARGET_FILE:rawframe_host_client> ${CMAKE_CURRENT_BINARY_DIR}/stalls_clicked.conf
                         games/tycoon/tycoon.game 30,221 then drag=92,180,132,180
                 WORKING_DIRECTORY ${PROJECT_SOURCE_DIR})
        set_tests_properties(client_keeps_stalls PROPERTIES
                             FIXTURES_REQUIRED tycoon_content
                             SKIP_REGULAR_EXPRESSION "skip: no device"
                             PASS_REGULAR_EXPRESSION
                             "kept: [^\n]*\"tycoon.top\":1[,}][^\n]*\n== first run\n.*\"code\":\"ui_summary\"[^\n]*\"toggled\":1,\"kept\":1,\"keptGiven\":0,\"keptWritten\":[1-9].*== second run\n.*\"code\":\"ui_summary\"[^\n]*\"ranged\":0,\"toggled\":0,\"kept\":0,\"keptGiven\":[1-9]")
        # Stalls' styles edited as it plays (D634): its HUD's dark panel
        # turned red in a copy of the game, the client styles its UI anew.
        add_test(NAME client_restyles_stalls
                 COMMAND ${RAWFRAME_SHELL} ${PROJECT_SOURCE_DIR}/tools/xvfb_run.sh -s "-screen 0 1280x720x24 -br" ${RAWFRAME_SHELL}
                         ${CMAKE_CURRENT_SOURCE_DIR}/tests/restyled.sh $<TARGET_FILE:rawframe_host_dedicated_server>
                         $<TARGET_FILE:rawframe_host_client> ${CMAKE_CURRENT_BINARY_DIR}/stalls_clicked.conf
                         ${PROJECT_SOURCE_DIR}/games/tycoon tycoon.game tycoon.styles "#101820c0" "#c01818c0"
                 WORKING_DIRECTORY ${PROJECT_SOURCE_DIR})
        set_tests_properties(client_restyles_stalls PROPERTIES
                             FIXTURES_REQUIRED tycoon_content
                             SKIP_REGULAR_EXPRESSION "skip: no device"
                             PASS_REGULAR_EXPRESSION
                             "edited [^\n]*tycoon.styles: #101820c0 to #c01818c0\n.*saw ui_restyled.*\"code\":\"ui_summary\"[^\n]*\"restyled\":1")
        # Stalls' UI document edited while it plays (D619): the sign made
        # wider; the client arranges its UI anew and presentation gives the
        # player's sign its new width.
        add_test(NAME client_rearranges_stalls
                 COMMAND ${RAWFRAME_SHELL} ${PROJECT_SOURCE_DIR}/tools/xvfb_run.sh -s "-screen 0 1280x720x24 -br" ${RAWFRAME_SHELL}
                         ${CMAKE_CURRENT_SOURCE_DIR}/tests/restyled.sh $<TARGET_FILE:rawframe_host_dedicated_server>
                         $<TARGET_FILE:rawframe_host_client> ${CMAKE_CURRENT_BINARY_DIR}/stalls_clicked.conf
                         ${PROJECT_SOURCE_DIR}/games/tycoon tycoon.game tycoon.ui "220," "260," ui_rearranged
                 WORKING_DIRECTORY ${PROJECT_SOURCE_DIR})
        set_tests_properties(client_rearranges_stalls PROPERTIES
                             FIXTURES_REQUIRED tycoon_content
                             SKIP_REGULAR_EXPRESSION "skip: no device"
                             PASS_REGULAR_EXPRESSION
                             "edited [^\n]*tycoon.ui: 220, to 260,\n.*saw ui_rearranged.*\"code\":\"presented_summary\"[^\n]*\"restarted\":1,")
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
                         games/tycoon/tycoon.game "until=ui_reachable,reachable>=2" frozenclick=60,95:ann until=ui_submitted
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
                         games/tycoon/tycoon.game "until=ui_reachable,reachable>=2" quick=Tab,Return keys=Tab keys=Return keys=b,o,Return until=ui_submitted
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
                         games/tycoon/tycoon.game "until=ui_reachable,reachable>=2" frozen=Tab,Return keys=Tab frozen=Return,b,o,Return
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
        # Gale's sea and ships drawn from behind the client's own ship
        # (D682), its bot at the helm and a second ship beside it, their
        # wakes and smoke (D692), and its HUD's words (D688).
        # Gale's sources cooked, so the client reads its HUD's words and font
        # (D688) and the server plays the same content.
        set(GALE_CONTENT ${CMAKE_CURRENT_BINARY_DIR}/gale_content)
        # A machine without the shader toolchain skips it, and the client's
        # test of what it cooked (D701, D707).
        add_test(NAME gale_cooked
                 COMMAND ${RAWFRAME_SHELL} ${CMAKE_CURRENT_SOURCE_DIR}/tests/cooked.sh $<TARGET_FILE:rawframe_host_cook_tool>
                         ${PROJECT_SOURCE_DIR}/games/gale ${GALE_CONTENT} ${CMAKE_CURRENT_BINARY_DIR}/gale_cook_cache)
        set_tests_properties(gale_cooked PROPERTIES
                             FIXTURES_SETUP gale_content
                             SKIP_REGULAR_EXPRESSION "skip: no shader toolchain"
                             PASS_REGULAR_EXPRESSION "failed 0")
        file(WRITE ${CMAKE_CURRENT_BINARY_DIR}/gale_server.conf "content.root = ${GALE_CONTENT}\n")
        configure_file(tests/gale_drawn.conf.in ${CMAKE_CURRENT_BINARY_DIR}/gale_drawn.conf @ONLY)
        add_test(NAME client_gale_drawn
                 COMMAND ${RAWFRAME_SHELL} ${CMAKE_CURRENT_SOURCE_DIR}/tests/cooked.sh --then ${GALE_CONTENT}
                         ${RAWFRAME_SHELL} ${PROJECT_SOURCE_DIR}/tools/xvfb_run.sh -s "-screen 0 1280x720x24 -br" ${RAWFRAME_SHELL}
                         ${CMAKE_CURRENT_SOURCE_DIR}/tests/drawn.sh $<TARGET_FILE:rawframe_host_dedicated_server>
                         $<TARGET_FILE:rawframe_host_client> ${CMAKE_CURRENT_BINARY_DIR}/gale_drawn.conf
                         games/gale/gale.game ${CMAKE_CURRENT_BINARY_DIR}/gale_server.conf 1
                 WORKING_DIRECTORY ${PROJECT_SOURCE_DIR})
        set_tests_properties(client_gale_drawn PROPERTIES
                             FIXTURES_REQUIRED gale_content
                             PROCESSORS 2
                             SKIP_REGULAR_EXPRESSION "skip: no device;skip: no shader toolchain"
                             PASS_REGULAR_EXPRESSION
                             "the screen showed [1-9][0-9][0-9][0-9][0-9]+ lit pixels.*\"code\":\"scene_drawing_summary\"[^\n]*\"frames\":[1-9][0-9]+,[^\n]*\"models\":[1-9][0-9]+,[^\n]*\"modelsLeftOut\":0,[^\n]*\"emittersDrawn\":[1-9][0-9]*,\"emittersLeftOut\":0,[^\n]*\"ribbonsDrawn\":[1-9][0-9]*,.*\"code\":\"ui_drawing_summary\"[^\n]*\"glyphRuns\":[1-9][0-9]*,.*\"code\":\"bots_summary\"[^\n]*\"bots\":2,\"admitted\":2,")
        # Quarry's land drawn (D736): every block a box of its kind's color,
        # some seven thousand, seen from above the land's near edge, played
        # from its sources, none left out.
        add_test(NAME client_quarry_drawn
                 COMMAND ${RAWFRAME_SHELL} ${PROJECT_SOURCE_DIR}/tools/xvfb_run.sh -s "-screen 0 1280x720x24 -br" ${RAWFRAME_SHELL}
                         ${CMAKE_CURRENT_SOURCE_DIR}/tests/drawn.sh $<TARGET_FILE:rawframe_host_dedicated_server>
                         $<TARGET_FILE:rawframe_host_client> ${CMAKE_CURRENT_SOURCE_DIR}/tests/quarry_drawn.conf
                         games/quarry/quarry.game ${CMAKE_CURRENT_SOURCE_DIR}/tests/quarry_server.conf
                 WORKING_DIRECTORY ${PROJECT_SOURCE_DIR})
        set_tests_properties(client_quarry_drawn PROPERTIES
                             PROCESSORS 2
                             SKIP_REGULAR_EXPRESSION "skip: no device"
                             PASS_REGULAR_EXPRESSION
                             "the screen showed [1-9][0-9][0-9][0-9][0-9]+ lit pixels.*\"code\":\"scene_drawing_summary\"[^\n]*\"frames\":[1-9][0-9]+,[^\n]*\"models\":[1-9][0-9][0-9][0-9][0-9][0-9]+,[^\n]*\"modelsLeftOut\":0,")
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
        # (D606). The runtime presents the controllers as the simple
        # controller, which it says (D680).
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
        # Reach on a headset (D657): the room drawn for both eyes from the
        # player's seat, the controllers its hands, and a flat bot seated
        # at the table beside it in one World (ADR-0081's cross-play). Its
        # sound stops until the session is first shown (D679).
        if(RAWFRAME_CLIENT_HEADSET AND RAWFRAME_MONADO_SERVICE AND NOT WIN32)
            # Reach's sources cooked, so the client hears it (D679) and the
            # server plays the same content.
            set(REACH_CONTENT ${CMAKE_CURRENT_BINARY_DIR}/reach_content)
            add_test(NAME reach_cooked
                     COMMAND rawframe_host_cook_tool ${PROJECT_SOURCE_DIR}/games/reach ${REACH_CONTENT}
                             ${CMAKE_CURRENT_BINARY_DIR}/reach_cook_cache)
            set_tests_properties(reach_cooked PROPERTIES
                                 FIXTURES_SETUP reach_content
                                 PASS_REGULAR_EXPRESSION "failed 0")
            file(WRITE ${CMAKE_CURRENT_BINARY_DIR}/reach_server.conf "content.root = ${REACH_CONTENT}\n")
            configure_file(tests/reach_headset.conf.in ${CMAKE_CURRENT_BINARY_DIR}/reach_headset.conf @ONLY)
            add_test(NAME client_reach_headset
                     COMMAND ${RAWFRAME_SHELL} ${PROJECT_SOURCE_DIR}/tools/xvfb_run.sh -s "-screen 0 1280x720x24 -br" ${RAWFRAME_SHELL}
                             ${PROJECT_SOURCE_DIR}/tools/xr_run.sh ${RAWFRAME_SHELL} ${CMAKE_CURRENT_SOURCE_DIR}/tests/drawn.sh
                             $<TARGET_FILE:rawframe_host_dedicated_server> $<TARGET_FILE:rawframe_host_client>
                             ${CMAKE_CURRENT_BINARY_DIR}/reach_headset.conf games/reach/reach.game
                             ${CMAKE_CURRENT_BINARY_DIR}/reach_server.conf 1
                     WORKING_DIRECTORY ${PROJECT_SOURCE_DIR})
            set_tests_properties(client_reach_headset PROPERTIES
                                 FIXTURES_REQUIRED reach_content
                                 PROCESSORS 2
                                 PASS_REGULAR_EXPRESSION
                                 "\"code\":\"output_suspended\".*\"code\":\"output_resumed\".*\"code\":\"playing_summary\"[^\n]*\"suspensions\":1,.*\"code\":\"scene_eyes_drawing_summary\"[^\n]*\"eyes\":2,\"frames\":[1-9][0-9]+}.*\"code\":\"bots_summary\"[^\n]*\"bots\":2,\"admitted\":2,.*\"code\":\"xr_summary\"[^\n]*\"framesSubmitted\":[1-9][0-9]+,[^\n]*\"framesHandLocated\":[1-9][0-9]+,\"controllerEvents\":[1-9][0-9]+,[^\n]*\"failed\":false}.*\"code\":\"input_summary\"[^\n]*\"controllerEvents\":[1-9][0-9]+")
            # Blocks taken and thrown from the headset end to end: Monado's
            # remote driver's Index controllers, the right one moved by
            # reach_hands.py, gripping one block with its trigger (D662)
            # and another with its tracked hand closed (D664), and no other
            # player, so the server's two welds made and let go are the
            # headset's; and its own hands drawn where it holds them
            # (D665), each frame where the headset has them then (D676);
            # the runtime presents them as Index controllers (D680). Played
            # for 960 frames: the hands keep wall-clock rounds, and a loaded
            # machine's client missed one of the two grips in 480 (D734).
            add_test(NAME client_reach_takes
                     COMMAND ${RAWFRAME_SHELL} ${PROJECT_SOURCE_DIR}/tools/xvfb_run.sh -s "-screen 0 1280x720x24 -br" ${RAWFRAME_SHELL}
                             ${PROJECT_SOURCE_DIR}/tools/xr_run.sh --hands ${CMAKE_CURRENT_SOURCE_DIR}/tests/reach_hands.py
                             ${RAWFRAME_SHELL} ${CMAKE_CURRENT_SOURCE_DIR}/tests/drawn.sh
                             $<TARGET_FILE:rawframe_host_dedicated_server> $<TARGET_FILE:rawframe_host_client>
                             ${CMAKE_CURRENT_BINARY_DIR}/reach_headset.conf games/reach/reach.game
                             ${CMAKE_CURRENT_BINARY_DIR}/reach_server.conf 0 960
                     WORKING_DIRECTORY ${PROJECT_SOURCE_DIR})
            set_tests_properties(client_reach_takes PROPERTIES
                                 FIXTURES_REQUIRED reach_content
                                 PROCESSORS 2
                                 PASS_REGULAR_EXPRESSION
                                 "\"domain\":\"physics3d\",\"code\":\"summary\"[^\n]*\"jointsMade\":([2-9]|[1-9][0-9]+),\"jointsRemoved\":([2-9]|[1-9][0-9]+),.*\"code\":\"xr_profiles\"[^\n]*\"right\":\"/interaction_profiles/valve/index_controller\".*\"code\":\"scene_placed_summary\"[^\n]*\"modelsPlaced\":[0-9]+,\"modelsTracked\":[1-9][0-9]*,\"modelsHidden\":0,.*\"code\":\"xr_summary\"[^\n]*\"framesHandTracked\":[1-9][0-9]*,[^\n]*\"failed\":false}")
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
    # nothing in (D493). A record a test waited for and never saw fails
    # it, rather than costing three minutes unseen (D627).
    get_property(window_tests DIRECTORY PROPERTY TESTS)
    list(FILTER window_tests INCLUDE REGEX "^client_")
    set_tests_properties(${window_tests} PROPERTIES LABELS window FAIL_REGULAR_EXPRESSION "never saw")
    # Gale's islands draw a material and its instance (D709): one unread
    # fails it.
    if(TEST client_gale_drawn)
        set_property(TEST client_gale_drawn APPEND PROPERTY FAIL_REGULAR_EXPRESSION "\"code\":\"material_unread\"")
    endif()
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
    # So is Reach's (D657).
    if(TEST client_reach_headset)
        set_tests_properties(client_reach_headset client_reach_takes PROPERTIES LABELS "window;headset")
    endif()
endif()
