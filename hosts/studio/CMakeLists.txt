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
                         "\"code\":\"ui_drawing_summary\"[^\n]*\"frames\":[1-9][0-9]+,\"boxes\":[1-9][0-9]+.*\"code\":\"studio_summary\"[^\n]*\"records\":2,\"scenes\":3,\"scene\":\"\",[^\n]*\"framesDrawn\":[1-9][0-9]+,\"boxes\":13,\"glyphs\":[1-9][0-9]+}.*\"code\":\"frame_summary\"[^\n]*\"framesShown\":[1-9][0-9]+,.*\"exit\":\"clean_stop\"")
    # Studio clicked: runners' level chosen shows its entities, read from
    # the session, and its ground chosen shows its four components and is
    # chosen in the session; its body's width typed as 12 and given with
    # Enter is set by the session, which writes the level; an x typed
    # first is refused before it reaches the session. Undo then Redo in
    # the header leave the edit standing.
    add_test(NAME studio_clicked
             COMMAND ${RAWFRAME_SHELL} ${PROJECT_SOURCE_DIR}/tools/xvfb_run.sh -s "-screen 0 1280x720x24 -br" ${RAWFRAME_SHELL}
                     ${CMAKE_CURRENT_SOURCE_DIR}/tests/clicked.sh $<TARGET_FILE:rawframe_host_studio_tool>
                     games/runners/runners.game level.scene 200,126 640,129 1100,225:x 1100,225:12 1166,23 1236,23
             WORKING_DIRECTORY ${PROJECT_SOURCE_DIR})
    set_tests_properties(studio_clicked PROPERTIES
                         SKIP_REGULAR_EXPRESSION "skip: no device"
                         PASS_REGULAR_EXPRESSION
                         "clicked at 200,126.*clicked at 640,129.*typed x at 1100,225.*typed 12 at 1100,225.*clicked at 1166,23.*clicked at 1236,23.*level.scene written: yes.*\"code\":\"studio_summary\"[^\n]*\"scene\":\"level.scene\",\"entities\":[1-9][0-9]*,\"entity\":\"c5400cf8-4b07-4d16-84c2-951852c66b40\",\"components\":4,\"applied\":1,\"refused\":1,\"undone\":1,\"redone\":1,\"undoable\":1,\"redoable\":0,\"viewsSet\":0,\"wheeled\":0,\"componentsScrolled\":0,\"previewing\":false,\"status\":\"redone\".*\"exit\":\"clean_stop\"")
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
                         "typed crate at 1100,97.*typed runners.tile at 1100,129.*clicked at 1236,129.*clicked at 517,97.*clicked at 1166,23.*level.scene written: yes.*\"code\":\"studio_summary\"[^\n]*\"scene\":\"level.scene\",\"entities\":10,\"entity\":\"\",\"components\":0,\"applied\":5,\"refused\":0,\"undone\":1,\"redone\":0,\"undoable\":4,\"redoable\":1,\"viewsSet\":0,\"wheeled\":0,\"componentsScrolled\":0,\"previewing\":false,\"status\":\"undone\".*\"exit\":\"clean_stop\"")
    # Studio scrolled (D441): runners' west slope has more rows than its
    # column holds; the wheel turned over them scrolls the column to its
    # end, and the tile's width, clicked where scrolling brought it, is the
    # field set.
    add_test(NAME studio_scrolls
             COMMAND ${RAWFRAME_SHELL} ${PROJECT_SOURCE_DIR}/tools/xvfb_run.sh -s "-screen 0 1280x720x24 -br" ${RAWFRAME_SHELL}
                     ${CMAKE_CURRENT_SOURCE_DIR}/tests/clicked.sh $<TARGET_FILE:rawframe_host_studio_tool>
                     games/runners/runners.game level.scene 200,126 640,225 wheel=1000,400,3 1100,661:9
             WORKING_DIRECTORY ${PROJECT_SOURCE_DIR})
    set_tests_properties(studio_scrolls PROPERTIES
                         SKIP_REGULAR_EXPRESSION "skip: no device"
                         PASS_REGULAR_EXPRESSION
                         "turned the wheel 3 at 1000,400.*typed 9 at 1100,661.*level.scene written: yes.*\"code\":\"studio_summary\"[^\n]*\"components\":4,\"applied\":1,\"refused\":0,[^\n]*\"wheeled\":3,\"componentsScrolled\":4[0-9](\\.[0-9]+)?,[^\n]*\"status\":\"width set to 9\".*\"exit\":\"clean_stop\"")
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
                         ${PROJECT_BINARY_DIR}/hosts/client/plaza_server.conf 200,97 300,161:0,30,0.5
                 WORKING_DIRECTORY ${PROJECT_SOURCE_DIR})
        set_tests_properties(studio_previews_plaza PROPERTIES
                             FIXTURES_REQUIRED plaza_content
                             SKIP_REGULAR_EXPRESSION "skip: no device"
                             PASS_REGULAR_EXPRESSION
                             "clicked at 200,97.*typed 0,30,0.5 at 300,161.*\"code\":\"studio_summary\"[^\n]*\"scene\":\"gate.scene\",[^\n]*\"viewsSet\":1,\"wheeled\":0,\"componentsScrolled\":0,\"previewing\":true,\"status\":\"view set and previewed\".*\"code\":\"scene_summary\"[^\n]*\"framesPreviewed\":[1-9][0-9]*}")
    endif()
endif()
