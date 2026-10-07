# Studio (ADR-0032, D435): a window onto an authoring session. Only where
# there are windows and a device to draw them on; no client or server
# closure contains it.
if(NOT TARGET rawframe_input_window OR NOT TARGET rawframe_render_canvas_gpu)
    return()
endif()

rawframe_host(NAME studio_tool OUTPUT rawframe-studio
    SOURCES src/main.cpp
    DEPS studio window_host render render_canvas_gpu host window composition result base
)

find_program(RAWFRAME_XVFB Xvfb)
if(RAWFRAME_BUILD_TESTS AND RAWFRAME_XVFB)
    # Studio on runners: its session welcomed, its three scenes listed by
    # name, and its panels and words drawn in a real window.
    add_test(NAME studio_shown
             COMMAND ${RAWFRAME_SHELL} ${PROJECT_SOURCE_DIR}/tools/xvfb_run.sh -s "-screen 0 1280x720x24 -br" ${RAWFRAME_SHELL}
                     ${CMAKE_CURRENT_SOURCE_DIR}/tests/shown.sh $<TARGET_FILE:rawframe_host_studio_tool>
                     games/runners/runners.game
             WORKING_DIRECTORY ${PROJECT_SOURCE_DIR})
    set_tests_properties(studio_shown PROPERTIES
                         SKIP_REGULAR_EXPRESSION "skip: no device"
                         PASS_REGULAR_EXPRESSION
                         "\"code\":\"ui_drawing_summary\"[^\n]*\"frames\":[1-9][0-9]+,\"boxes\":[1-9][0-9]+.*\"code\":\"studio_summary\"[^\n]*\"records\":3,\"scenes\":3,\"scene\":\"\",[^\n]*\"framesDrawn\":[1-9][0-9]+,\"boxes\":20,\"glyphs\":[1-9][0-9]+}.*\"code\":\"frame_summary\"[^\n]*\"framesShown\":[1-9][0-9]+,.*\"exit\":\"clean_stop\"")
    # Studio clicked: runners' level chosen shows its entities, read from
    # the session, and its ground chosen shows its four components and is
    # chosen in the session; its body's width typed as 12 and given with
    # Enter is set by the session, which writes the level; an x typed
    # first is refused before it reaches the session. Undo then Redo in
    # the header leave the edit standing.
    add_test(NAME studio_clicked
             COMMAND ${RAWFRAME_SHELL} ${PROJECT_SOURCE_DIR}/tools/xvfb_run.sh -s "-screen 0 1280x720x24 -br" ${RAWFRAME_SHELL}
                     ${CMAKE_CURRENT_SOURCE_DIR}/tests/clicked.sh $<TARGET_FILE:rawframe_host_studio_tool>
                     games/runners/runners.game level.scene 200,126 640,129 1100,353:x 1100,353:12 1166,23 1236,23
             WORKING_DIRECTORY ${PROJECT_SOURCE_DIR})
    set_tests_properties(studio_clicked PROPERTIES
                         SKIP_REGULAR_EXPRESSION "skip: no device"
                         PASS_REGULAR_EXPRESSION
                         "clicked at 200,126.*clicked at 640,129.*typed x at 1100,353.*typed 12 at 1100,353.*clicked at 1166,23.*clicked at 1236,23.*level.scene written: yes.*\"code\":\"studio_summary\"[^\n]*\"scene\":\"level.scene\",\"entities\":[1-9][0-9]*,\"entity\":\"c5400cf8-4b07-4d16-84c2-951852c66b40\",\"components\":4,\"applied\":1,\"refused\":1,\"undone\":1,\"redone\":1,\"undoable\":1,\"redoable\":0,[^\n]*\"status\":\"redone\".*\"exit\":\"clean_stop\"")
    # Studio's operations, as describe offers them: in runners' level a new
    # entity is made and chosen, renamed crate, given runners.tile by its
    # name typed, the tile removed, and the entity deleted; Undo brings it
    # back, so the level is written with a crate in it.
    add_test(NAME studio_operates
             COMMAND ${RAWFRAME_SHELL} ${PROJECT_SOURCE_DIR}/tools/xvfb_run.sh -s "-screen 0 1280x720x24 -br" ${RAWFRAME_SHELL}
                     ${CMAKE_CURRENT_SOURCE_DIR}/tests/clicked.sh $<TARGET_FILE:rawframe_host_studio_tool>
                     games/runners/runners.game level.scene 200,126 463,97 1100,97:crate 1100,129:runners.tile
                     1236,129 517,97 1166,23
             WORKING_DIRECTORY ${PROJECT_SOURCE_DIR})
    set_tests_properties(studio_operates PROPERTIES
                         SKIP_REGULAR_EXPRESSION "skip: no device"
                         PASS_REGULAR_EXPRESSION
                         "typed crate at 1100,97.*typed runners.tile at 1100,129.*clicked at 1236,129.*clicked at 517,97.*clicked at 1166,23.*level.scene written: yes.*\"code\":\"studio_summary\"[^\n]*\"scene\":\"level.scene\",\"entities\":10,\"entity\":\"\",\"components\":0,\"applied\":5,\"refused\":0,\"undone\":1,\"redone\":0,\"undoable\":4,\"redoable\":1,[^\n]*\"status\":\"undone\".*\"exit\":\"clean_stop\"")
    # A reference set by the name of the entity it names (D447): a new
    # entity in runners' level is given attach, whose parent is typed:
    # west, which two entities' names start, is refused, and ground set;
    # the field then shows ground, and is set again by gr, and cleared.
    add_test(NAME studio_references
             COMMAND ${RAWFRAME_SHELL} ${PROJECT_SOURCE_DIR}/tools/xvfb_run.sh -s "-screen 0 1280x720x24 -br" ${RAWFRAME_SHELL}
                     ${CMAKE_CURRENT_SOURCE_DIR}/tests/clicked.sh $<TARGET_FILE:rawframe_host_studio_tool>
                     games/runners/runners.game level.scene 200,126 463,97 1100,129:attach 1100,161:west
                     1100,161:ground
                     1100,161:gr 1100,161 keys=BackSpace,Return
             WORKING_DIRECTORY ${PROJECT_SOURCE_DIR})
    set_tests_properties(studio_references PROPERTIES
                         SKIP_REGULAR_EXPRESSION "skip: no device"
                         PASS_REGULAR_EXPRESSION
                         "typed attach at 1100,129.*typed west at 1100,161.*typed ground at 1100,161.*typed gr at 1100,161.*clicked at 1100,161.*pressed BackSpace,Return.*level.scene written: yes.*\"code\":\"studio_summary\"[^\n]*\"components\":1,\"applied\":5,\"refused\":1,[^\n]*\"status\":\"parent cleared\".*\"exit\":\"clean_stop\"")
    # A scene's instance placed and removed (D448, D450): in runners' hall,
    # the instance field refuses x, which no scene is, and places platform;
    # the entity it brought chosen, Delete removes it through the instance's
    # patch, Restore takes it back, and Remove instance removes the whole
    # instance, so the hall is as it was, byte for byte.
    add_test(NAME studio_instances
             COMMAND ${RAWFRAME_SHELL} ${PROJECT_SOURCE_DIR}/tools/xvfb_run.sh -s "-screen 0 1280x720x24 -br" ${RAWFRAME_SHELL}
                     ${CMAKE_CURRENT_SOURCE_DIR}/tests/clicked.sh $<TARGET_FILE:rawframe_host_studio_tool>
                     games/runners/runners.game hall.scene 200,97 674,161:x 674,161:platform 640,161 517,97
                     589,97 690,97
             WORKING_DIRECTORY ${PROJECT_SOURCE_DIR})
    set_tests_properties(studio_instances PROPERTIES
                         SKIP_REGULAR_EXPRESSION "skip: no device"
                         PASS_REGULAR_EXPRESSION
                         "typed x at 674,161.*typed platform at 674,161.*clicked at 640,161.*clicked at 517,97.*clicked at 589,97.*clicked at 690,97.*hall.scene written: no.*\"code\":\"studio_summary\"[^\n]*\"scene\":\"hall.scene\",\"entities\":1,[^\n]*\"applied\":4,\"refused\":1,[^\n]*\"status\":\"instance removed\".*\"exit\":\"clean_stop\"")
    # A new scene (D449): typed under runners' scenes, made with its
    # sidecar, listed in order and chosen, and given an entity.
    add_test(NAME studio_makes_scenes
             COMMAND ${RAWFRAME_SHELL} ${PROJECT_SOURCE_DIR}/tools/xvfb_run.sh -s "-screen 0 1280x720x24 -br" ${RAWFRAME_SHELL}
                     ${CMAKE_CURRENT_SOURCE_DIR}/tests/clicked.sh $<TARGET_FILE:rawframe_host_studio_tool>
                     games/runners/runners.game crate.scene 248,193:crate 463,97
             WORKING_DIRECTORY ${PROJECT_SOURCE_DIR})
    set_tests_properties(studio_makes_scenes PROPERTIES
                         SKIP_REGULAR_EXPRESSION "skip: no device"
                         PASS_REGULAR_EXPRESSION
                         "typed crate at 248,193.*clicked at 463,97.*\"code\":\"studio_summary\"[^\n]*\"scenes\":4,\"scene\":\"crate.scene\",\"entities\":1,[^\n]*\"applied\":2,\"refused\":0,[^\n]*\"status\":\"entity created\".*\"exit\":\"clean_stop\"")
    # An entity moved among the scene's own (D451): ground, first in
    # runners' level, is refused Up and moved Down twice, staying chosen.
    add_test(NAME studio_moves
             COMMAND ${RAWFRAME_SHELL} ${PROJECT_SOURCE_DIR}/tools/xvfb_run.sh -s "-screen 0 1280x720x24 -br" ${RAWFRAME_SHELL}
                     ${CMAKE_CURRENT_SOURCE_DIR}/tests/clicked.sh $<TARGET_FILE:rawframe_host_studio_tool>
                     games/runners/runners.game level.scene 200,126 640,129 756,97 800,97 800,97
             WORKING_DIRECTORY ${PROJECT_SOURCE_DIR})
    set_tests_properties(studio_moves PROPERTIES
                         SKIP_REGULAR_EXPRESSION "skip: no device"
                         PASS_REGULAR_EXPRESSION
                         "clicked at 756,97.*clicked at 800,97.*clicked at 800,97.*level.scene written: yes.*\"code\":\"studio_summary\"[^\n]*\"entity\":\"c5400cf8-4b07-4d16-84c2-951852c66b40\",[^\n]*\"applied\":2,\"refused\":1,[^\n]*\"status\":\"moved down\".*\"exit\":\"clean_stop\"")
    # An entity copied whole (D458): ground, chosen, is duplicated by the
    # Entities heading's button into "ground copy", its every component
    # and value written again (its tile's 64-bit texture among them), in
    # one transaction, the copy chosen.
    add_test(NAME studio_duplicates
             COMMAND ${RAWFRAME_SHELL} ${PROJECT_SOURCE_DIR}/tools/xvfb_run.sh -s "-screen 0 1280x720x24 -br" ${RAWFRAME_SHELL}
                     ${CMAKE_CURRENT_SOURCE_DIR}/tests/clicked.sh $<TARGET_FILE:rawframe_host_studio_tool>
                     games/runners/runners.game level.scene 200,126 640,129 800,65
             WORKING_DIRECTORY ${PROJECT_SOURCE_DIR})
    set_tests_properties(studio_duplicates PROPERTIES
                         ENVIRONMENT "STUDIO_SHOW_WRITTEN=1"
                         SKIP_REGULAR_EXPRESSION "skip: no device"
                         PASS_REGULAR_EXPRESSION
                         "clicked at 800,65.*level.scene written: yes.*\"texture\": 11839623990059228360.*\"name\": \"ground copy\",[ \n]*\"components\": {[ \n]*\"rawframe.physics2d.body\": {[ \n]*\"height\": 0.5,[ \n]*\"shape\": 1,[ \n]*\"width\": 40[ \n]*},[ \n]*\"rawframe.physics2d.pose\": {},[ \n]*\"rawframe.physics2d.velocity\": {},[ \n]*\"runners.tile\": {[ \n]*\"color\": 4294967295,[^}]*\"texture\": 11839623990059228360,.*\"code\":\"studio_summary\"[^\n]*\"components\":4,[^\n]*\"applied\":1,\"refused\":0,[^\n]*\"undoable\":1,[^\n]*\"status\":\"ground copy made\"")
    # A component authored against another layout updated (D452): the
    # copy of runners' level records an older mark for the physics body,
    # so ground's body heading offers Update, which carries it over and
    # writes the level back as it was, byte for byte.
    add_test(NAME studio_updates
             COMMAND ${RAWFRAME_SHELL} ${PROJECT_SOURCE_DIR}/tools/xvfb_run.sh -s "-screen 0 1280x720x24 -br" ${RAWFRAME_SHELL}
                     ${CMAKE_CURRENT_SOURCE_DIR}/tests/clicked.sh $<TARGET_FILE:rawframe_host_studio_tool>
                     games/runners/runners.game level.scene 200,126 640,129 1178,129
             WORKING_DIRECTORY ${PROJECT_SOURCE_DIR})
    set_tests_properties(studio_updates PROPERTIES
                         ENVIRONMENT "STUDIO_SCENE_EDIT=s/\"rawframe.physics2d.body\": \"593035e6aa3af5e7\"/\"rawframe.physics2d.body\": \"0000000000000001\"/"
                         SKIP_REGULAR_EXPRESSION "skip: no device"
                         PASS_REGULAR_EXPRESSION
                         "clicked at 1178,129.*level.scene written: no.*\"code\":\"studio_summary\"[^\n]*\"applied\":1,\"refused\":0,[^\n]*\"status\":\"rawframe.physics2d.body updated\".*\"exit\":\"clean_stop\"")
    # A game whose program does not compile (D453): Studio shows the
    # diagnostic rather than ending, opens its file at its line in the
    # editor studio.editor names (here one that fixes it, as the author
    # would), and Retry reads the game again, now whole.
    add_test(NAME studio_reads_again
             COMMAND ${RAWFRAME_SHELL} ${PROJECT_SOURCE_DIR}/tools/xvfb_run.sh -s "-screen 0 1280x720x24 -br" ${RAWFRAME_SHELL}
                     ${CMAKE_CURRENT_SOURCE_DIR}/tests/clicked.sh $<TARGET_FILE:rawframe_host_studio_tool>
                     games/runners/runners.game controls.kest 76,164 160,164
             WORKING_DIRECTORY ${PROJECT_SOURCE_DIR})
    set_tests_properties(studio_reads_again PROPERTIES
                         ENVIRONMENT "STUDIO_SCENE_EDIT=$a this is not kest (;STUDIO_SETTINGS=studio.editor = ${CMAKE_CURRENT_SOURCE_DIR}/tests/fixing_editor.sh --goto {file}:{line}:{column}"
                         SKIP_REGULAR_EXPRESSION "skip: no device"
                         PASS_REGULAR_EXPRESSION
                         "clicked at 76,164.*clicked at 160,164.*controls.kest written: no.*editor opened --goto [^ ]*/game/controls.kest:[0-9]+:1.*\"code\":\"studio_summary\"[^\n]*\"scenes\":3,[^\n]*\"opened\":1,[^\n]*\"status\":\"the game reads\".*\"exit\":\"clean_stop\"")
    # A scene's history (D454): ground renamed three times, the history
    # lists the three; its first entry clicked undoes the two after it,
    # and its last clicked redoes them.
    add_test(NAME studio_steps_history
             COMMAND ${RAWFRAME_SHELL} ${PROJECT_SOURCE_DIR}/tools/xvfb_run.sh -s "-screen 0 1280x720x24 -br" ${RAWFRAME_SHELL}
                     ${CMAKE_CURRENT_SOURCE_DIR}/tests/clicked.sh $<TARGET_FILE:rawframe_host_studio_tool>
                     games/runners/runners.game level.scene 200,129 640,129 1100,97:first 1100,97:second
                     1100,97:third 200,385 200,449
             WORKING_DIRECTORY ${PROJECT_SOURCE_DIR})
    set_tests_properties(studio_steps_history PROPERTIES
                         SKIP_REGULAR_EXPRESSION "skip: no device"
                         PASS_REGULAR_EXPRESSION
                         "typed third at 1100,97.*clicked at 200,385.*clicked at 200,449.*level.scene written: yes.*\"code\":\"studio_summary\"[^\n]*\"applied\":3,\"refused\":0,\"undone\":2,\"redone\":2,\"undoable\":3,\"redoable\":0,[^\n]*\"status\":\"redone\".*\"exit\":\"clean_stop\"")
    # An asset typed by name (D455): ground's tile texture, scrolled to, is
    # given hud.png, which the game declares, by its name; hud alone, which
    # several assets' names start with, is refused.
    add_test(NAME studio_names_assets
             COMMAND ${RAWFRAME_SHELL} ${PROJECT_SOURCE_DIR}/tools/xvfb_run.sh -s "-screen 0 1280x720x24 -br" ${RAWFRAME_SHELL}
                     ${CMAKE_CURRENT_SOURCE_DIR}/tests/clicked.sh $<TARGET_FILE:rawframe_host_studio_tool>
                     games/runners/runners.game level.scene 200,129 640,129 wheel=1100,400,6 1100,297:hud.png
                     1100,297:hud
             WORKING_DIRECTORY ${PROJECT_SOURCE_DIR})
    set_tests_properties(studio_names_assets PROPERTIES
                         SKIP_REGULAR_EXPRESSION "skip: no device"
                         PASS_REGULAR_EXPRESSION
                         "typed hud.png at 1100,297.*typed hud at 1100,297.*level.scene written: yes.*\"code\":\"studio_summary\"[^\n]*\"applied\":1,\"refused\":1,[^\n]*\"status\":\"texture: [0-9]+ assets match hud\".*\"exit\":\"clean_stop\"")
    # Studio scrolled (D441, D442): runners' west slope has more rows than
    # its column holds; six detents of the wheel over them scroll the
    # column 600 pixels, and the tile's width, clicked where scrolling
    # brought it, is the field set.
    add_test(NAME studio_scrolls
             COMMAND ${RAWFRAME_SHELL} ${PROJECT_SOURCE_DIR}/tools/xvfb_run.sh -s "-screen 0 1280x720x24 -br" ${RAWFRAME_SHELL}
                     ${CMAKE_CURRENT_SOURCE_DIR}/tests/clicked.sh $<TARGET_FILE:rawframe_host_studio_tool>
                     games/runners/runners.game level.scene 200,126 640,225 wheel=1000,400,6 1100,457:9
             WORKING_DIRECTORY ${PROJECT_SOURCE_DIR})
    set_tests_properties(studio_scrolls PROPERTIES
                         SKIP_REGULAR_EXPRESSION "skip: no device"
                         PASS_REGULAR_EXPRESSION
                         "turned the wheel 6 at 1000,400.*typed 9 at 1100,457.*level.scene written: yes.*\"code\":\"studio_summary\"[^\n]*\"components\":4,\"applied\":1,\"refused\":0,[^\n]*\"wheeled\":6,\"componentsScrolled\":600,[^\n]*\"status\":\"width set to 9\".*\"exit\":\"clean_stop\"")
    # Studio reverting (D443): runners' level brings a platform whose pose
    # its instance sets; the instance's x reverted drops its own value for
    # its source's, written to the level.
    add_test(NAME studio_reverts
             COMMAND ${RAWFRAME_SHELL} ${PROJECT_SOURCE_DIR}/tools/xvfb_run.sh -s "-screen 0 1280x720x24 -br" ${RAWFRAME_SHELL}
                     ${CMAKE_CURRENT_SOURCE_DIR}/tests/clicked.sh $<TARGET_FILE:rawframe_host_studio_tool>
                     games/runners/runners.game level.scene 200,126 640,321 1240,129
             WORKING_DIRECTORY ${PROJECT_SOURCE_DIR})
    set_tests_properties(studio_reverts PROPERTIES
                         SKIP_REGULAR_EXPRESSION "skip: no device"
                         PASS_REGULAR_EXPRESSION
                         "clicked at 1240,129.*level.scene written: yes.*\"code\":\"studio_summary\"[^\n]*\"entity\":\"b4560e55-e4e2-467a-ab08-e91b7119397f\",\"components\":1,\"applied\":1,\"refused\":0,[^\n]*\"status\":\"x reverted\".*\"exit\":\"clean_stop\"")
    # Studio previewing the plaza (D433, D440): choosing its gate attaches
    # the playing client's tooling endpoint as the scene's preview, and an
    # eye typed is handed to it, so the client draws frames through the
    # preview; Studio ending its session gives the camera back.
    if(TARGET rawframe_host_client AND TARGET rawframe_host_dedicated_server)
        add_test(NAME studio_previews_plaza
                 COMMAND ${RAWFRAME_SHELL} ${PROJECT_SOURCE_DIR}/tools/xvfb_run.sh -s "-screen 0 1280x720x24 -br" ${RAWFRAME_SHELL}
                         ${CMAKE_CURRENT_SOURCE_DIR}/tests/previewing.sh $<TARGET_FILE:rawframe_host_dedicated_server>
                         $<TARGET_FILE:rawframe_host_client> $<TARGET_FILE:rawframe_host_studio_tool>
                         ${PROJECT_BINARY_DIR}/hosts/client/plaza_drawn.conf games/plaza/plaza.game
                         ${PROJECT_BINARY_DIR}/hosts/client/plaza_server.conf 200,97 until=studio_previewing
                         300,193:0,30,0.5
                 WORKING_DIRECTORY ${PROJECT_SOURCE_DIR})
        set_tests_properties(studio_previews_plaza PROPERTIES
                             FIXTURES_REQUIRED plaza_content
                             SKIP_REGULAR_EXPRESSION "skip: no device"
                             PASS_REGULAR_EXPRESSION
                             "clicked at 200,97.*saw studio_previewing.*typed 0,30,0.5 at 300,193.*\"code\":\"studio_summary\"[^\n]*\"scene\":\"gate.scene\",[^\n]*\"viewsSet\":1,[^\n]*\"previewing\":true,[^\n]*\"status\":\"view set and previewed\".*\"code\":\"scene_summary\"[^\n]*\"framesPreviewed\":[1-9][0-9]*,")
        # Studio playing the plaza itself (D445): Play starts a server and a
        # client with a tooling endpoint; once the client says who it is,
        # the chosen gate's preview is attached to it (the clicks wait for
        # Studio to say so, however loaded the machine), an eye typed is
        # handed to it; an entity made in the gate while it plays is taken
        # by the played server's World from the written scene (D446); and
        # Stop lets it go and stops both, the client having drawn frames
        # through the preview.
        add_test(NAME studio_plays_plaza
                 COMMAND ${RAWFRAME_SHELL} ${PROJECT_SOURCE_DIR}/tools/xvfb_run.sh -s "-screen 0 1280x720x24 -br" ${RAWFRAME_SHELL}
                         ${CMAKE_CURRENT_SOURCE_DIR}/tests/plays.sh $<TARGET_FILE:rawframe_host_studio_tool>
                         $<TARGET_FILE:rawframe_host_dedicated_server> $<TARGET_FILE:rawframe_host_client>
                         games/plaza/plaza.game ${PROJECT_BINARY_DIR}/hosts/client/plaza_server.conf
                         ${PROJECT_BINARY_DIR}/hosts/client/plaza_drawn.conf 200,97 1096,23 until=studio_previewing 300,193:0,30,0.5
                         wait=3 463,97 wait=5 1096,23
                 WORKING_DIRECTORY ${PROJECT_SOURCE_DIR})
        set_tests_properties(studio_plays_plaza PROPERTIES
                             FIXTURES_REQUIRED plaza_content
                             SKIP_REGULAR_EXPRESSION "skip: no device"
                             PASS_REGULAR_EXPRESSION
                             "clicked at 1096,23.*saw studio_previewing.*typed 0,30,0.5 at 300,193.*clicked at 1096,23.*\"code\":\"studio_summary\"[^\n]*\"applied\":1,[^\n]*\"viewsSet\":1,[^\n]*\"previewing\":false,\"played\":1,\"playing\":false,[^\n]*\"status\":\"game stopped\".*\"code\":\"scene_summary\"[^\n]*\"framesPreviewed\":[1-9][0-9]*,.*\"code\":\"scene_followed\"[^\n]*\"scene\":\"gate.scene\",\"created\":1.*{\"endpoint\":\"127.0.0.1:[0-9]+\",\"pinFile\":\"[^\"]*/server.fingerprint\",\"tokenFile\":\"[^\"]*/token\"}")
        # What the author clicks in the played game, selected in Studio
        # (D456): the gate's view aimed at its post, a click in the middle
        # of the played client's window (placed at 700,340, 560 by 360) is
        # a ray the client keeps; the server says the ray meets the post,
        # which the gate scene brought, and Studio chooses it, marked in the
        # played client where it stands (D464).
        add_test(NAME studio_picks_plaza
                 COMMAND ${RAWFRAME_SHELL} ${PROJECT_SOURCE_DIR}/tools/xvfb_run.sh -s "-screen 0 1280x720x24 -br" ${RAWFRAME_SHELL}
                         ${CMAKE_CURRENT_SOURCE_DIR}/tests/plays.sh $<TARGET_FILE:rawframe_host_studio_tool>
                         $<TARGET_FILE:rawframe_host_dedicated_server> $<TARGET_FILE:rawframe_host_client>
                         games/plaza/plaza.game ${PROJECT_BINARY_DIR}/hosts/client/plaza_server.conf
                         ${PROJECT_BINARY_DIR}/hosts/client/plaza_drawn.conf 200,97 1096,23 until=studio_previewing
                         300,193:-30,3,-20 300,225:-30,2.5,-30 wait=3 980,520 wait=3 1096,23
                 WORKING_DIRECTORY ${PROJECT_SOURCE_DIR})
        set_tests_properties(studio_picks_plaza PROPERTIES
                             FIXTURES_REQUIRED plaza_content
                             SKIP_REGULAR_EXPRESSION "skip: no device"
                             PASS_REGULAR_EXPRESSION
                             "clicked at 980,520.*\"code\":\"studio_summary\"[^\n]*\"entity\":\"6c632219-53a9-4af4-8781-bb2a8f4fa53b\",[^\n]*\"picked\":1,[^\n]*\"marked\":[1-9][0-9]*,.*\"code\":\"scene_summary\"[^\n]*\"marksShown\":[1-9]")
        # A drag in the played game moving what it pressed (D457): the
        # gate's post, aimed at, pressed in the middle of the played client's
        # window and let go a hundred pixels to its right, is chosen, and its
        # pose moved across the level plane in one transaction, one history
        # entry.
        add_test(NAME studio_drags_plaza
                 COMMAND ${RAWFRAME_SHELL} ${PROJECT_SOURCE_DIR}/tools/xvfb_run.sh -s "-screen 0 1280x720x24 -br" ${RAWFRAME_SHELL}
                         ${CMAKE_CURRENT_SOURCE_DIR}/tests/plays.sh $<TARGET_FILE:rawframe_host_studio_tool>
                         $<TARGET_FILE:rawframe_host_dedicated_server> $<TARGET_FILE:rawframe_host_client>
                         games/plaza/plaza.game ${PROJECT_BINARY_DIR}/hosts/client/plaza_server.conf
                         ${PROJECT_BINARY_DIR}/hosts/client/plaza_drawn.conf 200,97 1096,23 until=studio_previewing
                         300,193:-30,3,-20 300,225:-30,2.5,-30 wait=3 drag=980,520,1080,520 wait=3 1096,23
                 WORKING_DIRECTORY ${PROJECT_SOURCE_DIR})
        set_tests_properties(studio_drags_plaza PROPERTIES
                             FIXTURES_REQUIRED plaza_content
                             SKIP_REGULAR_EXPRESSION "skip: no device"
                             PASS_REGULAR_EXPRESSION
                             "dragged from 980,520 to 1080,520.*\"code\":\"studio_summary\"[^\n]*\"entity\":\"6c632219-53a9-4af4-8781-bb2a8f4fa53b\",[^\n]*\"applied\":1,[^\n]*\"undoable\":1,[^\n]*\"picked\":1,\"dragged\":1,")
        # The post's X handle dragged (D466): the post chosen and marked, a
        # press on its mark's X axis right of it, off the post itself, let go
        # further right and lower, moves the post along X alone, as far as
        # the release points along the axis, about 2.6 meters.
        add_test(NAME studio_handles_plaza
                 COMMAND ${RAWFRAME_SHELL} ${PROJECT_SOURCE_DIR}/tools/xvfb_run.sh -s "-screen 0 1280x720x24 -br" ${RAWFRAME_SHELL}
                         ${CMAKE_CURRENT_SOURCE_DIR}/tests/plays.sh $<TARGET_FILE:rawframe_host_studio_tool>
                         $<TARGET_FILE:rawframe_host_dedicated_server> $<TARGET_FILE:rawframe_host_client>
                         games/plaza/plaza.game ${PROJECT_BINARY_DIR}/hosts/client/plaza_server.conf
                         ${PROJECT_BINARY_DIR}/hosts/client/plaza_drawn.conf 200,97 1096,23 until=studio_previewing
                         300,193:-30,3,-20 300,225:-30,2.5,-30 wait=3 980,520 wait=3 drag=999,520,1079,540 wait=3 1096,23
                 WORKING_DIRECTORY ${PROJECT_SOURCE_DIR})
        set_tests_properties(studio_handles_plaza PROPERTIES
                             FIXTURES_REQUIRED plaza_content
                             SKIP_REGULAR_EXPRESSION "skip: no device"
                             PASS_REGULAR_EXPRESSION
                             "dragged from 999,520 to 1079,540.*\"code\":\"studio_summary\"[^\n]*\"applied\":1,[^\n]*\"dragged\":1,.*scene gate.scene: [^\n]*\"id\":\"6c632219-53a9-4af4-8781-bb2a8f4fa53b\",[^\n]*\"rawframe.physics3d.pose\":{\"x\":-27\\.[2-6][0-9]*,\"y\":2\\.5,\"z\":-30}")
        # The post raised with Shift and turned with Control held through
        # the drag (D463): up the upright plane facing the eye, and about
        # the post's own place, each one transaction.
        add_test(NAME studio_raises_plaza
                 COMMAND ${RAWFRAME_SHELL} ${PROJECT_SOURCE_DIR}/tools/xvfb_run.sh -s "-screen 0 1280x720x24 -br" ${RAWFRAME_SHELL}
                         ${CMAKE_CURRENT_SOURCE_DIR}/tests/plays.sh $<TARGET_FILE:rawframe_host_studio_tool>
                         $<TARGET_FILE:rawframe_host_dedicated_server> $<TARGET_FILE:rawframe_host_client>
                         games/plaza/plaza.game ${PROJECT_BINARY_DIR}/hosts/client/plaza_server.conf
                         ${PROJECT_BINARY_DIR}/hosts/client/plaza_drawn.conf 200,97 1096,23 until=studio_previewing
                         300,193:-30,3,-20 300,225:-30,2.5,-30 wait=3 drag=980,520,980,470,Shift_L wait=3 1096,23
                 WORKING_DIRECTORY ${PROJECT_SOURCE_DIR})
        set_tests_properties(studio_raises_plaza PROPERTIES
                             FIXTURES_REQUIRED plaza_content
                             SKIP_REGULAR_EXPRESSION "skip: no device"
                             PASS_REGULAR_EXPRESSION
                             "dragged from 980,520 to 980,470 holding Shift_L.*\"code\":\"studio_summary\"[^\n]*\"entity\":\"6c632219-53a9-4af4-8781-bb2a8f4fa53b\",[^\n]*\"applied\":1,[^\n]*\"undoable\":1,[^\n]*\"picked\":1,\"dragged\":1,\"raised\":1,\"turned\":0,")
        add_test(NAME studio_turns_plaza
                 COMMAND ${RAWFRAME_SHELL} ${PROJECT_SOURCE_DIR}/tools/xvfb_run.sh -s "-screen 0 1280x720x24 -br" ${RAWFRAME_SHELL}
                         ${CMAKE_CURRENT_SOURCE_DIR}/tests/plays.sh $<TARGET_FILE:rawframe_host_studio_tool>
                         $<TARGET_FILE:rawframe_host_dedicated_server> $<TARGET_FILE:rawframe_host_client>
                         games/plaza/plaza.game ${PROJECT_BINARY_DIR}/hosts/client/plaza_server.conf
                         ${PROJECT_BINARY_DIR}/hosts/client/plaza_drawn.conf 200,97 1096,23 until=studio_previewing
                         300,193:-30,3,-20 300,225:-30,2.5,-30 wait=3 drag=980,520,1080,520,Control_L wait=3 1096,23
                 WORKING_DIRECTORY ${PROJECT_SOURCE_DIR})
        set_tests_properties(studio_turns_plaza PROPERTIES
                             FIXTURES_REQUIRED plaza_content
                             SKIP_REGULAR_EXPRESSION "skip: no device"
                             PASS_REGULAR_EXPRESSION
                             "dragged from 980,520 to 1080,520 holding Control_L.*\"code\":\"studio_summary\"[^\n]*\"entity\":\"6c632219-53a9-4af4-8781-bb2a8f4fa53b\",[^\n]*\"applied\":1,[^\n]*\"undoable\":1,[^\n]*\"picked\":1,\"dragged\":1,\"raised\":0,\"turned\":1,")
    endif()
endif()
