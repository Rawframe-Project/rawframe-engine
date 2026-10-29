# Studio, a window onto an authoring session. Only where there are windows
# and a device to draw them on; no client or server closure contains it.
if(NOT TARGET rawframe_input_window OR NOT TARGET rawframe_render_canvas_gpu)
    return()
endif()

rawframe_host(NAME studio_tool OUTPUT rawframe-studio
    SOURCES src/main.cpp
    DEPS studio window_host render render_canvas_gpu host window composition result base
)
# Its icon in the program file itself on Windows, and a program of windows,
# not of the console: opened from Explorer it shows no console beside it,
# its main still its entry. A parent that hands it files for its output, as
# the tests do, still has them written.
if(WIN32)
    target_sources(rawframe_host_studio_tool PRIVATE resources/rawframe-studio.rc)
    set_target_properties(rawframe_host_studio_tool PROPERTIES WIN32_EXECUTABLE TRUE)
    target_link_options(rawframe_host_studio_tool PRIVATE "LINKER:/ENTRY:mainCRTStartup")
endif()

# Five end-to-end scenarios that drive Studio's window under an X server of
# their own. Studio's behavior is otherwise tested through the authoring
# session it speaks (hosts/author/tests, modules/authoring_session/tests);
# its look is not tested.
find_program(RAWFRAME_XVFB Xvfb)
if(RAWFRAME_BUILD_TESTS AND RAWFRAME_XVFB)
    # A new game: Studio with no game opens on its start screen, a game is
    # made from the starter template under the name typed, and it opens and
    # is kept first among the games opened last.
    add_test(NAME studio_starts
             COMMAND ${CMAKE_COMMAND} -E env "STUDIO_SETTINGS=studio.preferences = {work}/studio.preferences"
                     ${RAWFRAME_SHELL} ${PROJECT_SOURCE_DIR}/tools/xvfb_run.sh -s "-screen 0 1280x720x24 -br" ${RAWFRAME_SHELL}
                     ${CMAKE_CURRENT_SOURCE_DIR}/tests/clicked.sh $<TARGET_FILE:rawframe_host_studio_tool>
                     none none @starter-3d "@new game:mygame" "until=studio_said,status=\"opened mygame.game" wait=1
             WORKING_DIRECTORY ${PROJECT_SOURCE_DIR})
    set_tests_properties(studio_starts PROPERTIES
                         SKIP_REGULAR_EXPRESSION "skip: no device"
                         PASS_REGULAR_EXPRESSION
                         "\"recent\": \\[[ \n]*\"[^\"]*/games/mygame/mygame.game\".*\"code\":\"studio_summary\"[^\n]*\"scenes\":[1-9],.*\"code\":\"studio_start\"[^\n]*\"made\":1,\"game\":\"mygame.game\",\"recent\":1}")
    # Editing, undo and saving: the ground renamed, undone with Control Z,
    # redone through the command palette, undone from the Edit menu, and
    # deleted with the Delete key, the scene written.
    add_test(NAME studio_commands
             COMMAND ${RAWFRAME_SHELL} ${PROJECT_SOURCE_DIR}/tools/xvfb_run.sh -s "-screen 0 1280x720x24 -br" ${RAWFRAME_SHELL}
                     ${CMAKE_CURRENT_SOURCE_DIR}/tests/clicked.sh $<TARGET_FILE:rawframe_host_studio_tool>
                     games/runners/runners.game level.scene @level.scene @ground "@name:first" "until=studio_said,status=\"renamed first\""
                     keys=Control_L+z "until=studio_said,status=\"undone\"" @commands:redo "until=studio_said,status=\"redone\""
                     @Edit "@Undo  Ctrl+Z" "until=studio_said,status=\"undone\"" keys=Delete "until=studio_said,status=\"entity deleted\""
             WORKING_DIRECTORY ${PROJECT_SOURCE_DIR})
    set_tests_properties(studio_commands PROPERTIES
                         SKIP_REGULAR_EXPRESSION "skip: no device"
                         PASS_REGULAR_EXPRESSION
                         "pressed Control_L\\+z.*found commands at.*typed redo at.*found Edit at.*found Undo  Ctrl\\+Z at.*pressed Delete.*level.scene written: yes.*\"code\":\"studio_summary\"[^\n]*\"applied\":2,\"refused\":0,\"undone\":2,\"redone\":1,[^\n]*\"status\":\"entity deleted\".*\"exit\":\"clean_stop\"")
    # Working together: Studio joined to a shared session's host hears
    # another collaborator's rename, made headless, shows it, and cooks on
    # the host.
    add_test(NAME studio_joins
             COMMAND ${RAWFRAME_SHELL} ${PROJECT_SOURCE_DIR}/tools/xvfb_run.sh -s "-screen 0 1280x720x24 -br" ${RAWFRAME_SHELL}
                     ${CMAKE_CURRENT_SOURCE_DIR}/tests/joined.sh $<TARGET_FILE:rawframe_host_studio_tool>
                     $<TARGET_FILE:rawframe_host_author> $<TARGET_FILE:rawframe_host_cook_tool>
             WORKING_DIRECTORY ${PROJECT_SOURCE_DIR})
    set_tests_properties(studio_joins PROPERTIES
                         SKIP_REGULAR_EXPRESSION "skip: no device"
                         PASS_REGULAR_EXPRESSION
                         "found Session at.*found bens ground at.*\"code\":\"studio_joined\"[^\n]*\"author\":\"ada\".*\"code\":\"studio_host_heard\"[^\n]*\"kind\":\"changed\",\"author\":\"ben\",\"document\":\"level.scene\".*\"code\":\"studio_summary\"[^\n]*\"status\":\"cooked [0-9]+ sources.*\"exit\":\"clean_stop\".*ben's rename kept.*the host's level holds it.*cooked on the host")
    if(TARGET rawframe_host_client AND TARGET rawframe_host_dedicated_server)
        # Playing in the viewport: Plaza's server and client started from Play,
        # the client's pictures streamed into Studio's viewport, and a click
        # there handed to the client, which picks the gate's post through the
        # server.
        add_test(NAME studio_plays_in_viewport
                 COMMAND ${CMAKE_COMMAND} -E env "STUDIO_SETTINGS=studio.play_in_viewport = true"
                         ${RAWFRAME_SHELL} ${PROJECT_SOURCE_DIR}/tools/xvfb_run.sh -s "-screen 0 1280x720x24 -br" ${RAWFRAME_SHELL}
                         ${CMAKE_CURRENT_SOURCE_DIR}/tests/plays.sh $<TARGET_FILE:rawframe_host_studio_tool>
                         $<TARGET_FILE:rawframe_host_dedicated_server> $<TARGET_FILE:rawframe_host_client>
                         games/plaza/plaza.game ${PROJECT_BINARY_DIR}/hosts/arena/plaza_server.conf
                         ${PROJECT_BINARY_DIR}/hosts/client/plaza_drawn.conf @gate.scene @Play until=studio_previewing
                         "@Scene settings" "@eye:-30,3,-20" "@target:-30,2.5,-30" wait=3 @Viewport "until=studio_said,status=\"picked "
                         @Stop until=studio_game_ended
                 WORKING_DIRECTORY ${PROJECT_SOURCE_DIR})
        set_tests_properties(studio_plays_in_viewport PROPERTIES
                             FIXTURES_REQUIRED plaza_content
                             SKIP_REGULAR_EXPRESSION "skip: no device"
                             PASS_REGULAR_EXPRESSION
                             "\"code\":\"studio_summary\"[^\n]*\"entity\":\"6c632219-53a9-4af4-8781-bb2a8f4fa53b\",[^\n]*\"picked\":1,.*\"code\":\"studio_viewport\"[^\n]*\"inViewport\":true,\"pictures\":[1-9][0-9]*,\"inputs\":[1-9][0-9]*,\"inputsTaken\":[1-9][0-9]*,\"width\":[1-9][0-9]+,\"height\":[1-9][0-9]+,.*\"code\":\"frame_summary\"[^\n]*\"window\":false,[^\n]*\"streamed\":[1-9][0-9]*,\"streamFailures\":0}")
        # Exporting: Runners exported for the desktop from the File menu's
        # dialog by rawframe-export, its folder holding the receipt; then a
        # phone's export refused while it has no address.
        add_test(NAME studio_exports
                 COMMAND ${CMAKE_COMMAND} -E env
                         "STUDIO_TOOLS=rawframe-export=$<TARGET_FILE:rawframe_host_export_tool> rawframe-cook=$<TARGET_FILE:rawframe_host_cook_tool> rawframe-build=$<TARGET_FILE:rawframe_host_build_tool> rawframe-server=$<TARGET_FILE:rawframe_host_dedicated_server> rawframe-client=$<TARGET_FILE:rawframe_host_client> rawframe-play=$<TARGET_FILE:rawframe_host_play_tool>"
                         "STUDIO_SETTINGS=studio.export = {work}/tools/rawframe-export"
                         ${RAWFRAME_SHELL} ${PROJECT_SOURCE_DIR}/tools/xvfb_run.sh -s "-screen 0 1280x720x24 -br" ${RAWFRAME_SHELL}
                         ${CMAKE_CURRENT_SOURCE_DIR}/tests/clicked.sh $<TARGET_FILE:rawframe_host_studio_tool>
                         games/runners/runners.game level.scene @level.scene @File @Export "until=studio_said,status=\"export\""
                         "@title:Runners exported" "until=studio_said,status=\"the export's title set"
                         @Export "until=studio_exported~240" @android "until=studio_said,status=\"exports for android"
                         @Export "until=studio_said,status=\"a phone's export needs"
                 WORKING_DIRECTORY ${PROJECT_SOURCE_DIR})
        set_tests_properties(studio_exports PROPERTIES
                             SKIP_REGULAR_EXPRESSION "skip: no device"
                             PASS_REGULAR_EXPRESSION
                             "\"code\":\"studio_exported\"[^\n]*\"target\":\"desktop\",[^\n]*\"code\":0,\"receipt\":true,\"exports\":1}.*\"status\":\"a phone's export needs.*\"exit\":\"clean_stop\"")
        # A played game is three programs, Studio, its server and a client
        # drawing on the device.
        set_tests_properties(studio_plays_in_viewport PROPERTIES PROCESSORS 3)
    endif()
    # A record a test waited for and never saw fails it at once.
    get_property(studio_tests DIRECTORY PROPERTY TESTS)
    set_tests_properties(${studio_tests} PROPERTIES LABELS integration FAIL_REGULAR_EXPRESSION "never saw")
    # https://github.com/Rawframe-Project/rawframe-engine/issues/70
    if(TEST studio_plays_in_viewport)
        set_property(TEST studio_plays_in_viewport APPEND PROPERTY LABELS quarantined)
    endif()
endif()
