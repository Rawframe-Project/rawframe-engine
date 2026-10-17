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
    # name, and its panels and words drawn in a real window, a frame as they
    # change and a few a second while they do not (D494); Move to among
    # its buttons (D498).
    add_test(NAME studio_shown
             COMMAND ${RAWFRAME_SHELL} ${PROJECT_SOURCE_DIR}/tools/xvfb_run.sh -s "-screen 0 1280x720x24 -br" ${RAWFRAME_SHELL}
                     ${CMAKE_CURRENT_SOURCE_DIR}/tests/shown.sh $<TARGET_FILE:rawframe_host_studio_tool>
                     games/runners/runners.game
             WORKING_DIRECTORY ${PROJECT_SOURCE_DIR})
    set_tests_properties(studio_shown PROPERTIES
                         SKIP_REGULAR_EXPRESSION "skip: no device"
                         PASS_REGULAR_EXPRESSION
                         "\"code\":\"ui_drawing_summary\"[^\n]*\"frames\":[1-9][0-9]*,\"framesOnPanel\":0,\"boxes\":[1-9][0-9]+.*\"code\":\"studio_summary\"[^\n]*\"records\":3,\"scenes\":14,\"scene\":\"\",[^\n]*\"framesDrawn\":[1-9][0-9]+,\"boxes\":75,\"glyphs\":[1-9][0-9]+}.*\"code\":\"frame_summary\"[^\n]*\"framesShown\":[1-9][0-9]*,[^\n]*\"framesUnchanged\":[1-9][0-9]*,.*\"exit\":\"clean_stop\"")
    # Studio read by a screen reader (D575): its window's tree over AT-SPI
    # holds its menus and the palette's field (D777), its buttons, its
    # docks as scroll panes named by their headings or tabs in SPEC-0051's
    # order (D776), and its fields as entries named by theirs; runners' level
    # and then New pressed through AT-SPI as a screen reader presses them
    # make an entity there, as clicks do.
    add_test(NAME studio_read_aloud
             COMMAND ${RAWFRAME_SHELL} ${PROJECT_SOURCE_DIR}/tools/xvfb_run.sh -s "-screen 0 1280x720x24 -br" ${RAWFRAME_SHELL}
                     ${CMAKE_CURRENT_SOURCE_DIR}/tests/read_aloud.sh $<TARGET_FILE:rawframe_host_studio_tool>
                     games/runners/runners.game level.scene level.scene New "until=entity created"
             WORKING_DIRECTORY ${PROJECT_SOURCE_DIR})
    set_tests_properties(studio_read_aloud PROPERTIES
                         SKIP_REGULAR_EXPRESSION "skip: no"
                         PASS_REGULAR_EXPRESSION
                         "a11y: 1 frame Rawframe Studio\na11y: 2 label Rawframe Studio  runners.game\na11y: 2 push button Edit\na11y: 2 push button Entity\n.*a11y: 2 entry commands\na11y: 2 push button Undo\n.*a11y: 2 scroll pane Hierarchy\n.*a11y: 3 push button New\n.*a11y: 2 scroll pane Documents\na11y: 2 panel Dock edge\na11y: 2 push button Assets\na11y: 2 push button History\na11y: 2 push button Console\na11y: 2 push button Network\na11y: 2 push button Environment\na11y: 2 push button Profiler\na11y: 2 push button Frame\na11y: 2 scroll pane Assets\n.*a11y: 3 entry new scene or import\n.*a11y: 3 push button level.scene\n.*a11y: 2 panel Inspector edge\na11y: 2 scroll pane Inspector\n.*a11y: pressed level.scene: b true\na11y: pressed New: b true\nlevel.scene written: yes.*\"code\":\"studio_accessibility_ready\".*\"code\":\"studio_summary\"[^\n]*\"scene\":\"level.scene\",[^\n]*\"applied\":1,\"refused\":0,[^\n]*\"status\":\"entity created\".*\"exit\":\"clean_stop\"")
    # Studio typed into by a screen reader (D613): the new scene's entry
    # takes a name inserted through AT-SPI's EditableText as typing, the
    # field begun as a press begins it, and reads it back.
    add_test(NAME studio_types_aloud
             COMMAND ${RAWFRAME_SHELL} ${PROJECT_SOURCE_DIR}/tools/xvfb_run.sh -s "-screen 0 1280x720x24 -br" ${RAWFRAME_SHELL}
                     ${CMAKE_CURRENT_SOURCE_DIR}/tests/read_aloud.sh $<TARGET_FILE:rawframe_host_studio_tool>
                     games/runners/runners.game level.scene "type=new scene or import:arena.scene"
             WORKING_DIRECTORY ${PROJECT_SOURCE_DIR})
    set_tests_properties(studio_types_aloud PROPERTIES
                         SKIP_REGULAR_EXPRESSION "skip: no"
                         PASS_REGULAR_EXPRESSION
                         "a11y: 3 entry new scene or import\n.*a11y: typed into new scene or import: b true\na11y: new scene or import reads arena.scene\n.*\"exit\":\"clean_stop\"")
    # The docks' edges dragged (D779): the Hierarchy wider and the
    # Inspector wider by a hundred pixels each, the bottom dock taller by
    # as much; the sizes are kept in the test's preferences as each edge is
    # let go.
    add_test(NAME studio_splits
             COMMAND ${CMAKE_COMMAND} -E env "STUDIO_SETTINGS=studio.preferences = {work}/studio.preferences"
                     ${RAWFRAME_SHELL} ${PROJECT_SOURCE_DIR}/tools/xvfb_run.sh -s "-screen 0 1280x720x24 -br" ${RAWFRAME_SHELL}
                     ${CMAKE_CURRENT_SOURCE_DIR}/tests/clicked.sh $<TARGET_FILE:rawframe_host_studio_tool>
                     games/runners/runners.game level.scene drag=369,200,469,200 drag=911,300,811,300 drag=400,418,400,318
                     wait=1
             WORKING_DIRECTORY ${PROJECT_SOURCE_DIR})
    set_tests_properties(studio_splits PROPERTIES
                         SKIP_REGULAR_EXPRESSION "skip: no device"
                         PASS_REGULAR_EXPRESSION
                         "\"hierarchyWidth\": 460,[ \n]*\"inspectorWidth\": 460,[ \n]*\"dockHeight\": 36[0-9].*\"code\":\"studio_layout\"[^\n]*\"resized\":3,\"hierarchyWidth\":460,\"inspectorWidth\":460,\"dockHeight\":36[0-9]}.*\"exit\":\"clean_stop\"")
    # And the sizes kept read back as Studio starts (D779), from a copy of
    # a document, nothing being dragged: Studio writes its preferences as it
    # opens a game (D781), never the checked-in one.
    add_test(NAME studio_keeps_layout
             COMMAND ${CMAKE_COMMAND} -E env STUDIO_PREFERENCES=${CMAKE_CURRENT_SOURCE_DIR}/tests/layout.preferences
                     ${RAWFRAME_SHELL} ${PROJECT_SOURCE_DIR}/tools/xvfb_run.sh -s "-screen 0 1280x720x24 -br" ${RAWFRAME_SHELL}
                     ${CMAKE_CURRENT_SOURCE_DIR}/tests/clicked.sh $<TARGET_FILE:rawframe_host_studio_tool>
                     games/runners/runners.game level.scene @level.scene wait=2
             WORKING_DIRECTORY ${PROJECT_SOURCE_DIR})
    set_tests_properties(studio_keeps_layout PROPERTIES
                         SKIP_REGULAR_EXPRESSION "skip: no device"
                         PASS_REGULAR_EXPRESSION
                         "\"code\":\"studio_layout\"[^\n]*\"resized\":0,\"hierarchyWidth\":420,\"inspectorWidth\":300,\"dockHeight\":300}.*\"exit\":\"clean_stop\"")
    # Several entities chosen (D787): the west wall, then the east wall
    # with Control held, their shared friction set together as one step;
    # then the rows from the east wall to the steep slope with Shift held,
    # deleted as one step.
    add_test(NAME studio_chooses_many
             COMMAND ${RAWFRAME_SHELL} ${PROJECT_SOURCE_DIR}/tools/xvfb_run.sh -s "-screen 0 1280x720x24 -br" ${RAWFRAME_SHELL}
                     ${CMAKE_CURRENT_SOURCE_DIR}/tests/clicked.sh $<TARGET_FILE:rawframe_host_studio_tool>
                     games/runners/runners.game level.scene @level.scene "@west wall" drag=186,288,186,288,Control_L
                     "until=studio_said,status=\"2 entities chosen" "@friction#2:0.25" "until=studio_said,status=\"friction set to 0.25 on 2"
                     drag=186,384,186,384,Shift_L "until=studio_said,status=\"4 entities chosen" @Delete "until=studio_said,status=\"4 entities deleted"
             WORKING_DIRECTORY ${PROJECT_SOURCE_DIR})
    set_tests_properties(studio_chooses_many PROPERTIES
                         SKIP_REGULAR_EXPRESSION "skip: no device"
                         PASS_REGULAR_EXPRESSION
                         "level.scene written: yes.*\"code\":\"studio_selection\"[^\n]*\"chosen\":[01],\"together\":2,")
    # Layers (D788): runners' scene with the ground on layer 1 and the walls
    # on layer 2 (tests/layered.scene, written by rawframe-author apply);
    # the Layers tab lists them, choosing layer 2 chooses both
    # walls, and with it locked, a wall's field is not set.
    add_test(NAME studio_layers
             COMMAND ${CMAKE_COMMAND} -E env
                     "STUDIO_SCENE_FILE=${CMAKE_CURRENT_SOURCE_DIR}/tests/layered.scene"
                     ${RAWFRAME_SHELL} ${PROJECT_SOURCE_DIR}/tools/xvfb_run.sh -s "-screen 0 1280x720x24 -br" ${RAWFRAME_SHELL}
                     ${CMAKE_CURRENT_SOURCE_DIR}/tests/clicked.sh $<TARGET_FILE:rawframe_host_studio_tool>
                     games/runners/runners.game level.scene @level.scene @Layers "@layer 2  2"
                     "until=studio_said,status=\"layer 2 chosen" "@Lock layer 2" "until=studio_said,status=\"layer 2 locked"
                     @Hierarchy "@west wall" "@friction#2:0.25" "until=studio_said,status=\"layer 2 is locked"
             WORKING_DIRECTORY ${PROJECT_SOURCE_DIR})
    set_tests_properties(studio_layers PROPERTIES
                         SKIP_REGULAR_EXPRESSION "skip: no device"
                         PASS_REGULAR_EXPRESSION
                         "\"code\":\"studio_selection\"[^\n]*\"layersChosen\":1,\"locks\":1,\"lockRefused\":[1-9]")
    # The Hierarchy as a tree (D785): the west wall dragged onto the ground
    # follows it, given runners' 2D attachment and shown under the ground;
    # the ground dragged onto the west wall, which follows it, is refused.
    add_test(NAME studio_parents
             COMMAND ${CMAKE_COMMAND} -E env STUDIO_SHOW_WRITTEN=1
                     ${RAWFRAME_SHELL} ${PROJECT_SOURCE_DIR}/tools/xvfb_run.sh -s "-screen 0 1280x720x24 -br" ${RAWFRAME_SHELL}
                     ${CMAKE_CURRENT_SOURCE_DIR}/tests/clicked.sh $<TARGET_FILE:rawframe_host_studio_tool>
                     games/runners/runners.game level.scene @level.scene drag=186,256,186,222
                     "until=studio_said,status=\"west wall follows ground"
                     drag=186,222,186,256 "until=studio_said,status=\"ground cannot follow what follows it"
             WORKING_DIRECTORY ${PROJECT_SOURCE_DIR})
    set_tests_properties(studio_parents PROPERTIES
                         SKIP_REGULAR_EXPRESSION "skip: no device"
                         PASS_REGULAR_EXPRESSION
                         "level.scene written: yes.*\"name\": \"west wall\".*\"rawframe.physics2d.attach\": {[ \n]*\"parent\": \"c5400cf8-4b07-4d16-84c2-951852c66b40\".*\"code\":\"studio_summary\"[^\n]*\"applied\":1,\"refused\":1,.*\"exit\":\"clean_stop\"")
    # Typed controls (D784): the ground's body's motion chosen from its
    # labels, its tile's color typed as hex digits, and its friction given a
    # value past its range, which is set and said so; and its tile's
    # texture chosen from the game's textures (D786).
    add_test(NAME studio_typed
             COMMAND ${RAWFRAME_SHELL} ${PROJECT_SOURCE_DIR}/tools/xvfb_run.sh -s "-screen 0 1280x720x24 -br" ${RAWFRAME_SHELL}
                     ${CMAKE_CURRENT_SOURCE_DIR}/tests/clicked.sh $<TARGET_FILE:rawframe_host_studio_tool>
                     games/runners/runners.game level.scene @level.scene @ground "@motion#2" @kinematic
                     "until=studio_said,status=\"motion set to kinematic"
                     "@friction#2:3" "until=studio_said,status=\"friction set to 3, past 0 to 1"
                     wheel=1100,500,30 "@color#2:#ff0000" "until=studio_said,status=\"color set to"
                     "@texture#2" @hud.png "until=studio_said,status=\"texture set to hud.png"
             WORKING_DIRECTORY ${PROJECT_SOURCE_DIR})
    set_tests_properties(studio_typed PROPERTIES
                         SKIP_REGULAR_EXPRESSION "skip: no device"
                         PASS_REGULAR_EXPRESSION
                         "level.scene written: yes.*\"code\":\"studio_summary\"[^\n]*\"applied\":4,\"refused\":0,.*\"exit\":\"clean_stop\"")
    # Commands' keys set in the Preferences dialog (D782): Duplicate given
    # Ctrl+Shift+D, which then duplicates; Redo given Ctrl+Z, which is said
    # to be Undo's too; text that is no chord refused; the keys kept in the
    # test's preferences.
    add_test(NAME studio_shortcuts
             COMMAND ${CMAKE_COMMAND} -E env "STUDIO_SETTINGS=studio.preferences = {work}/studio.preferences"
                     ${RAWFRAME_SHELL} ${PROJECT_SOURCE_DIR}/tools/xvfb_run.sh -s "-screen 0 1280x720x24 -br" ${RAWFRAME_SHELL}
                     ${CMAKE_CURRENT_SOURCE_DIR}/tests/clicked.sh $<TARGET_FILE:rawframe_host_studio_tool>
                     games/runners/runners.game level.scene @level.scene @Window @Preferences "until=studio_said,status=\"preferences"
                     "@Duplicate keys#2:Ctrl+Shift+D" "until=studio_said,status=\"Duplicate is Ctrl+Shift+D"
                     "@Redo keys#2:Ctrl+Z" "until=studio_said,status=\"Redo is Ctrl+Z, Ctrl+Z also Undo's"
                     "@Undo keys#2:Hello" "until=studio_said,status=\"keys refused"
                     @Close @ground keys=Control_L+Shift_L+d "until=studio_said,status=\"ground copy made"
             WORKING_DIRECTORY ${PROJECT_SOURCE_DIR})
    set_tests_properties(studio_shortcuts PROPERTIES
                         SKIP_REGULAR_EXPRESSION "skip: no device"
                         PASS_REGULAR_EXPRESSION
                         "\"keys\": {[ \n]*\"Duplicate\": \\[[ \n]*\"Ctrl\\+Shift\\+D\"[ \n]*\\],[ \n]*\"Redo\": \\[[ \n]*\"Ctrl\\+Z\"[ \n]*\\][ \n]*}.*\"code\":\"studio_summary\"[^\n]*\"applied\":[1-9][0-9]*,\"refused\":1,.*\"exit\":\"clean_stop\"")
    # Studio with no game starts on its start screen (D781): a game made
    # from the starter template under the name typed, by the function
    # rawframe-author new runs, and opened, kept first among the games
    # opened last.
    add_test(NAME studio_starts
             COMMAND ${CMAKE_COMMAND} -E env "STUDIO_SETTINGS=studio.preferences = {work}/studio.preferences"
                     ${RAWFRAME_SHELL} ${PROJECT_SOURCE_DIR}/tools/xvfb_run.sh -s "-screen 0 1280x720x24 -br" ${RAWFRAME_SHELL}
                     ${CMAKE_CURRENT_SOURCE_DIR}/tests/clicked.sh $<TARGET_FILE:rawframe_host_studio_tool>
                     none none @starter-3d "@new game#2:mygame" "until=studio_said,status=\"opened mygame.game" wait=1
             WORKING_DIRECTORY ${PROJECT_SOURCE_DIR})
    set_tests_properties(studio_starts PROPERTIES
                         SKIP_REGULAR_EXPRESSION "skip: no device"
                         PASS_REGULAR_EXPRESSION
                         "\"recent\": \\[[ \n]*\"[^\"]*/games/mygame/mygame.game\".*\"code\":\"studio_summary\"[^\n]*\"scenes\":[1-9],.*\"code\":\"studio_start\"[^\n]*\"made\":1,\"game\":\"mygame.game\",\"recent\":1}")
    # A game opened last opened from the start screen, one no longer there
    # not offered; and one opened by its description's path (D781).
    add_test(NAME studio_opens_recent
             COMMAND ${CMAKE_COMMAND} -E env STUDIO_PREFERENCES=${CMAKE_CURRENT_SOURCE_DIR}/tests/recent.preferences
                     ${RAWFRAME_SHELL} ${PROJECT_SOURCE_DIR}/tools/xvfb_run.sh -s "-screen 0 1280x720x24 -br" ${RAWFRAME_SHELL}
                     ${CMAKE_CURRENT_SOURCE_DIR}/tests/clicked.sh $<TARGET_FILE:rawframe_host_studio_tool>
                     none none "@starter.game  templates/starter-3d" "until=studio_said,status=\"opened starter.game" wait=1
             WORKING_DIRECTORY ${PROJECT_SOURCE_DIR})
    set_tests_properties(studio_opens_recent PROPERTIES
                         SKIP_REGULAR_EXPRESSION "skip: no device"
                         PASS_REGULAR_EXPRESSION
                         "\"code\":\"studio_summary\"[^\n]*\"scenes\":[1-9],.*\"code\":\"studio_start\"[^\n]*\"made\":0,\"game\":\"starter.game\",\"recent\":2}")
    add_test(NAME studio_opens
             COMMAND ${RAWFRAME_SHELL} ${PROJECT_SOURCE_DIR}/tools/xvfb_run.sh -s "-screen 0 1280x720x24 -br" ${RAWFRAME_SHELL}
                     ${CMAKE_CURRENT_SOURCE_DIR}/tests/clicked.sh $<TARGET_FILE:rawframe_host_studio_tool>
                     none none "@open#2:templates/nothing.game" "until=studio_said,status=\"templates/nothing.game is not"
                     "@open#2:templates/starter-3d/starter.game" "until=studio_said,status=\"opened starter.game" wait=1
             WORKING_DIRECTORY ${PROJECT_SOURCE_DIR})
    set_tests_properties(studio_opens PROPERTIES
                         SKIP_REGULAR_EXPRESSION "skip: no device"
                         PASS_REGULAR_EXPRESSION
                         "\"code\":\"studio_summary\"[^\n]*\"scenes\":[1-9],.*\"code\":\"studio_start\"[^\n]*\"made\":0,\"game\":\"starter.game\"")
    # Studio's commands (D777): ground renamed, then undone by Control Z,
    # redone through the palette's search, undone again from the Edit
    # menu, and deleted by the Delete key with no field holding the
    # keyboard; each as its button does it.
    add_test(NAME studio_commands
             COMMAND ${RAWFRAME_SHELL} ${PROJECT_SOURCE_DIR}/tools/xvfb_run.sh -s "-screen 0 1280x720x24 -br" ${RAWFRAME_SHELL}
                     ${CMAKE_CURRENT_SOURCE_DIR}/tests/clicked.sh $<TARGET_FILE:rawframe_host_studio_tool>
                     games/runners/runners.game level.scene @level.scene @ground "@name#2:first" "until=studio_said,status=\"renamed first\""
                     keys=Control_L+z "until=studio_said,status=\"undone\"" @commands:redo "until=studio_said,status=\"redone\""
                     @Edit "@Undo  Ctrl+Z" "until=studio_said,status=\"undone\"" keys=Delete "until=studio_said,status=\"entity deleted\""
             WORKING_DIRECTORY ${PROJECT_SOURCE_DIR})
    # Framing what is chosen (D792): the west wall chosen, F pressed with
    # no field holding the keyboard, and the view turned on it by the
    # session, its target where the wall stands.
    add_test(NAME studio_frames
             COMMAND ${RAWFRAME_SHELL} ${PROJECT_SOURCE_DIR}/tools/xvfb_run.sh -s "-screen 0 1280x720x24 -br" ${RAWFRAME_SHELL}
                     ${CMAKE_CURRENT_SOURCE_DIR}/tests/clicked.sh $<TARGET_FILE:rawframe_host_studio_tool>
                     games/runners/runners.game level.scene @level.scene "@west wall" keys=f "until=studio_said,status=\"framed\""
             WORKING_DIRECTORY ${PROJECT_SOURCE_DIR})
    set_tests_properties(studio_frames PROPERTIES
                         SKIP_REGULAR_EXPRESSION "skip: no device"
                         PASS_REGULAR_EXPRESSION
                         "pressed f.*\"code\":\"studio_summary\"[^\n]*\"viewsSet\":1,[^\n]*\"status\":\"framed\".*\"exit\":\"clean_stop\"")
    # Bookmarks (D796): a view kept in bookmark 2 by Control Shift 2, the
    # view moved, and Control 2 going back to it, through the session's
    # records into the scene's editor state.
    add_test(NAME studio_bookmarks
             COMMAND ${RAWFRAME_SHELL} ${PROJECT_SOURCE_DIR}/tools/xvfb_run.sh -s "-screen 0 1280x720x24 -br" ${RAWFRAME_SHELL}
                     ${CMAKE_CURRENT_SOURCE_DIR}/tests/clicked.sh $<TARGET_FILE:rawframe_host_studio_tool>
                     games/runners/runners.game level.scene @level.scene "@eye#2:0,30,0.5" "until=studio_said,status=\"view set\""
                     keys=Control_L+Shift_L+2 "until=studio_said,status=\"bookmark 2 set\"" "@eye#2:5,5,5"
                     "until=studio_said,status=\"view set\"#2" keys=Control_L+2 "until=studio_said,status=\"bookmark 2\""
             WORKING_DIRECTORY ${PROJECT_SOURCE_DIR})
    set_tests_properties(studio_bookmarks PROPERTIES
                         SKIP_REGULAR_EXPRESSION "skip: no device"
                         PASS_REGULAR_EXPRESSION
                         "saw studio_said,status=\"bookmark 2\".*\"code\":\"studio_viewport\"[^\n]*\"bookmarksKept\":1,\"bookmarksVisited\":1")
    set_tests_properties(studio_commands PROPERTIES
                         SKIP_REGULAR_EXPRESSION "skip: no device"
                         PASS_REGULAR_EXPRESSION
                         "pressed Control_L\\+z.*found commands at.*typed redo at.*found Edit at.*found Undo  Ctrl\\+Z at.*pressed Delete.*level.scene written: yes.*\"code\":\"studio_summary\"[^\n]*\"applied\":2,\"refused\":0,\"undone\":2,\"redone\":1,[^\n]*\"status\":\"entity deleted\".*\"exit\":\"clean_stop\"")
    # Studio clicked: runners' level chosen shows its entities, read from
    # the session, and its ground chosen shows its four components and is
    # chosen in the session; its body's width typed as 12 and given with
    # Enter is set by the session, which writes the level; an x typed
    # first is refused before it reaches the session. Undo then Redo in
    # the header leave the edit standing. This test and those after it that
    # end in an edit wait for its status (studio_said) before Studio is
    # stopped, so a loaded machine drawing a frame a second still takes
    # the last input (D489).
    add_test(NAME studio_clicked
             COMMAND ${RAWFRAME_SHELL} ${PROJECT_SOURCE_DIR}/tools/xvfb_run.sh -s "-screen 0 1280x720x24 -br" ${RAWFRAME_SHELL}
                     ${CMAKE_CURRENT_SOURCE_DIR}/tests/clicked.sh $<TARGET_FILE:rawframe_host_studio_tool>
                     games/runners/runners.game level.scene @level.scene @ground "@width#2:x" "@width#2:12" @Undo @Redo "until=studio_said,status=\"redone\""
             WORKING_DIRECTORY ${PROJECT_SOURCE_DIR})
    set_tests_properties(studio_clicked PROPERTIES
                         SKIP_REGULAR_EXPRESSION "skip: no device"
                         PASS_REGULAR_EXPRESSION
                         "found level.scene at.*found ground at.*typed x at [0-9]+,[0-9]+.*typed 12 at [0-9]+,[0-9]+.*found Undo at 1[0-9][0-9][0-9],2[0-9].*found Redo at 1[0-9][0-9][0-9],2[0-9].*level.scene written: yes.*\"code\":\"studio_summary\"[^\n]*\"scene\":\"level.scene\",\"entities\":[1-9][0-9]*,\"entity\":\"c5400cf8-4b07-4d16-84c2-951852c66b40\",\"components\":4,\"applied\":1,\"refused\":1,\"undone\":1,\"redone\":1,\"undoable\":1,\"redoable\":0,[^\n]*\"status\":\"redone\".*\"exit\":\"clean_stop\"")
    # Studio's operations, as describe offers them: in runners' level a new
    # entity is made and chosen, renamed crate, given runners.tile by its
    # name typed, the tile removed, and the entity deleted; Undo brings it
    # back, so the level is written with a crate in it.
    add_test(NAME studio_operates
             COMMAND ${RAWFRAME_SHELL} ${PROJECT_SOURCE_DIR}/tools/xvfb_run.sh -s "-screen 0 1280x720x24 -br" ${RAWFRAME_SHELL}
                     ${CMAKE_CURRENT_SOURCE_DIR}/tests/clicked.sh $<TARGET_FILE:rawframe_host_studio_tool>
                     games/runners/runners.game level.scene @level.scene @New "@name#2:crate" "@add#2:runners.tile"
                     @Remove @Delete @Undo "until=studio_said,status=\"undone\""
             WORKING_DIRECTORY ${PROJECT_SOURCE_DIR})
    set_tests_properties(studio_operates PROPERTIES
                         SKIP_REGULAR_EXPRESSION "skip: no device"
                         PASS_REGULAR_EXPRESSION
                         "typed crate at [0-9]+,[0-9]+.*typed runners.tile at [0-9]+,[0-9]+.*clicked at [0-9]+,[0-9]+.*clicked at [0-9]+,[0-9]+.*clicked at [0-9]+,[0-9]+.*level.scene written: yes.*\"code\":\"studio_summary\"[^\n]*\"scene\":\"level.scene\",\"entities\":10,\"entity\":\"\",\"components\":0,\"applied\":5,\"refused\":0,\"undone\":1,\"redone\":0,\"undoable\":4,\"redoable\":1,[^\n]*\"status\":\"undone\".*\"exit\":\"clean_stop\"")
    # A reference set by the name of the entity it names (D447): a new
    # entity in runners' level is given attach, whose parent is typed:
    # west, which two entities' names start, is refused, and ground set;
    # the field then shows ground, and is set again by gr, and cleared.
    add_test(NAME studio_references
             COMMAND ${RAWFRAME_SHELL} ${PROJECT_SOURCE_DIR}/tools/xvfb_run.sh -s "-screen 0 1280x720x24 -br" ${RAWFRAME_SHELL}
                     ${CMAKE_CURRENT_SOURCE_DIR}/tests/clicked.sh $<TARGET_FILE:rawframe_host_studio_tool>
                     games/runners/runners.game level.scene @level.scene @New "@add#2:attach" "@parent#2:west"
                     "@parent#2:ground"
                     "@parent#2:gr" "@parent#2" keys=BackSpace,Return "until=studio_said,status=\"parent cleared\""
             WORKING_DIRECTORY ${PROJECT_SOURCE_DIR})
    set_tests_properties(studio_references PROPERTIES
                         SKIP_REGULAR_EXPRESSION "skip: no device"
                         PASS_REGULAR_EXPRESSION
                         "typed attach at [0-9]+,[0-9]+.*typed west at [0-9]+,[0-9]+.*typed ground at [0-9]+,[0-9]+.*typed gr at [0-9]+,[0-9]+.*clicked at [0-9]+,[0-9]+.*pressed BackSpace,Return.*level.scene written: yes.*\"code\":\"studio_summary\"[^\n]*\"components\":1,\"applied\":5,\"refused\":1,[^\n]*\"status\":\"parent cleared\".*\"exit\":\"clean_stop\"")
    # A scene's instance placed and removed (D448, D450): in runners' hall,
    # the instance field refuses x, which no scene is, and places platform;
    # the entity it brought chosen, Delete removes it through the instance's
    # patch, Restore takes it back, and Remove instance removes the whole
    # instance, so the hall is as it was, byte for byte. The instance field
    # is under the search's (D473).
    add_test(NAME studio_instances
             COMMAND ${RAWFRAME_SHELL} ${PROJECT_SOURCE_DIR}/tools/xvfb_run.sh -s "-screen 0 1280x720x24 -br" ${RAWFRAME_SHELL}
                     ${CMAKE_CURRENT_SOURCE_DIR}/tests/clicked.sh $<TARGET_FILE:rawframe_host_studio_tool>
                     games/runners/runners.game hall.scene @hall.scene "@instance of#2:x" "@instance of#2:platform" 186,254 @Delete
                     @Restore "@Remove instance" "until=studio_said,status=\"instance removed\""
             WORKING_DIRECTORY ${PROJECT_SOURCE_DIR})
    set_tests_properties(studio_instances PROPERTIES
                         SKIP_REGULAR_EXPRESSION "skip: no device"
                         PASS_REGULAR_EXPRESSION
                         "typed x at [0-9]+,[0-9]+.*typed platform at [0-9]+,[0-9]+.*clicked at [0-9]+,[0-9]+.*clicked at [0-9]+,[0-9]+.*clicked at [0-9]+,[0-9]+.*clicked at [0-9]+,[0-9]+.*hall.scene written: no.*\"code\":\"studio_summary\"[^\n]*\"scene\":\"hall.scene\",\"entities\":1,[^\n]*\"applied\":4,\"refused\":1,[^\n]*\"status\":\"instance removed\".*\"exit\":\"clean_stop\"")
    # A new scene (D449): typed under runners' scenes, made with its
    # sidecar, listed in order and chosen, and given an entity.
    add_test(NAME studio_makes_scenes
             COMMAND ${RAWFRAME_SHELL} ${PROJECT_SOURCE_DIR}/tools/xvfb_run.sh -s "-screen 0 1280x720x24 -br" ${RAWFRAME_SHELL}
                     ${CMAKE_CURRENT_SOURCE_DIR}/tests/clicked.sh $<TARGET_FILE:rawframe_host_studio_tool>
                     games/runners/runners.game crate.scene "@new scene or import#2:crate" @New "until=studio_said,status=\"entity created\""
             WORKING_DIRECTORY ${PROJECT_SOURCE_DIR})
    set_tests_properties(studio_makes_scenes PROPERTIES
                         SKIP_REGULAR_EXPRESSION "skip: no device"
                         PASS_REGULAR_EXPRESSION
                         "typed crate at [0-9]+,[0-9]+.*clicked at [0-9]+,[0-9]+.*\"code\":\"studio_summary\"[^\n]*\"scenes\":15,\"scene\":\"crate.scene\",\"entities\":1,[^\n]*\"applied\":2,\"refused\":0,[^\n]*\"status\":\"entity created\".*\"exit\":\"clean_stop\"")
    # A file imported from Studio (D503, D505): the plaza's paving typed by
    # its absolute path into the field under runners' scenes, made one of
    # runners' textures under its own name.
    add_test(NAME studio_imports
             COMMAND ${RAWFRAME_SHELL} ${PROJECT_SOURCE_DIR}/tools/xvfb_run.sh -s "-screen 0 1280x720x24 -br" ${RAWFRAME_SHELL}
                     ${CMAKE_CURRENT_SOURCE_DIR}/tests/clicked.sh $<TARGET_FILE:rawframe_host_studio_tool>
                     games/runners/runners.game level.scene "@new scene or import#2:${PROJECT_SOURCE_DIR}/games/plaza/paving.png"
                     "until=studio_said,status=\"imported "
             WORKING_DIRECTORY ${PROJECT_SOURCE_DIR})
    set_tests_properties(studio_imports PROPERTIES
                         SKIP_REGULAR_EXPRESSION "skip: no device"
                         PASS_REGULAR_EXPRESSION
                         "typed [^\n]*/games/plaza/paving.png at [0-9]+,[0-9]+.*\"code\":\"studio_summary\"[^\n]*\"applied\":1,\"refused\":0,[^\n]*\"status\":\"imported texture paving.png\".*\"exit\":\"clean_stop\"")
    # An entity moved among the scene's own (D451): ground, first in
    # runners' level, is refused Up and moved Down twice, staying chosen.
    add_test(NAME studio_moves
             COMMAND ${RAWFRAME_SHELL} ${PROJECT_SOURCE_DIR}/tools/xvfb_run.sh -s "-screen 0 1280x720x24 -br" ${RAWFRAME_SHELL}
                     ${CMAKE_CURRENT_SOURCE_DIR}/tests/clicked.sh $<TARGET_FILE:rawframe_host_studio_tool>
                     games/runners/runners.game level.scene @level.scene @ground @Up @Down @Down "until=studio_said,status=\"moved down\""
             WORKING_DIRECTORY ${PROJECT_SOURCE_DIR})
    set_tests_properties(studio_moves PROPERTIES
                         SKIP_REGULAR_EXPRESSION "skip: no device"
                         PASS_REGULAR_EXPRESSION
                         "clicked at [0-9]+,[0-9]+.*clicked at [0-9]+,[0-9]+.*clicked at [0-9]+,[0-9]+.*level.scene written: yes.*\"code\":\"studio_summary\"[^\n]*\"entity\":\"c5400cf8-4b07-4d16-84c2-951852c66b40\",[^\n]*\"applied\":2,\"refused\":1,[^\n]*\"status\":\"moved down\".*\"exit\":\"clean_stop\"")
    # An entity copied whole (D458): ground, chosen, is duplicated by the
    # Entities heading's button into "ground copy", its every component
    # and value written again (its tile's 64-bit texture among them), in
    # one transaction, the copy chosen.
    add_test(NAME studio_duplicates
             COMMAND ${RAWFRAME_SHELL} ${PROJECT_SOURCE_DIR}/tools/xvfb_run.sh -s "-screen 0 1280x720x24 -br" ${RAWFRAME_SHELL}
                     ${CMAKE_CURRENT_SOURCE_DIR}/tests/clicked.sh $<TARGET_FILE:rawframe_host_studio_tool>
                     games/runners/runners.game level.scene @level.scene @ground @Duplicate "until=studio_said,status=\"ground copy made\""
             WORKING_DIRECTORY ${PROJECT_SOURCE_DIR})
    set_tests_properties(studio_duplicates PROPERTIES
                         ENVIRONMENT "STUDIO_SHOW_WRITTEN=1"
                         SKIP_REGULAR_EXPRESSION "skip: no device"
                         PASS_REGULAR_EXPRESSION
                         "clicked at [0-9]+,[0-9]+.*level.scene written: yes.*\"texture\": 11839623990059228360.*\"name\": \"ground copy\",[ \n]*\"components\": {[ \n]*\"rawframe.physics2d.body\": {[ \n]*\"height\": 0.5,[ \n]*\"shape\": 1,[ \n]*\"width\": 40[ \n]*},[ \n]*\"rawframe.physics2d.pose\": {},[ \n]*\"rawframe.physics2d.velocity\": {},[ \n]*\"runners.tile\": {[ \n]*\"color\": 4294967295,[^}]*\"texture\": 11839623990059228360,.*\"code\":\"studio_summary\"[^\n]*\"components\":4,[^\n]*\"applied\":1,\"refused\":0,[^\n]*\"undoable\":1,[^\n]*\"status\":\"ground copy made\"")
    # An entity moved to another scene (D498): ground, chosen in the level,
    # Move to pressed and the hall's row clicked, is made in the hall with
    # its id and components and deleted from the level, both written, in
    # one transaction over both scenes (D497), the level left one entry to
    # undo and eight entities.
    add_test(NAME studio_moves_to_scene
             COMMAND ${RAWFRAME_SHELL} ${PROJECT_SOURCE_DIR}/tools/xvfb_run.sh -s "-screen 0 1280x720x24 -br" ${RAWFRAME_SHELL}
                     ${CMAKE_CURRENT_SOURCE_DIR}/tests/clicked.sh $<TARGET_FILE:rawframe_host_studio_tool>
                     games/runners/runners.game hall.scene @level.scene @ground "@Move to" @hall.scene
                     "until=studio_said,status=\"ground moved to hall.scene\""
             WORKING_DIRECTORY ${PROJECT_SOURCE_DIR})
    set_tests_properties(studio_moves_to_scene PROPERTIES
                         ENVIRONMENT "STUDIO_SHOW_WRITTEN=1;STUDIO_ALSO_WRITTEN=level.scene"
                         SKIP_REGULAR_EXPRESSION "skip: no device"
                         PASS_REGULAR_EXPRESSION
                         "saw studio_said,status=.ground moved to hall.scene.*hall.scene written: yes.*\"id\": \"c5400cf8-4b07-4d16-84c2-951852c66b40\",[ \n]*\"name\": \"ground\".*level.scene written: yes.*\"code\":\"studio_summary\"[^\n]*\"scene\":\"level.scene\",\"entities\":8,[^\n]*\"applied\":1,\"refused\":0,[^\n]*\"undoable\":1,")
    # A component authored against another layout updated (D452): the
    # copy of runners' level records an older mark for the physics body,
    # so ground's body heading offers Update, which carries it over and
    # writes the level back as it was, byte for byte.
    add_test(NAME studio_updates
             COMMAND ${RAWFRAME_SHELL} ${PROJECT_SOURCE_DIR}/tools/xvfb_run.sh -s "-screen 0 1280x720x24 -br" ${RAWFRAME_SHELL}
                     ${CMAKE_CURRENT_SOURCE_DIR}/tests/clicked.sh $<TARGET_FILE:rawframe_host_studio_tool>
                     games/runners/runners.game level.scene @level.scene @ground @Update "until=studio_said,status=\"rawframe.physics2d.body updated\""
             WORKING_DIRECTORY ${PROJECT_SOURCE_DIR})
    set_tests_properties(studio_updates PROPERTIES
                         ENVIRONMENT "STUDIO_SCENE_EDIT=s/\"rawframe.physics2d.body\": \"593035e6aa3af5e7\"/\"rawframe.physics2d.body\": \"0000000000000001\"/"
                         SKIP_REGULAR_EXPRESSION "skip: no device"
                         PASS_REGULAR_EXPRESSION
                         "clicked at [0-9]+,[0-9]+.*level.scene written: no.*\"code\":\"studio_summary\"[^\n]*\"applied\":1,\"refused\":0,[^\n]*\"status\":\"rawframe.physics2d.body updated\".*\"exit\":\"clean_stop\"")
    # A game whose program does not compile (D453): Studio shows the
    # diagnostic rather than ending, opens its file at its line in the
    # editor studio.editor names (here one that fixes it, as the author
    # would), and Retry reads the game again, now whole.
    add_test(NAME studio_reads_again
             COMMAND ${RAWFRAME_SHELL} ${PROJECT_SOURCE_DIR}/tools/xvfb_run.sh -s "-screen 0 1280x720x24 -br" ${RAWFRAME_SHELL}
                     ${CMAKE_CURRENT_SOURCE_DIR}/tests/clicked.sh $<TARGET_FILE:rawframe_host_studio_tool>
                     games/runners/runners.game controls.kest "@Open in editor" @Retry "until=studio_said,status=\"the game reads\""
             WORKING_DIRECTORY ${PROJECT_SOURCE_DIR})
    set_tests_properties(studio_reads_again PROPERTIES
                         ENVIRONMENT "STUDIO_SCENE_EDIT=$a this is not kest (;STUDIO_SETTINGS=studio.editor = ${CMAKE_CURRENT_SOURCE_DIR}/tests/fixing_editor.sh --goto {file}:{line}:{column}"
                         SKIP_REGULAR_EXPRESSION "skip: no device"
                         PASS_REGULAR_EXPRESSION
                         "clicked at [0-9]+,[0-9]+.*clicked at [0-9]+,[0-9]+.*controls.kest written: no.*editor opened --goto [^ ]*/game/controls.kest:[0-9]+:1.*\"code\":\"studio_summary\"[^\n]*\"scenes\":14,[^\n]*\"opened\":1,[^\n]*\"status\":\"the game reads\".*\"exit\":\"clean_stop\"")
    # A search of the entity column (D473): in runners' level, slope finds
    # the three slopes, and the field, under them now, then takes a wall
    # holding runners.tile by its last part, which finds the two walls.
    add_test(NAME studio_finds
             COMMAND ${RAWFRAME_SHELL} ${PROJECT_SOURCE_DIR}/tools/xvfb_run.sh -s "-screen 0 1280x720x24 -br" ${RAWFRAME_SHELL}
                     ${CMAKE_CURRENT_SOURCE_DIR}/tests/clicked.sh $<TARGET_FILE:rawframe_host_studio_tool>
                     games/runners/runners.game level.scene @level.scene "@find#2:slope" "@find#2:wall has:tile" "until=studio_searched,having=\"tile\""
             WORKING_DIRECTORY ${PROJECT_SOURCE_DIR})
    set_tests_properties(studio_finds PROPERTIES
                         SKIP_REGULAR_EXPRESSION "skip: no device"
                         PASS_REGULAR_EXPRESSION
                         "level.scene written: no.*\"code\":\"studio_searched\"[^\n]*\"found\":3,\"named\":\"slope\",\"having\":\"\".*\"code\":\"studio_searched\"[^\n]*\"found\":2,\"named\":\"wall\",\"having\":\"tile\".*\"code\":\"studio_summary\"[^\n]*\"entities\":2,[^\n]*\"searched\":2,.*\"exit\":\"clean_stop\"")
    # A scene's history (D454): ground renamed three times, the history
    # lists the three; its first entry clicked undoes the two after it,
    # and its last clicked redoes them. The history sits under the view's
    # four rows (the snap the fourth, D471).
    add_test(NAME studio_steps_history
             COMMAND ${RAWFRAME_SHELL} ${PROJECT_SOURCE_DIR}/tools/xvfb_run.sh -s "-screen 0 1280x720x24 -br" ${RAWFRAME_SHELL}
                     ${CMAKE_CURRENT_SOURCE_DIR}/tests/clicked.sh $<TARGET_FILE:rawframe_host_studio_tool>
                     games/runners/runners.game level.scene @level.scene @ground "@name#2:first" "@name#2:second"
                     "@name#2:third" @History "@rename to first" "@rename to third" "until=studio_said,status=\"redone\""
             WORKING_DIRECTORY ${PROJECT_SOURCE_DIR})
    set_tests_properties(studio_steps_history PROPERTIES
                         SKIP_REGULAR_EXPRESSION "skip: no device"
                         PASS_REGULAR_EXPRESSION
                         "typed third at [0-9]+,[0-9]+.*clicked at [0-9]+,[0-9]+.*clicked at [0-9]+,[0-9]+.*level.scene written: yes.*\"code\":\"studio_summary\"[^\n]*\"applied\":3,\"refused\":0,\"undone\":2,\"redone\":2,\"undoable\":3,\"redoable\":0,[^\n]*\"status\":\"redone\".*\"exit\":\"clean_stop\"")
    # An asset typed by name (D455): ground's tile texture, scrolled to, is
    # given hud.png, which the game declares, by its name; hud alone, which
    # several assets' names start with, is refused.
    add_test(NAME studio_names_assets
             COMMAND ${RAWFRAME_SHELL} ${PROJECT_SOURCE_DIR}/tools/xvfb_run.sh -s "-screen 0 1280x720x24 -br" ${RAWFRAME_SHELL}
                     ${CMAKE_CURRENT_SOURCE_DIR}/tests/clicked.sh $<TARGET_FILE:rawframe_host_studio_tool>
                     games/runners/runners.game level.scene @level.scene @ground wheel=1100,400,6 "@texture#2:hud.png"
                     "@texture#2:hud" "until=studio_said,status=\"texture: "
             WORKING_DIRECTORY ${PROJECT_SOURCE_DIR})
    set_tests_properties(studio_names_assets PROPERTIES
                         SKIP_REGULAR_EXPRESSION "skip: no device"
                         PASS_REGULAR_EXPRESSION
                         "typed hud.png at [0-9]+,[0-9]+.*typed hud at [0-9]+,[0-9]+.*level.scene written: yes.*\"code\":\"studio_summary\"[^\n]*\"applied\":1,\"refused\":1,[^\n]*\"status\":\"texture: [0-9]+ assets match hud\".*\"exit\":\"clean_stop\"")
    # Studio scrolled (D441, D442): runners' west slope has more rows than
    # its column holds; six detents of the wheel over them scroll the
    # column 600 pixels, and the tile's width, clicked where scrolling
    # brought it, is the field set.
    add_test(NAME studio_scrolls
             COMMAND ${RAWFRAME_SHELL} ${PROJECT_SOURCE_DIR}/tools/xvfb_run.sh -s "-screen 0 1280x720x24 -br" ${RAWFRAME_SHELL}
                     ${CMAKE_CURRENT_SOURCE_DIR}/tests/clicked.sh $<TARGET_FILE:rawframe_host_studio_tool>
                     games/runners/runners.game level.scene @level.scene "@west slope" wheel=1000,400,6 "@width#2:9" "until=studio_said,status=\"width set to 9\""
             WORKING_DIRECTORY ${PROJECT_SOURCE_DIR})
    set_tests_properties(studio_scrolls PROPERTIES
                         SKIP_REGULAR_EXPRESSION "skip: no device"
                         PASS_REGULAR_EXPRESSION
                         "turned the wheel 6 at 1000,400.*typed 9 at [0-9]+,[0-9]+.*level.scene written: yes.*\"code\":\"studio_summary\"[^\n]*\"components\":4,\"applied\":1,\"refused\":0,[^\n]*\"wheeled\":6,\"componentsScrolled\":600,[^\n]*\"status\":\"width set to 9\".*\"exit\":\"clean_stop\"")
    # The same, the slope clicked and the wheel turned in one frame, as a
    # slow machine reads them (D479): the wheel still scrolls the rows the
    # click brought, not only those shown before it.
    add_test(NAME studio_scrolls_frozen
             COMMAND ${RAWFRAME_SHELL} ${PROJECT_SOURCE_DIR}/tools/xvfb_run.sh -s "-screen 0 1280x720x24 -br" ${RAWFRAME_SHELL}
                     ${CMAKE_CURRENT_SOURCE_DIR}/tests/clicked.sh $<TARGET_FILE:rawframe_host_studio_tool>
                     games/runners/runners.game level.scene @level.scene freeze "@west slope" wheel=1000,400,6 thaw "@width#2:9" "until=studio_said,status=\"width set to 9\""
             WORKING_DIRECTORY ${PROJECT_SOURCE_DIR})
    set_tests_properties(studio_scrolls_frozen PROPERTIES
                         SKIP_REGULAR_EXPRESSION "skip: no device"
                         PASS_REGULAR_EXPRESSION
                         "turned the wheel 6 at 1000,400.*typed 9 at [0-9]+,[0-9]+.*level.scene written: yes.*\"code\":\"studio_summary\"[^\n]*\"components\":4,\"applied\":1,\"refused\":0,[^\n]*\"wheeled\":6,\"componentsScrolled\":600,[^\n]*\"status\":\"width set to 9\".*\"exit\":\"clean_stop\"")
    # The same, the row and the slope clicked in one frame too (D486): the
    # second press is on the components the first brought.
    add_test(NAME studio_clicks_frozen
             COMMAND ${RAWFRAME_SHELL} ${PROJECT_SOURCE_DIR}/tools/xvfb_run.sh -s "-screen 0 1280x720x24 -br" ${RAWFRAME_SHELL}
                     ${CMAKE_CURRENT_SOURCE_DIR}/tests/clicked.sh $<TARGET_FILE:rawframe_host_studio_tool>
                     games/runners/runners.game level.scene freeze @level.scene 186,318 wheel=1000,400,6 thaw "@width#2:9" "until=studio_said,status=\"width set to 9\""
             WORKING_DIRECTORY ${PROJECT_SOURCE_DIR})
    set_tests_properties(studio_clicks_frozen PROPERTIES
                         SKIP_REGULAR_EXPRESSION "skip: no device"
                         PASS_REGULAR_EXPRESSION
                         "turned the wheel 6 at 1000,400.*typed 9 at [0-9]+,[0-9]+.*level.scene written: yes.*\"code\":\"studio_summary\"[^\n]*\"components\":4,\"applied\":1,\"refused\":0,[^\n]*\"wheeled\":6,\"componentsScrolled\":600,[^\n]*\"status\":\"width set to 9\".*\"exit\":\"clean_stop\"")
    # The history test, the third edit typed and the history's rows clicked
    # in one frame (D489): the edit is submitted before the clicks that
    # follow it, which find the row it added.
    add_test(NAME studio_steps_history_frozen
             COMMAND ${RAWFRAME_SHELL} ${PROJECT_SOURCE_DIR}/tools/xvfb_run.sh -s "-screen 0 1280x720x24 -br" ${RAWFRAME_SHELL}
                     ${CMAKE_CURRENT_SOURCE_DIR}/tests/clicked.sh $<TARGET_FILE:rawframe_host_studio_tool>
                     games/runners/runners.game level.scene @level.scene @ground "@name#2:first" "@name#2:second"
                     @History freeze 1161,97:third 200,474 200,538 thaw "until=studio_said,status=\"redone\""
             WORKING_DIRECTORY ${PROJECT_SOURCE_DIR})
    set_tests_properties(studio_steps_history_frozen PROPERTIES
                         SKIP_REGULAR_EXPRESSION "skip: no device"
                         PASS_REGULAR_EXPRESSION
                         "typed third at [0-9]+,[0-9]+.*clicked at 200,474.*clicked at 200,538.*let the program go on.*level.scene written: yes.*\"code\":\"studio_summary\"[^\n]*\"applied\":3,\"refused\":0,\"undone\":2,\"redone\":2,\"undoable\":3,\"redoable\":0,")
    # Studio reverting (D443): runners' level brings a platform whose pose
    # its instance sets; the instance's x reverted drops its own value for
    # its source's, written to the level.
    add_test(NAME studio_reverts
             COMMAND ${RAWFRAME_SHELL} ${PROJECT_SOURCE_DIR}/tools/xvfb_run.sh -s "-screen 0 1280x720x24 -br" ${RAWFRAME_SHELL}
                     ${CMAKE_CURRENT_SOURCE_DIR}/tests/clicked.sh $<TARGET_FILE:rawframe_host_studio_tool>
                     games/runners/runners.game level.scene @level.scene "@instance 0  b4560e55" "@Revert#2" "until=studio_said,status=\"x reverted\""
             WORKING_DIRECTORY ${PROJECT_SOURCE_DIR})
    set_tests_properties(studio_reverts PROPERTIES
                         SKIP_REGULAR_EXPRESSION "skip: no device"
                         PASS_REGULAR_EXPRESSION
                         "clicked at [0-9]+,[0-9]+.*level.scene written: yes.*\"code\":\"studio_summary\"[^\n]*\"entity\":\"b4560e55-e4e2-467a-ab08-e91b7119397f\",\"components\":1,\"applied\":1,\"refused\":0,[^\n]*\"status\":\"x reverted\".*\"exit\":\"clean_stop\"")
    # A game's style sheet edited in Studio (D637): Stalls' sheet listed
    # with its scenes and chosen, its button class chosen, and its base
    # fill typed as a color, set through the session's styles.set_part and
    # written; Undo and Redo leave it standing.
    add_test(NAME studio_styles
             COMMAND ${RAWFRAME_SHELL} ${PROJECT_SOURCE_DIR}/tools/xvfb_run.sh -s "-screen 0 1280x720x24 -br" ${RAWFRAME_SHELL}
                     ${CMAKE_CURRENT_SOURCE_DIR}/tests/clicked.sh $<TARGET_FILE:rawframe_host_studio_tool>
                     games/tycoon/tycoon.game tycoon.styles @tycoon.styles @button "@fill#2:#c01818c0" @Undo @Redo
                     "until=studio_said,status=\"redone\""
             WORKING_DIRECTORY ${PROJECT_SOURCE_DIR})
    set_tests_properties(studio_styles PROPERTIES
                         SKIP_REGULAR_EXPRESSION "skip: no device"
                         PASS_REGULAR_EXPRESSION
                         "clicked at [0-9]+,[0-9]+.*clicked at [0-9]+,[0-9]+.*typed #c01818c0 at [0-9]+,[0-9]+.*clicked at [0-9]+,[0-9]+.*clicked at [0-9]+,[0-9]+.*tycoon.styles written: yes.*\"code\":\"studio_summary\"[^\n]*\"scene\":\"tycoon.styles\",[^\n]*\"applied\":1,\"refused\":0,\"undone\":1,\"redone\":1,\"undoable\":1,\"redoable\":0,[^\n]*\"status\":\"redone\".*\"exit\":\"clean_stop\"")
    # A style sheet's classes shown (D837): Tycoon's four, each with a
    # sample of each variant it sets, thirteen in all; the field's hovered
    # one pressed chooses the field class.
    add_test(NAME studio_style_preview
             COMMAND ${RAWFRAME_SHELL} ${PROJECT_SOURCE_DIR}/tools/xvfb_run.sh -s "-screen 0 1280x720x24 -br" ${RAWFRAME_SHELL}
                     ${CMAKE_CURRENT_SOURCE_DIR}/tests/clicked.sh $<TARGET_FILE:rawframe_host_studio_tool>
                     games/tycoon/tycoon.game tycoon.styles @tycoon.styles "@field hovered" wait=1
             WORKING_DIRECTORY ${PROJECT_SOURCE_DIR})
    set_tests_properties(studio_style_preview PROPERTIES
                         SKIP_REGULAR_EXPRESSION "skip: no device"
                         PASS_REGULAR_EXPRESSION
                         "found field hovered at [0-9]+,[0-9]+.*\"code\":\"studio_style_preview\"[^\n]*\"classes\":4,\"swatches\":13}.*\"code\":\"studio_summary\"[^\n]*\"scene\":\"tycoon.styles\",[^\n]*\"entity\":\"139f1a51051d860e\",.*\"exit\":\"clean_stop\"")
    # A data document edited in Studio (D812): Quarry's action map listed
    # with its documents and chosen, its aim action chosen, and its shown
    # name typed, set through the session's data.set and written; Undo and
    # Redo leave it standing. Quarry, whose two documents stay in view of
    # the Assets dock, where Plaza's sixteen run past it.
    # The game's settings edited in Studio (D818): Quarry's description
    # listed as Project settings and chosen, its mapped line chosen and
    # typed, set through the session's lines.set and written; Undo and Redo
    # leave it standing. A group in view: Quarry's sixteen run past the
    # Hierarchy.
    add_test(NAME studio_sets_settings
             COMMAND ${RAWFRAME_SHELL} ${PROJECT_SOURCE_DIR}/tools/xvfb_run.sh -s "-screen 0 1280x720x24 -br" ${RAWFRAME_SHELL}
                     ${CMAKE_CURRENT_SOURCE_DIR}/tests/clicked.sh $<TARGET_FILE:rawframe_host_studio_tool>
                     games/quarry/quarry.game quarry.game "@Project settings" @mapped "@mapped#3:8192"
                     @Undo @Redo "until=studio_said,status=\"redone\""
             WORKING_DIRECTORY ${PROJECT_SOURCE_DIR})
    set_tests_properties(studio_sets_settings PROPERTIES
                         SKIP_REGULAR_EXPRESSION "skip: no device"
                         PASS_REGULAR_EXPRESSION
                         "typed 8192 at [0-9]+,[0-9]+.*quarry.game written: yes.*\"code\":\"studio_summary\"[^\n]*\"scene\":\"quarry.game\",[^\n]*\"applied\":1,\"refused\":0,\"undone\":1,\"redone\":1,[^\n]*\"status\":\"redone\".*\"exit\":\"clean_stop\"")
    add_test(NAME studio_edits_data
             COMMAND ${RAWFRAME_SHELL} ${PROJECT_SOURCE_DIR}/tools/xvfb_run.sh -s "-screen 0 1280x720x24 -br" ${RAWFRAME_SHELL}
                     ${CMAKE_CURRENT_SOURCE_DIR}/tests/clicked.sh $<TARGET_FILE:rawframe_host_studio_tool>
                     games/quarry/quarry.game quarry.actions @quarry.actions "@  aim (actions)" "@displayName#2:Walk"
                     @Undo @Redo "until=studio_said,status=\"redone\""
             WORKING_DIRECTORY ${PROJECT_SOURCE_DIR})
    set_tests_properties(studio_edits_data PROPERTIES
                         SKIP_REGULAR_EXPRESSION "skip: no device"
                         PASS_REGULAR_EXPRESSION
                         "typed Walk at [0-9]+,[0-9]+.*quarry.actions written: yes.*\"code\":\"studio_summary\"[^\n]*\"scene\":\"quarry.actions\",[^\n]*\"applied\":1,\"refused\":0,\"undone\":1,\"redone\":1,[^\n]*\"status\":\"redone\".*\"exit\":\"clean_stop\"")
    # A material instance edited in Studio (D722): Gale's shore listed after
    # its UI document and its rock's graph (D730), its tint chosen among the
    # rock's inputs and typed as three numbers, set through the session's
    # materials.set_override and written; Undo and Redo leave it standing.
    add_test(NAME studio_material
             COMMAND ${RAWFRAME_SHELL} ${PROJECT_SOURCE_DIR}/tools/xvfb_run.sh -s "-screen 0 1280x720x24 -br" ${RAWFRAME_SHELL}
                     ${CMAKE_CURRENT_SOURCE_DIR}/tests/clicked.sh $<TARGET_FILE:rawframe_host_studio_tool>
                     games/gale/gale.game shore.rfmaterial @shore.rfmaterial @tint "@override#2:[0.6,0.2,0.1]" @Undo @Redo
                     "until=studio_said,status=\"redone\""
             WORKING_DIRECTORY ${PROJECT_SOURCE_DIR})
    set_tests_properties(studio_material PROPERTIES
                         SKIP_REGULAR_EXPRESSION "skip: no device"
                         PASS_REGULAR_EXPRESSION
                         "typed \\[0.6,0.2,0.1\\] at [0-9]+,[0-9]+.*shore.rfmaterial written: yes.*\"status\":\"tint set to \\[0.6,0.2,0.1\\]\".*\"code\":\"studio_summary\"[^\n]*\"scenes\":8,\"scene\":\"shore.rfmaterial\",\"entities\":2,\"entity\":\"tint\",[^\n]*\"applied\":1,\"refused\":0,\"undone\":1,\"redone\":1,\"undoable\":1,\"redoable\":0,[^\n]*\"status\":\"redone\".*\"exit\":\"clean_stop\"")
    # A material graph edited in Studio (D730): Gale's rock listed, its
    # surface node chosen among its five, and its roughness typed, set
    # through the session's materials.set_node_input and written; Undo and
    # Redo leave it standing. The surface shows each of the Surface
    # contract's inputs, the roughness 17th (D732).
    add_test(NAME studio_graph
             COMMAND ${RAWFRAME_SHELL} ${PROJECT_SOURCE_DIR}/tools/xvfb_run.sh -s "-screen 0 1280x720x24 -br" ${RAWFRAME_SHELL}
                     ${CMAKE_CURRENT_SOURCE_DIR}/tests/clicked.sh $<TARGET_FILE:rawframe_host_studio_tool>
                     games/gale/gale.game rock.rfmaterial @rock.rfmaterial "@60c00000  rawframe/surface@1" "@specular_roughness#2:0.4" @Undo @Redo
                     "until=studio_said,status=\"redone\""
             WORKING_DIRECTORY ${PROJECT_SOURCE_DIR})
    set_tests_properties(studio_graph PROPERTIES
                         SKIP_REGULAR_EXPRESSION "skip: no device"
                         PASS_REGULAR_EXPRESSION
                         "typed 0.4 at [0-9]+,[0-9]+.*rock.rfmaterial written: yes.*\"status\":\"specular_roughness set to 0.4\".*\"code\":\"studio_summary\"[^\n]*\"scene\":\"rock.rfmaterial\",\"entities\":5,\"entity\":\"60c0000000000005\",[^\n]*\"applied\":1,\"refused\":0,\"undone\":1,\"redone\":1,[^\n]*\"status\":\"redone\".*\"exit\":\"clean_stop\"")
    # A material graph on the canvas (D819): Gale's rock's five nodes laid
    # out in columns with its four wires, its triplanar projection chosen by
    # its node there, and its tiling typed, set and written; Undo and Redo
    # leave it standing.
    add_test(NAME studio_graph_canvas
             COMMAND ${RAWFRAME_SHELL} ${PROJECT_SOURCE_DIR}/tools/xvfb_run.sh -s "-screen 0 1280x720x24 -br" ${RAWFRAME_SHELL}
                     ${CMAKE_CURRENT_SOURCE_DIR}/tests/clicked.sh $<TARGET_FILE:rawframe_host_studio_tool>
                     games/gale/gale.game rock.rfmaterial @rock.rfmaterial "@triplanar_projection 60c0000000000003" "@tile#2:0.5"
                     @Undo @Redo "until=studio_said,status=\"redone\""
             WORKING_DIRECTORY ${PROJECT_SOURCE_DIR})
    set_tests_properties(studio_graph_canvas PROPERTIES
                         SKIP_REGULAR_EXPRESSION "skip: no device"
                         PASS_REGULAR_EXPRESSION
                         "typed 0.5 at [0-9]+,[0-9]+.*rock.rfmaterial written: yes.*\"code\":\"studio_canvas\"[^\n]*\"nodes\":5,\"wires\":4,\"columns\":[2-9].*\"status\":\"tile set to 0.5\".*\"code\":\"studio_summary\"[^\n]*\"scene\":\"rock.rfmaterial\",\"entities\":5,\"entity\":\"60c0000000000003\",[^\n]*\"applied\":1,\"refused\":0,\"undone\":1,\"redone\":1,[^\n]*\"status\":\"redone\".*\"exit\":\"clean_stop\"")
    # An animation graph on the canvas (D819): Tycoon's amble, a data
    # document, its five nodes and four wires laid out, its blend space
    # chosen by its node there. The browser narrowed to Animations and
    # turned down first: Tycoon's clips come before the graph, past the
    # Assets dock.
    add_test(NAME studio_animation_canvas
             COMMAND ${RAWFRAME_SHELL} ${PROJECT_SOURCE_DIR}/tools/xvfb_run.sh -s "-screen 0 1280x720x24 -br" ${RAWFRAME_SHELL}
                     ${CMAKE_CURRENT_SOURCE_DIR}/tests/clicked.sh $<TARGET_FILE:rawframe_host_studio_tool>
                     games/tycoon/tycoon.game amble.rfanim @Animations wheel=457,650,4 @amble.rfanim "@blend_space_1d 51a0000000000004" wait=1
             WORKING_DIRECTORY ${PROJECT_SOURCE_DIR})
    set_tests_properties(studio_animation_canvas PROPERTIES
                         SKIP_REGULAR_EXPRESSION "skip: no device"
                         PASS_REGULAR_EXPRESSION
                         "found blend_space_1d 51a0000000000004 at [0-9]+,[0-9]+.*amble.rfanim written: no.*\"code\":\"studio_canvas\"[^\n]*\"nodes\":5,\"wires\":4,\"columns\":3}.*\"code\":\"studio_summary\"[^\n]*\"scene\":\"amble.rfanim\",[^\n]*\"entity\":\"graph.51a0000000000004\",.*\"exit\":\"clean_stop\"")
    # A clip on its timeline (D827): Tycoon's walk, its six tracks named by
    # their skeleton's bones, its keys and its two footsteps marks at their
    # times; the hips' second translation key pressed on the timeline and
    # moved to a quarter second by its row's field (`data.set` through the
    # key's place), then pressed again where it went; the hips' track drawn
    # as a curve of three values under the lanes (D836).
    add_test(NAME studio_edits_clip
             COMMAND ${RAWFRAME_SHELL} ${PROJECT_SOURCE_DIR}/tools/xvfb_run.sh -s "-screen 0 1280x720x24 -br" ${RAWFRAME_SHELL}
                     ${CMAKE_CURRENT_SOURCE_DIR}/tests/clicked.sh $<TARGET_FILE:rawframe_host_studio_tool>
                     games/tycoon/tycoon.game amble/walk.rfanim @Animations wheel=457,650,4 @amble/walk.rfanim
                     "@hips translation at 0.5" "@time#2:0.25" "until=studio_said,status=\"tracks[0].keys[1].time set"
                     "@hips translation at 0.25" wait=1
             WORKING_DIRECTORY ${PROJECT_SOURCE_DIR})
    set_tests_properties(studio_edits_clip PROPERTIES
                         SKIP_REGULAR_EXPRESSION "skip: no device"
                         PASS_REGULAR_EXPRESSION
                         "typed 0.25 at [0-9]+,[0-9]+.*found hips translation at 0.25 at.*amble/walk.rfanim written: yes.*\"code\":\"studio_curve\"[^\n]*\"track\":0,\"values\":3,\"samples\":160}.*\"code\":\"studio_timeline\"[^\n]*\"tracks\":6,\"keys\":11,\"events\":2,\"markers\":0,\"named\":7,\"marks\":13}.*\"code\":\"studio_summary\"[^\n]*\"scene\":\"amble/walk.rfanim\",[^\n]*\"entity\":\"tracks.0..keys.1.\",[^\n]*\"applied\":1,\"refused\":0,.*\"exit\":\"clean_stop\"")
    # A node the surface reads is not deleted: the session refuses what
    # leaves a graph that does not read (D728), and Studio says so.
    add_test(NAME studio_graph_keeps
             COMMAND ${RAWFRAME_SHELL} ${PROJECT_SOURCE_DIR}/tools/xvfb_run.sh -s "-screen 0 1280x720x24 -br" ${RAWFRAME_SHELL}
                     ${CMAKE_CURRENT_SOURCE_DIR}/tests/clicked.sh $<TARGET_FILE:rawframe_host_studio_tool>
                     games/gale/gale.game rock.rfmaterial @rock.rfmaterial "@60c00000  rawframe/multiply@1" @Delete
                     "until=studio_said,status=\"the material graph does not read: a graph's connections are to its nodes\""
             WORKING_DIRECTORY ${PROJECT_SOURCE_DIR})
    set_tests_properties(studio_graph_keeps PROPERTIES
                         SKIP_REGULAR_EXPRESSION "skip: no device"
                         PASS_REGULAR_EXPRESSION
                         "clicked at [0-9]+,[0-9]+.*rock.rfmaterial written: no.*\"status\":\"the material graph does not read: a graph's connections are to its nodes\".*\"code\":\"studio_summary\"[^\n]*\"applied\":0,\"refused\":1,.*\"exit\":\"clean_stop\"")
    # A material graph's node made in Studio (D731): in Gale's rock New
    # lists the node types the material module holds, a uv node is made
    # and chosen, and its channel, which its type's signature offers though
    # the node holds none (D732), typed, set through the session's
    # materials.set_node_param and written.
    add_test(NAME studio_graph_makes
             COMMAND ${RAWFRAME_SHELL} ${PROJECT_SOURCE_DIR}/tools/xvfb_run.sh -s "-screen 0 1280x720x24 -br" ${RAWFRAME_SHELL}
                     ${CMAKE_CURRENT_SOURCE_DIR}/tests/clicked.sh $<TARGET_FILE:rawframe_host_studio_tool>
                     games/gale/gale.game rock.rfmaterial @rock.rfmaterial @New @rawframe/uv@1 "@channel#2:1"
                     "until=studio_said,status=\"channel set to 1\""
             WORKING_DIRECTORY ${PROJECT_SOURCE_DIR})
    set_tests_properties(studio_graph_makes PROPERTIES
                         ENVIRONMENT "STUDIO_SHOW_WRITTEN=1"
                         SKIP_REGULAR_EXPRESSION "skip: no device"
                         PASS_REGULAR_EXPRESSION
                         "clicked at [0-9]+,[0-9]+.*clicked at [0-9]+,[0-9]+.*typed 1 at [0-9]+,[0-9]+.*rock.rfmaterial written: yes.*\"type\": \"rawframe/uv@1\".*\"code\":\"studio_summary\"[^\n]*\"scene\":\"rock.rfmaterial\",\"entities\":6,[^\n]*\"applied\":2,\"refused\":0,[^\n]*\"status\":\"channel set to 1\".*\"exit\":\"clean_stop\"")
    # A material graph's nodes connected in Studio (D733): in Gale's rock
    # the tint's parameter node chosen and its out pressed, then the
    # surface chosen and its emission color's field pressed, which takes a
    # connection from it through the session's materials.set_node_input,
    # written.
    add_test(NAME studio_graph_connects
             COMMAND ${RAWFRAME_SHELL} ${PROJECT_SOURCE_DIR}/tools/xvfb_run.sh -s "-screen 0 1280x720x24 -br" ${RAWFRAME_SHELL}
                     ${CMAKE_CURRENT_SOURCE_DIR}/tests/clicked.sh $<TARGET_FILE:rawframe_host_studio_tool>
                     games/gale/gale.game rock.rfmaterial @rock.rfmaterial "@60c00000  rawframe/parameter@1" @out "@60c00000  rawframe/surface@1" "@emission_color#2"
                     "until=studio_said,status=\"emission_color connected\""
             WORKING_DIRECTORY ${PROJECT_SOURCE_DIR})
    set_tests_properties(studio_graph_connects PROPERTIES
                         ENVIRONMENT "STUDIO_SHOW_WRITTEN=1"
                         SKIP_REGULAR_EXPRESSION "skip: no device"
                         PASS_REGULAR_EXPRESSION
                         "clicked at [0-9]+,[0-9]+.*clicked at [0-9]+,[0-9]+.*clicked at [0-9]+,[0-9]+.*rock.rfmaterial written: yes.*\"emission_color\": {[ \n]*\"node\": \"60c0000000000001\",[ \n]*\"output\": \"out\".*\"code\":\"studio_summary\"[^\n]*\"entity\":\"60c0000000000005\",[^\n]*\"applied\":1,\"refused\":0,[^\n]*\"status\":\"emission_color connected\".*\"exit\":\"clean_stop\"")
    # A style class made, named, and deleted in Studio (D638): in Stalls'
    # sheet New makes a class and chooses it, its name typed as tip, and
    # Delete deletes it; Undo brings it back, so the sheet is written with
    # it.
    add_test(NAME studio_style_classes
             COMMAND ${RAWFRAME_SHELL} ${PROJECT_SOURCE_DIR}/tools/xvfb_run.sh -s "-screen 0 1280x720x24 -br" ${RAWFRAME_SHELL}
                     ${CMAKE_CURRENT_SOURCE_DIR}/tests/clicked.sh $<TARGET_FILE:rawframe_host_studio_tool>
                     games/tycoon/tycoon.game tycoon.styles @tycoon.styles @New "@name#2:tip" @Delete @Undo
                     "until=studio_said,status=\"undone\""
             WORKING_DIRECTORY ${PROJECT_SOURCE_DIR})
    set_tests_properties(studio_style_classes PROPERTIES
                         SKIP_REGULAR_EXPRESSION "skip: no device"
                         PASS_REGULAR_EXPRESSION
                         "clicked at [0-9]+,[0-9]+.*typed tip at [0-9]+,[0-9]+.*clicked at [0-9]+,[0-9]+.*clicked at [0-9]+,[0-9]+.*tycoon.styles written: yes.*\"code\":\"studio_summary\"[^\n]*\"scene\":\"tycoon.styles\",\"entities\":5,[^\n]*\"applied\":3,\"refused\":0,\"undone\":1,\"redone\":0,\"undoable\":2,\"redoable\":1,[^\n]*\"status\":\"undone\".*\"exit\":\"clean_stop\"")
    # A style class's transitions in Studio (D639): the button's, under its
    # looks at the column's end, cleared, so the class has none.
    add_test(NAME studio_style_transitions
             COMMAND ${RAWFRAME_SHELL} ${PROJECT_SOURCE_DIR}/tools/xvfb_run.sh -s "-screen 0 1280x720x24 -br" ${RAWFRAME_SHELL}
                     ${CMAKE_CURRENT_SOURCE_DIR}/tests/clicked.sh $<TARGET_FILE:rawframe_host_studio_tool>
                     games/tycoon/tycoon.game tycoon.styles @tycoon.styles @button wheel=1000,400,30 "@all#2" keys=BackSpace,Return
                     "until=studio_said,status=\"transitions unset\""
             WORKING_DIRECTORY ${PROJECT_SOURCE_DIR})
    set_tests_properties(studio_style_transitions PROPERTIES
                         SKIP_REGULAR_EXPRESSION "skip: no device"
                         PASS_REGULAR_EXPRESSION
                         "clicked at [0-9]+,[0-9]+.*pressed BackSpace,Return.*tycoon.styles written: yes.*\"code\":\"studio_summary\"[^\n]*\"applied\":1,\"refused\":0,[^\n]*\"status\":\"transitions unset\".*\"exit\":\"clean_stop\"")
    # A game's UI document edited in Studio (D643): Stalls' listed after its
    # sheet and chosen, its purse's width typed, its tip deleted, and the
    # delete undone, so the document is written with the width.
    # A UI document laid out as its game would show it (D832): Tycoon's
    # ten nodes on a screen in the center, its sign pressed there and its
    # row chosen, its width typed, and the screen laid out again.
    # The environment panel (D834): Plaza's gate scene holds none of its
    # sky or warmth, which the panel offers; the sky added as an entity of
    # its own, then found there and pressed to choose it.
    add_test(NAME studio_environment
             COMMAND ${RAWFRAME_SHELL} ${PROJECT_SOURCE_DIR}/tools/xvfb_run.sh -s "-screen 0 1280x720x24 -br" ${RAWFRAME_SHELL}
                     ${CMAKE_CURRENT_SOURCE_DIR}/tests/clicked.sh $<TARGET_FILE:rawframe_host_studio_tool>
                     games/plaza/plaza.game gate.scene @gate.scene @Environment "@  Add plaza.sky"
                     "until=studio_said,status=\"plaza.sky added" "@  plaza.sky plaza.sky" wait=1
             WORKING_DIRECTORY ${PROJECT_SOURCE_DIR})
    set_tests_properties(studio_environment PROPERTIES
                         SKIP_REGULAR_EXPRESSION "skip: no device"
                         PASS_REGULAR_EXPRESSION
                         "found   plaza.sky plaza.sky at [0-9]+,[0-9]+.*gate.scene written: yes.*\"code\":\"studio_environment\"[^\n]*\"components\":[2-9],\"held\":0,.*\"code\":\"studio_environment\"[^\n]*\"held\":1,.*\"code\":\"studio_summary\"[^\n]*\"scene\":\"gate.scene\",[^\n]*\"entity\":\"[0-9a-f-]+\",[^\n]*\"applied\":1,\"refused\":0,.*\"exit\":\"clean_stop\"")
    add_test(NAME studio_ui_preview
             COMMAND ${RAWFRAME_SHELL} ${PROJECT_SOURCE_DIR}/tools/xvfb_run.sh -s "-screen 0 1280x720x24 -br" ${RAWFRAME_SHELL}
                     ${CMAKE_CURRENT_SOURCE_DIR}/tests/clicked.sh $<TARGET_FILE:rawframe_host_studio_tool>
                     games/tycoon/tycoon.game tycoon.ui @tycoon.ui "@tycoon.sign preview" "@widthOffset#2:300" wait=1
             WORKING_DIRECTORY ${PROJECT_SOURCE_DIR})
    set_tests_properties(studio_ui_preview PROPERTIES
                         SKIP_REGULAR_EXPRESSION "skip: no device"
                         PASS_REGULAR_EXPRESSION
                         "found tycoon.sign preview at [0-9]+,[0-9]+.*typed 300 at.*tycoon.ui written: yes.*\"code\":\"studio_ui_preview\"[^\n]*\"nodes\":10,\"laid\":10,\"pressable\":10,\"classes\":[1-9][0-9]*}.*\"code\":\"studio_ui_preview\"[^\n]*\"nodes\":10,.*\"code\":\"studio_summary\"[^\n]*\"scene\":\"tycoon.ui\",[^\n]*\"entity\":\"tycoon.sign\",[^\n]*\"applied\":1,\"refused\":0,.*\"exit\":\"clean_stop\"")
    add_test(NAME studio_ui
             COMMAND ${RAWFRAME_SHELL} ${PROJECT_SOURCE_DIR}/tools/xvfb_run.sh -s "-screen 0 1280x720x24 -br" ${RAWFRAME_SHELL}
                     ${CMAKE_CURRENT_SOURCE_DIR}/tests/clicked.sh $<TARGET_FILE:rawframe_host_studio_tool>
                     games/tycoon/tycoon.game tycoon.ui @tycoon.ui "@  tycoon.purse" "@widthOffset#2:300" "@  tycoon.tip" @Delete @Undo
                     "until=studio_said,status=\"undone\""
             WORKING_DIRECTORY ${PROJECT_SOURCE_DIR})
    set_tests_properties(studio_ui PROPERTIES
                         SKIP_REGULAR_EXPRESSION "skip: no device"
                         PASS_REGULAR_EXPRESSION
                         "typed 300 at [0-9]+,[0-9]+.*clicked at [0-9]+,[0-9]+.*clicked at [0-9]+,[0-9]+.*clicked at [0-9]+,[0-9]+.*tycoon.ui written: yes.*\"code\":\"studio_summary\"[^\n]*\"scene\":\"tycoon.ui\",[^\n]*\"applied\":2,\"refused\":0,\"undone\":1,\"redone\":0,\"undoable\":1,\"redoable\":1,[^\n]*\"status\":\"undone\".*\"exit\":\"clean_stop\"")
    # A UI node moved among its siblings (D644): Stalls' purse, first in
    # its wallet, is refused Up and moved Down, after the tip, staying
    # chosen.
    add_test(NAME studio_ui_moves
             COMMAND ${RAWFRAME_SHELL} ${PROJECT_SOURCE_DIR}/tools/xvfb_run.sh -s "-screen 0 1280x720x24 -br" ${RAWFRAME_SHELL}
                     ${CMAKE_CURRENT_SOURCE_DIR}/tests/clicked.sh $<TARGET_FILE:rawframe_host_studio_tool>
                     games/tycoon/tycoon.game tycoon.ui @tycoon.ui "@  tycoon.purse" @Up @Down "until=studio_said,status=\"moved down\""
             WORKING_DIRECTORY ${PROJECT_SOURCE_DIR})
    set_tests_properties(studio_ui_moves PROPERTIES
                         SKIP_REGULAR_EXPRESSION "skip: no device"
                         PASS_REGULAR_EXPRESSION
                         "clicked at [0-9]+,[0-9]+.*clicked at [0-9]+,[0-9]+.*tycoon.ui written: yes.*\"code\":\"studio_summary\"[^\n]*\"entity\":\"tycoon.purse\",[^\n]*\"applied\":1,\"refused\":1,[^\n]*\"status\":\"moved down\".*\"exit\":\"clean_stop\"")
    # A UI node moved into another (D671): Stalls' wallet is refused a
    # move into its own purse, and the purse, Move to pressed and the
    # sign's row clicked, is moved into the sign, last among its children,
    # staying chosen.
    add_test(NAME studio_ui_moves_into
             COMMAND ${RAWFRAME_SHELL} ${PROJECT_SOURCE_DIR}/tools/xvfb_run.sh -s "-screen 0 1280x720x24 -br" ${RAWFRAME_SHELL}
                     ${CMAKE_CURRENT_SOURCE_DIR}/tests/clicked.sh $<TARGET_FILE:rawframe_host_studio_tool>
                     games/tycoon/tycoon.game tycoon.ui @tycoon.ui @tycoon.wallet "@Move to" "@  tycoon.purse" "@  tycoon.purse" "@Move to" @tycoon.sign
                     "until=studio_said,status=\"moved into tycoon.sign\""
             WORKING_DIRECTORY ${PROJECT_SOURCE_DIR})
    set_tests_properties(studio_ui_moves_into PROPERTIES
                         ENVIRONMENT "STUDIO_SHOW_WRITTEN=1"
                         SKIP_REGULAR_EXPRESSION "skip: no device"
                         PASS_REGULAR_EXPRESSION
                         "clicked at [0-9]+,[0-9]+.*clicked at [0-9]+,[0-9]+.*tycoon.ui written: yes.*\"component\": \"tycoon.sign\",[^]]*\"children\": \\[[ \n]*{[ \n]*\"component\": \"tycoon.purse\".*\"code\":\"studio_summary\"[^\n]*\"entity\":\"tycoon.purse\",[^\n]*\"applied\":1,\"refused\":1,[^\n]*\"status\":\"moved into tycoon.sign\".*\"exit\":\"clean_stop\"")
    # A UI node made in Studio (D672): Stalls' tip deleted, New offers its
    # component alone, the only node component the document no longer
    # holds, and its row pressed makes it at the document's top, chosen.
    add_test(NAME studio_ui_makes
             COMMAND ${RAWFRAME_SHELL} ${PROJECT_SOURCE_DIR}/tools/xvfb_run.sh -s "-screen 0 1280x720x24 -br" ${RAWFRAME_SHELL}
                     ${CMAKE_CURRENT_SOURCE_DIR}/tests/clicked.sh $<TARGET_FILE:rawframe_host_studio_tool>
                     games/tycoon/tycoon.game tycoon.ui @tycoon.ui "@  tycoon.tip" @Delete @New @tycoon.tip
                     "until=studio_said,status=\"tycoon.tip made\""
             WORKING_DIRECTORY ${PROJECT_SOURCE_DIR})
    set_tests_properties(studio_ui_makes PROPERTIES
                         ENVIRONMENT "STUDIO_SHOW_WRITTEN=1"
                         SKIP_REGULAR_EXPRESSION "skip: no device"
                         PASS_REGULAR_EXPRESSION
                         "clicked at [0-9]+,[0-9]+.*clicked at [0-9]+,[0-9]+.*tycoon.ui written: yes.*\"component\": \"tycoon.tip\"[ \n]*}[ \n]*\\][ \n]*}.*\"code\":\"studio_summary\"[^\n]*\"entity\":\"tycoon.tip\",[^\n]*\"applied\":2,\"refused\":0,[^\n]*\"status\":\"tycoon.tip made\".*\"exit\":\"clean_stop\"")
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
                         ${PROJECT_BINARY_DIR}/hosts/arena/plaza_server.conf @gate.scene until=studio_previewing
                         "@eye#2:0,30,0.5" "until=studio_said,status=\"view set and previewed\""
                 WORKING_DIRECTORY ${PROJECT_SOURCE_DIR})
        set_tests_properties(studio_previews_plaza PROPERTIES
                             FIXTURES_REQUIRED plaza_content
                             SKIP_REGULAR_EXPRESSION "skip: no device"
                             PASS_REGULAR_EXPRESSION
                             "clicked at [0-9]+,[0-9]+.*saw studio_previewing.*typed 0,30,0.5 at [0-9]+,[0-9]+.*\"code\":\"studio_summary\"[^\n]*\"scene\":\"gate.scene\",[^\n]*\"viewsSet\":1,[^\n]*\"previewing\":true,[^\n]*\"status\":\"view set and previewed\".*\"code\":\"scene_summary\"[^\n]*\"framesPreviewed\":[1-9][0-9]*,")
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
                         games/plaza/plaza.game ${PROJECT_BINARY_DIR}/hosts/arena/plaza_server.conf
                         ${PROJECT_BINARY_DIR}/hosts/client/plaza_drawn.conf @gate.scene @Play until=studio_previewing "@eye#2:0,30,0.5"
                         wait=3 @New wait=5 @Stop until=studio_game_ended
                 WORKING_DIRECTORY ${PROJECT_SOURCE_DIR})
        set_tests_properties(studio_plays_plaza PROPERTIES
                             FIXTURES_REQUIRED plaza_content
                             SKIP_REGULAR_EXPRESSION "skip: no device"
                             PASS_REGULAR_EXPRESSION
                             "clicked at [0-9]+,[0-9]+.*saw studio_previewing.*typed 0,30,0.5 at [0-9]+,[0-9]+.*clicked at [0-9]+,[0-9]+.*\"code\":\"studio_summary\"[^\n]*\"applied\":1,[^\n]*\"viewsSet\":1,[^\n]*\"previewing\":false,\"played\":1,\"playing\":false,[^\n]*\"status\":\"game stopped\".*\"code\":\"scene_summary\"[^\n]*\"framesPreviewed\":[1-9][0-9]*,.*\"code\":\"scene_followed\"[^\n]*\"scene\":\"gate.scene\",\"created\":1.*{\"endpoint\":\"127.0.0.1:[0-9]+\",\"pinFile\":\"[^\"]*/server.fingerprint\",\"tokenFile\":\"[^\"]*/token\"}")
        # Played in the viewport (D789): the client's window kept hidden, its
        # pictures drawn at the viewport's size and streamed to Studio,
        # which shows them in the center; and a click in the viewport handed
        # to the hidden client as its own window's (D790), which keeps it as
        # a ray the server picks the gate's post with, as studio_picks_plaza
        # clicks the client's own window.
        add_test(NAME studio_plays_in_viewport
                 COMMAND ${CMAKE_COMMAND} -E env "STUDIO_SETTINGS=studio.play_in_viewport = true"
                         ${RAWFRAME_SHELL} ${PROJECT_SOURCE_DIR}/tools/xvfb_run.sh -s "-screen 0 1280x720x24 -br" ${RAWFRAME_SHELL}
                         ${CMAKE_CURRENT_SOURCE_DIR}/tests/plays.sh $<TARGET_FILE:rawframe_host_studio_tool>
                         $<TARGET_FILE:rawframe_host_dedicated_server> $<TARGET_FILE:rawframe_host_client>
                         games/plaza/plaza.game ${PROJECT_BINARY_DIR}/hosts/arena/plaza_server.conf
                         ${PROJECT_BINARY_DIR}/hosts/client/plaza_drawn.conf @gate.scene @Play until=studio_previewing
                         "@eye#2:-30,3,-20" "@target#2:-30,2.5,-30" wait=3 @Viewport "until=studio_said,status=\"picked "
                         @Stop until=studio_game_ended
                 WORKING_DIRECTORY ${PROJECT_SOURCE_DIR})
        set_tests_properties(studio_plays_in_viewport PROPERTIES
                             FIXTURES_REQUIRED plaza_content
                             SKIP_REGULAR_EXPRESSION "skip: no device"
                             PASS_REGULAR_EXPRESSION
                             "\"code\":\"studio_summary\"[^\n]*\"entity\":\"6c632219-53a9-4af4-8781-bb2a8f4fa53b\",[^\n]*\"picked\":1,.*\"code\":\"studio_viewport\"[^\n]*\"inViewport\":true,\"pictures\":[1-9][0-9]*,\"inputs\":[1-9][0-9]*,\"inputsTaken\":[1-9][0-9]*,\"width\":[1-9][0-9]+,\"height\":[1-9][0-9]+,.*\"code\":\"frame_summary\"[^\n]*\"window\":false,[^\n]*\"streamed\":[1-9][0-9]*,\"streamFailures\":0}")
        # A layer hidden in the viewport (D791): the game played in the
        # viewport with layers replicated, the gate's post put on layer 1
        # as it plays (the scene written, which the server takes), and
        # Hide on the Layers' layer 1 told the preview, which takes it.
        # Whether the post's model is then left out is render_scene's test:
        # the gate lies past the played player's interest, so its client
        # holds none of it.
        add_test(NAME studio_hides_layer
                 COMMAND ${CMAKE_COMMAND} -E env "STUDIO_SETTINGS=studio.play_in_viewport = true"
                         ${RAWFRAME_SHELL} ${PROJECT_SOURCE_DIR}/tools/xvfb_run.sh -s "-screen 0 1280x720x24 -br" ${RAWFRAME_SHELL}
                         ${CMAKE_CURRENT_SOURCE_DIR}/tests/plays.sh $<TARGET_FILE:rawframe_host_studio_tool>
                         $<TARGET_FILE:rawframe_host_dedicated_server> $<TARGET_FILE:rawframe_host_client>
                         games/plaza/plaza.game ${PROJECT_BINARY_DIR}/hosts/arena/plaza_server.conf
                         ${PROJECT_BINARY_DIR}/hosts/client/plaza_drawn.conf @gate.scene @Play until=studio_previewing
                         "@gate post" wheel=1100,400,20 "@add#2:rawframe.world.layer" "until=studio_said,status=\"rawframe.world.layer added"
                         wheel=1100,400,20 "@layer#2:1" "until=studio_said,status=\"layer set to 1" wait=3
                         @Layers "@Hide layer 1" "until=studio_said,status=\"layer 1 hidden" wait=3
                         @Stop until=studio_game_ended
                 WORKING_DIRECTORY ${PROJECT_SOURCE_DIR})
        set_tests_properties(studio_hides_layer PROPERTIES
                             FIXTURES_REQUIRED plaza_content
                             PROCESSORS 3
                             SKIP_REGULAR_EXPRESSION "skip: no device"
                             PASS_REGULAR_EXPRESSION
                             "saw studio_said,status=\"layer set to 1.*saw studio_said,status=\"layer 1 hidden.*\"code\":\"studio_selection\"[^\n]*\"hiddenTold\":[1-9].*\"code\":\"scene_followed\"[^\n]*\"scene\":\"gate.scene\",\"created\":0,\"destroyed\":0,\"changed\":1")
        # The Console (D778): Plaza played, the Console shown, its Server
        # lines hidden and shown again, and the game stopped; what Studio
        # said and the records its server and client logged reach its
        # lines.
        add_test(NAME studio_console
                 COMMAND ${RAWFRAME_SHELL} ${PROJECT_SOURCE_DIR}/tools/xvfb_run.sh -s "-screen 0 1280x720x24 -br" ${RAWFRAME_SHELL}
                         ${CMAKE_CURRENT_SOURCE_DIR}/tests/plays.sh $<TARGET_FILE:rawframe_host_studio_tool>
                         $<TARGET_FILE:rawframe_host_dedicated_server> $<TARGET_FILE:rawframe_host_client>
                         games/plaza/plaza.game ${PROJECT_BINARY_DIR}/hosts/arena/plaza_server.conf
                         ${PROJECT_BINARY_DIR}/hosts/client/plaza_drawn.conf @gate.scene @Play until=studio_previewing
                         @Console wait=2 @Server wait=1 @Server "@Problems only" @Stop until=studio_game_ended
                 WORKING_DIRECTORY ${PROJECT_SOURCE_DIR})
        set_tests_properties(studio_console PROPERTIES
                             FIXTURES_REQUIRED plaza_content
                             SKIP_REGULAR_EXPRESSION "skip: no device"
                             PASS_REGULAR_EXPRESSION
                             "found Console at.*found Server at.*found Server at.*found Problems only at.*\"code\":\"studio_summary\"[^\n]*\"played\":1,[^\n]*\"consoleLines\":([2-9][0-9]|[1-9][0-9][0-9]+),.*\"exit\":\"clean_stop\"")
        # Stalls played from Studio with its UI document edited as it plays
        # (D761): the document chosen, the game played, and once the played
        # client's UI is up (its log says what navigation reaches), the
        # purse's width typed; the played client arranges its UI anew (the
        # game stopped once it says so) and gives the purse its new width.
        add_test(NAME studio_plays_ui
                 COMMAND ${RAWFRAME_SHELL} ${PROJECT_SOURCE_DIR}/tools/xvfb_run.sh -s "-screen 0 1280x720x24 -br" ${RAWFRAME_SHELL}
                         ${CMAKE_CURRENT_SOURCE_DIR}/tests/plays.sh $<TARGET_FILE:rawframe_host_studio_tool>
                         $<TARGET_FILE:rawframe_host_dedicated_server> $<TARGET_FILE:rawframe_host_client>
                         games/tycoon/tycoon.game ${PROJECT_BINARY_DIR}/hosts/client/stalls_server.conf
                         ${PROJECT_BINARY_DIR}/hosts/client/stalls_clicked.conf @tycoon.ui "@  tycoon.purse" @Play
                         until=ui_reachable@play/client.log~600 "@widthOffset#2:300" until=ui_rearranged@play/client.log~300 @Stop
                         until=studio_game_ended
                 WORKING_DIRECTORY ${PROJECT_SOURCE_DIR})
        set_tests_properties(studio_plays_ui PROPERTIES
                             FIXTURES_REQUIRED tycoon_content
                             SKIP_REGULAR_EXPRESSION "skip: no device"
                             PASS_REGULAR_EXPRESSION
                             "typed 300 at [0-9]+,[0-9]+.*\"code\":\"studio_summary\"[^\n]*\"played\":1,[^\n]*\"status\":\"game stopped\".*\"code\":\"ui_rearranged\".*\"code\":\"presented_summary\"[^\n]*\"restarted\":1,")
        # The game cooked by Studio and played from that content alone
        # (D502): Cook, its progress, then Play, the server's settings and
        # the client's naming no content; previewed, its client drawing
        # the cooked meshes and textures.
        add_test(NAME studio_cooks_plaza
                 COMMAND ${CMAKE_COMMAND} -E env STUDIO_COOK=$<TARGET_FILE:rawframe_host_cook_tool>
                         ${RAWFRAME_SHELL} ${PROJECT_SOURCE_DIR}/tools/xvfb_run.sh -s "-screen 0 1280x720x24 -br" ${RAWFRAME_SHELL}
                         ${CMAKE_CURRENT_SOURCE_DIR}/tests/plays.sh $<TARGET_FILE:rawframe_host_studio_tool>
                         $<TARGET_FILE:rawframe_host_dedicated_server> $<TARGET_FILE:rawframe_host_client>
                         games/plaza/plaza.game /dev/null ${PROJECT_BINARY_DIR}/hosts/client/plaza_drawn.conf
                         @Cook "until=studio_said,status=\"cooked " @gate.scene @Play until=studio_previewing wait=3
                         @Stop until=studio_game_ended
                 WORKING_DIRECTORY ${PROJECT_SOURCE_DIR})
        set_tests_properties(studio_cooks_plaza PROPERTIES
                             SKIP_REGULAR_EXPRESSION "skip: no device"
                             PASS_REGULAR_EXPRESSION
                             "saw studio_said,status=\"cooked .*saw studio_previewing.*\"code\":\"studio_said\"[^\n]*\"status\":\"cooked [1-9][0-9]* sources, 0 reused\".*\"code\":\"scene_summary\"[^\n]*\"gameMeshes\":[1-9][0-9]*,[^\n]*\"texturesReady\":[1-9][0-9]*,")
        # The asset browser (D799): Plaza's sources narrowed to its textures,
        # one chosen and said not cooked, then cooked by Studio and said
        # cooked; its scene left out of the list while textures are shown.
        add_test(NAME studio_browses_assets
                 COMMAND ${CMAKE_COMMAND} -E env STUDIO_COOK=$<TARGET_FILE:rawframe_host_cook_tool>
                         ${RAWFRAME_SHELL} ${PROJECT_SOURCE_DIR}/tools/xvfb_run.sh -s "-screen 0 1280x720x24 -br" ${RAWFRAME_SHELL}
                         ${CMAKE_CURRENT_SOURCE_DIR}/tests/plays.sh $<TARGET_FILE:rawframe_host_studio_tool>
                         $<TARGET_FILE:rawframe_host_dedicated_server> $<TARGET_FILE:rawframe_host_client>
                         games/plaza/plaza.game /dev/null /dev/null
                         @Textures "until=studio_said,status=\"Textures in all folders"
                         @paving.png "until=studio_said,status=\"paving.png: texture, not cooked"
                         @Cook "until=studio_said,status=\"cooked " @paving.png
                         "until=studio_said,status=\"paving.png: texture, cooked"
                 WORKING_DIRECTORY ${PROJECT_SOURCE_DIR})
        set_tests_properties(studio_browses_assets PROPERTIES
                             SKIP_REGULAR_EXPRESSION "skip: no device"
                             PASS_REGULAR_EXPRESSION
                             "saw studio_said,status=\"paving.png: texture, cooked.*\"code\":\"studio_assets\"[^\n]*\"sources\":[1-9][0-9]*,\"sourcesChosen\":2,\"narrowed\":1,\"chosen\":\"paving.png\",")
        # References (D803): Plaza cooked by Studio, the paving chosen in the
        # browser and its References shown, the game's description that reads
        # it listed and chosen from there, its own shown in turn.
        add_test(NAME studio_shows_references
                 COMMAND ${CMAKE_COMMAND} -E env STUDIO_COOK=$<TARGET_FILE:rawframe_host_cook_tool>
                         ${RAWFRAME_SHELL} ${PROJECT_SOURCE_DIR}/tools/xvfb_run.sh -s "-screen 0 1280x720x24 -br" ${RAWFRAME_SHELL}
                         ${CMAKE_CURRENT_SOURCE_DIR}/tests/plays.sh $<TARGET_FILE:rawframe_host_studio_tool>
                         $<TARGET_FILE:rawframe_host_dedicated_server> $<TARGET_FILE:rawframe_host_client>
                         games/plaza/plaza.game /dev/null /dev/null
                         @Cook "until=studio_said,status=\"cooked " @Textures
                         "until=studio_said,status=\"Textures in all folders" @paving.png
                         "until=studio_said,status=\"paving.png: texture" @References
                         "until=studio_said,status=\"paving.png: 1 references" @plaza.game
                         "until=studio_said,status=\"plaza.game: "
                 WORKING_DIRECTORY ${PROJECT_SOURCE_DIR})
        set_tests_properties(studio_shows_references PROPERTIES
                             SKIP_REGULAR_EXPRESSION "skip: no device"
                             PASS_REGULAR_EXPRESSION
                             "saw studio_said,status=\"plaza.game: .*\"code\":\"studio_assets\"[^\n]*\"referencesShown\":2,")
        # Files dropped from outside (D802): a picture dropped on Studio's
        # Assets, by XDND as a file manager drops one, imported into the
        # game, then listed among its textures.
        add_test(NAME studio_imports_dropped
                 COMMAND ${RAWFRAME_SHELL} ${PROJECT_SOURCE_DIR}/tools/xvfb_run.sh -s "-screen 0 1280x720x24 -br" ${RAWFRAME_SHELL}
                         ${CMAKE_CURRENT_SOURCE_DIR}/tests/plays.sh $<TARGET_FILE:rawframe_host_studio_tool>
                         $<TARGET_FILE:rawframe_host_dedicated_server> $<TARGET_FILE:rawframe_host_client>
                         games/plaza/plaza.game /dev/null /dev/null
                         "drop=@Assets:${PROJECT_SOURCE_DIR}/games/gale/stone.png"
                         "until=studio_said,status=\"imported " @Textures
                         "until=studio_said,status=\"Textures in all folders" @stone.png
                         "until=studio_said,status=\"stone.png: texture"
                 WORKING_DIRECTORY ${PROJECT_SOURCE_DIR})
        set_tests_properties(studio_imports_dropped PROPERTIES
                             SKIP_REGULAR_EXPRESSION "skip: no device"
                             PASS_REGULAR_EXPRESSION
                             "dropped [^\n]*stone.png at [0-9]+,[0-9]+: finished.*saw studio_said,status=\"stone.png: texture.*\"code\":\"studio_assets\"[^\n]*\"dropped\":1,\"droppedImported\":1")
        # Thumbnails (D801): Plaza cooked by Studio, its textures shown in the
        # browser with large thumbnails from that cook.
        add_test(NAME studio_shows_thumbnails
                 COMMAND ${CMAKE_COMMAND} -E env STUDIO_COOK=$<TARGET_FILE:rawframe_host_cook_tool>
                         ${RAWFRAME_SHELL} ${PROJECT_SOURCE_DIR}/tools/xvfb_run.sh -s "-screen 0 1280x720x24 -br" ${RAWFRAME_SHELL}
                         ${CMAKE_CURRENT_SOURCE_DIR}/tests/plays.sh $<TARGET_FILE:rawframe_host_studio_tool>
                         $<TARGET_FILE:rawframe_host_dedicated_server> $<TARGET_FILE:rawframe_host_client>
                         games/plaza/plaza.game /dev/null /dev/null
                         @Cook "until=studio_said,status=\"cooked " @Textures
                         "until=studio_said,status=\"Textures in all folders" "@Large thumbnails"
                         "until=studio_said,status=\"64 pixel thumbnails" wait=2
                 WORKING_DIRECTORY ${PROJECT_SOURCE_DIR})
        set_tests_properties(studio_shows_thumbnails PROPERTIES
                             SKIP_REGULAR_EXPRESSION "skip: no device"
                             PASS_REGULAR_EXPRESSION
                             "saw studio_said,status=\"64 pixel thumbnails.*\"code\":\"studio_assets\"[^\n]*\"thumbnail\":64,\"thumbnailsMade\":[1-9]")
        # Import settings (D800): Plaza's paving chosen in the browser, its
        # texture's exact set false in the Inspector, then cooked again by
        # Reimport.
        add_test(NAME studio_sets_import_settings
                 COMMAND ${CMAKE_COMMAND} -E env STUDIO_COOK=$<TARGET_FILE:rawframe_host_cook_tool>
                         ${RAWFRAME_SHELL} ${PROJECT_SOURCE_DIR}/tools/xvfb_run.sh -s "-screen 0 1280x720x24 -br" ${RAWFRAME_SHELL}
                         ${CMAKE_CURRENT_SOURCE_DIR}/tests/plays.sh $<TARGET_FILE:rawframe_host_studio_tool>
                         $<TARGET_FILE:rawframe_host_dedicated_server> $<TARGET_FILE:rawframe_host_client>
                         games/plaza/plaza.game /dev/null /dev/null
                         @Textures "until=studio_said,status=\"Textures in all folders"
                         @paving.png "until=studio_said,status=\"paving.png: texture"
                         "@exact#2:false" "until=studio_said,status=\"paving.png: exact set"
                         @Reimport "until=studio_said,status=\"cooked "
                 WORKING_DIRECTORY ${PROJECT_SOURCE_DIR})
        set_tests_properties(studio_sets_import_settings PROPERTIES
                             SKIP_REGULAR_EXPRESSION "skip: no device"
                             PASS_REGULAR_EXPRESSION
                             "saw studio_said,status=\"cooked .*\"code\":\"studio_assets\"[^\n]*\"settingsSet\":1,\"settingsRefused\":0")
        # As a phone (D809): Plaza played from Studio as a phone, its client
        # in a window of a phone's size, drawing at it, the mouse a finger.
        add_test(NAME studio_plays_as_phone
                 COMMAND ${RAWFRAME_SHELL} ${PROJECT_SOURCE_DIR}/tools/xvfb_run.sh -s "-screen 0 1280x720x24 -br" ${RAWFRAME_SHELL}
                         ${CMAKE_CURRENT_SOURCE_DIR}/tests/plays.sh $<TARGET_FILE:rawframe_host_studio_tool>
                         $<TARGET_FILE:rawframe_host_dedicated_server> $<TARGET_FILE:rawframe_host_client>
                         games/plaza/plaza.game ${PROJECT_BINARY_DIR}/hosts/arena/plaza_server.conf
                         ${PROJECT_BINARY_DIR}/hosts/client/plaza_drawn.conf @Game "@As a phone"
                         "until=studio_said,status=\"As a phone" @gate.scene @Play until=studio_previewing wait=2
                         @Stop until=studio_game_ended
                 WORKING_DIRECTORY ${PROJECT_SOURCE_DIR})
        set_tests_properties(studio_plays_as_phone PROPERTIES
                             FIXTURES_REQUIRED plaza_content
                             SKIP_REGULAR_EXPRESSION "skip: no device"
                             PASS_REGULAR_EXPRESSION
                             "\"code\":\"studio_played\"[^\n]*\"deviceWidth\":360,.*\"code\":\"frame_summary\"[^\n]*\"width\":360,\"height\":640,")
        # The live World (D808): Plaza played, the Hierarchy switched to the
        # server's World, its first entity read again and again in the
        # Inspector, then the scene again.
        add_test(NAME studio_inspects_live
                 COMMAND ${RAWFRAME_SHELL} ${PROJECT_SOURCE_DIR}/tools/xvfb_run.sh -s "-screen 0 1280x720x24 -br" ${RAWFRAME_SHELL}
                         ${CMAKE_CURRENT_SOURCE_DIR}/tests/plays.sh $<TARGET_FILE:rawframe_host_studio_tool>
                         $<TARGET_FILE:rawframe_host_dedicated_server> $<TARGET_FILE:rawframe_host_client>
                         games/plaza/plaza.game ${PROJECT_BINARY_DIR}/hosts/arena/plaza_server.conf
                         ${PROJECT_BINARY_DIR}/hosts/client/plaza_drawn.conf @gate.scene @Play until=studio_previewing
                         @Live "until=studio_said,status=\"live: " wait=3 @Live "until=studio_said,status=\"the scene again"
                         @Stop until=studio_game_ended
                 WORKING_DIRECTORY ${PROJECT_SOURCE_DIR})
        set_tests_properties(studio_inspects_live PROPERTIES
                             FIXTURES_REQUIRED plaza_content
                             SKIP_REGULAR_EXPRESSION "skip: no device"
                             PASS_REGULAR_EXPRESSION
                             "\"status\":\"live: [1-9][0-9]* entities on the server.*\"code\":\"studio_played\"[^\n]*\"live\":false,\"liveListed\":1,\"liveReads\":([2-9]|[1-9][0-9]+),\"liveComponents\":[1-9]")
        # The Network panel (D807): the lossy link chosen, Plaza played, its
        # client over QUIC at 150 ms with loss, the server's traffic read
        # and shown while the panel is, once the server counts the client
        # (a sanitized client over the lossy link took longer than a fixed
        # wait, and the client's own log said it was in before the server
        # counted it, D823), and a line for each connection (D840).
        add_test(NAME studio_shows_network
                 COMMAND ${RAWFRAME_SHELL} ${PROJECT_SOURCE_DIR}/tools/xvfb_run.sh -s "-screen 0 1280x720x24 -br" ${RAWFRAME_SHELL}
                         ${CMAKE_CURRENT_SOURCE_DIR}/tests/plays.sh $<TARGET_FILE:rawframe_host_studio_tool>
                         $<TARGET_FILE:rawframe_host_dedicated_server> $<TARGET_FILE:rawframe_host_client>
                         games/plaza/plaza.game ${PROJECT_BINARY_DIR}/hosts/arena/plaza_server.conf
                         ${PROJECT_BINARY_DIR}/hosts/client/plaza_drawn.conf @Network "@Lossy link"
                         "until=studio_said,status=\"Lossy link" @Assets @gate.scene @Play until=studio_previewing
                         @Network "until=studio_network,connections>=1~600" wait=3 @Stop
                         until=studio_game_ended
                 WORKING_DIRECTORY ${PROJECT_SOURCE_DIR})
        set_tests_properties(studio_shows_network PROPERTIES
                             FIXTURES_REQUIRED plaza_content
                             SKIP_REGULAR_EXPRESSION "skip: no device"
                             PASS_REGULAR_EXPRESSION
                             "\"code\":\"studio_played\"[^\n]*\"link\":\"Lossy link\",\"networkReads\":([2-9]|[1-9][0-9]+),\"mostConnections\":[1-9],\"connectionLines\":[1-9],.*\"latencyMs\":150,\"jitterMs\":30,\"lossPpm\":50000,")
        # The Profiler (D838): Plaza played from Studio and its Profiler
        # shown, which reads the server's last ticks once a second and says
        # how long they and each of the World's systems took, Plaza's five
        # Kest systems each its own (D841); then kept as a capture, listed,
        # and opened read only (D844).
        add_test(NAME studio_profiles
                 COMMAND ${RAWFRAME_SHELL} ${PROJECT_SOURCE_DIR}/tools/xvfb_run.sh -s "-screen 0 1280x720x24 -br" ${RAWFRAME_SHELL}
                         ${CMAKE_CURRENT_SOURCE_DIR}/tests/plays.sh $<TARGET_FILE:rawframe_host_studio_tool>
                         $<TARGET_FILE:rawframe_host_dedicated_server> $<TARGET_FILE:rawframe_host_client>
                         games/plaza/plaza.game ${PROJECT_BINARY_DIR}/hosts/arena/plaza_server.conf
                         ${PROJECT_BINARY_DIR}/hosts/client/plaza_drawn.conf @gate.scene @Play until=studio_previewing
                         @Profiler "until=studio_profile,ticks>=100~600" "@Save capture" until=studio_capture_kept
                         @Stop until=studio_game_ended
                 WORKING_DIRECTORY ${PROJECT_SOURCE_DIR})
        set_tests_properties(studio_profiles PROPERTIES
                             FIXTURES_REQUIRED plaza_content
                             SKIP_REGULAR_EXPRESSION "skip: no device"
                             PASS_REGULAR_EXPRESSION
                             "\"code\":\"studio_profile\"[^\n]*\"ticks\":1[0-9][0-9],\"systems\":[1-9][0-9]*,\"gameSystems\":5,.*\"code\":\"studio_capture_kept\"[^\n]*\"capture\":\"captures/profile-1.rfprofile\".*\"exit\":\"clean_stop\"")
        # The Frame tab (D842): Plaza played from Studio, its preview's last
        # frame read once a second while the tab shows, then kept as a
        # capture with its draw steps (D843).
        add_test(NAME studio_frame_capture
                 COMMAND ${RAWFRAME_SHELL} ${PROJECT_SOURCE_DIR}/tools/xvfb_run.sh -s "-screen 0 1280x720x24 -br" ${RAWFRAME_SHELL}
                         ${CMAKE_CURRENT_SOURCE_DIR}/tests/plays.sh $<TARGET_FILE:rawframe_host_studio_tool>
                         $<TARGET_FILE:rawframe_host_dedicated_server> $<TARGET_FILE:rawframe_host_client>
                         games/plaza/plaza.game ${PROJECT_BINARY_DIR}/hosts/arena/plaza_server.conf
                         ${PROJECT_BINARY_DIR}/hosts/client/plaza_drawn.conf @gate.scene @Play until=studio_previewing
                         @Frame "until=studio_frame,draws>=1~600" "@Capture frame" "until=studio_frame,captures>=1~600"
                         @Stop until=studio_game_ended
                 WORKING_DIRECTORY ${PROJECT_SOURCE_DIR})
        set_tests_properties(studio_frame_capture PROPERTIES
                             FIXTURES_REQUIRED plaza_content
                             SKIP_REGULAR_EXPRESSION "skip: no device"
                             PASS_REGULAR_EXPRESSION
                             "\"code\":\"studio_frame\"[^\n]*\"draws\":[1-9][0-9]*,\"figures\":40,\"steps\":[1-9][0-9]*,\"captures\":1,.*\"exit\":\"clean_stop\"")
        # Captures opened read only (D843, D844): a frame's and a profile's,
        # each put beside a copy of Runners and opened as Studio starts,
        # shown as its rows, a step's lines in the Inspector.
        add_test(NAME studio_shows_frame_capture
                 COMMAND ${CMAKE_COMMAND} -E env
                         "STUDIO_EXTRA_FILES=${CMAKE_CURRENT_SOURCE_DIR}/tests/captures/frame.rfframe=captures/frame.rfframe"
                         ${RAWFRAME_SHELL} ${PROJECT_SOURCE_DIR}/tools/xvfb_run.sh -s "-screen 0 1280x720x24 -br" ${RAWFRAME_SHELL}
                         ${CMAKE_CURRENT_SOURCE_DIR}/tests/clicked.sh $<TARGET_FILE:rawframe_host_studio_tool>
                         games/runners/runners.game captures/frame.rfframe wheel=457,650,4 @captures/frame.rfframe until=studio_capture_shown
                         "@translucent (1 drawn)" wait=1
                 WORKING_DIRECTORY ${PROJECT_SOURCE_DIR})
        set_tests_properties(studio_shows_frame_capture PROPERTIES
                             SKIP_REGULAR_EXPRESSION "skip: no device"
                             PASS_REGULAR_EXPRESSION
                             "\"code\":\"studio_capture_shown\"[^\n]*\"figures\":5,\"steps\":3,\"shown\":3}.*\"code\":\"studio_summary\"[^\n]*\"entity\":\"step 2\",.*\"exit\":\"clean_stop\"")
        add_test(NAME studio_shows_profile_capture
                 COMMAND ${CMAKE_COMMAND} -E env
                         "STUDIO_EXTRA_FILES=${CMAKE_CURRENT_SOURCE_DIR}/tests/captures/profile.rfprofile=captures/profile.rfprofile"
                         ${RAWFRAME_SHELL} ${PROJECT_SOURCE_DIR}/tools/xvfb_run.sh -s "-screen 0 1280x720x24 -br" ${RAWFRAME_SHELL}
                         ${CMAKE_CURRENT_SOURCE_DIR}/tests/clicked.sh $<TARGET_FILE:rawframe_host_studio_tool>
                         games/runners/runners.game captures/profile.rfprofile wheel=457,650,4 @captures/profile.rfprofile until=studio_capture_shown
                         @runners.run wait=1
                 WORKING_DIRECTORY ${PROJECT_SOURCE_DIR})
        set_tests_properties(studio_shows_profile_capture PROPERTIES
                             SKIP_REGULAR_EXPRESSION "skip: no device"
                             PASS_REGULAR_EXPRESSION
                             "\"code\":\"studio_capture_shown\"[^\n]*\"ticks\":4,\"systems\":2}.*\"code\":\"studio_summary\"[^\n]*\"entity\":\"system 1\",.*\"exit\":\"clean_stop\"")
        # A blockout edited in Studio (D850): a yard beside a copy of Runners,
        # its floor a row; a wall typed as a new solid and its size set by
        # its field, each through the session's lines (D849) and written; a
        # line that is no solid refused; the wall dragged a meter in the plan
        # the center draws of them (D852), and the floor chosen there (D851).
        add_test(NAME studio_blockout
                 COMMAND ${CMAKE_COMMAND} -E env
                         "STUDIO_EXTRA_FILES=${CMAKE_CURRENT_SOURCE_DIR}/tests/blockouts/yard.blockout=yard.blockout"
                         STUDIO_SHOW_WRITTEN=1
                         ${RAWFRAME_SHELL} ${PROJECT_SOURCE_DIR}/tools/xvfb_run.sh -s "-screen 0 1280x720x24 -br" ${RAWFRAME_SHELL}
                         ${CMAKE_CURRENT_SOURCE_DIR}/tests/clicked.sh $<TARGET_FILE:rawframe_host_studio_tool>
                         games/runners/runners.game yard.blockout wheel=457,650,4 @yard.blockout until=studio_blockout
                         "@new solid#2:box wall at 0 0 3 size 4 2 0.2" "until=studio_said,status=\"wall added"
                         "@wall (box)" "@size#2:4 3 0.2" "until=studio_said,status=\"wall size set"
                         "drag=@wall plan,64,0" "until=studio_said,status=\"wall moved to 1 0 3"
                         "@new solid#2:sphere ball at 0 0 0" wait=1 "@floor plan" wait=1
                 WORKING_DIRECTORY ${PROJECT_SOURCE_DIR})
        set_tests_properties(studio_blockout PROPERTIES
                             SKIP_REGULAR_EXPRESSION "skip: no device"
                             PASS_REGULAR_EXPRESSION
                             "yard.blockout written: yes\nblockout 1\nbox floor at 0 0 0 size 8 0.5 8\nbox wall at 1 0 3 size 4 3 0.2\n.*\"code\":\"studio_blockout\"[^\n]*\"lines\":3,\"solids\":2}.*\"code\":\"studio_plan\"[^\n]*\"solids\":2,\"cuts\":0,\"centimetersWide\":800,\"centimetersDeep\":800}.*\"code\":\"studio_summary\"[^\n]*\"scene\":\"yard.blockout\",[^\n]*\"entity\":\"floor\",[^\n]*\"applied\":3,\"refused\":1,.*\"exit\":\"clean_stop\"")
        # A terrain brushed in Studio (D856): a flat field beside a copy of
        # Runners, its layers as rows and its ground drawn from above; the
        # brush's radius and amount set, a block pressed raising the ground
        # there through the session's terrain.brush (D855) and written; the
        # rock chosen and a block painted with it.
        add_test(NAME studio_terrain
                 COMMAND ${CMAKE_COMMAND} -E env
                         "STUDIO_EXTRA_FILES=${CMAKE_CURRENT_SOURCE_DIR}/tests/terrains/field.terrain=field.terrain"
                         STUDIO_SHOW_WRITTEN=1
                         ${RAWFRAME_SHELL} ${PROJECT_SOURCE_DIR}/tools/xvfb_run.sh -s "-screen 0 1280x720x24 -br" ${RAWFRAME_SHELL}
                         ${CMAKE_CURRENT_SOURCE_DIR}/tests/clicked.sh $<TARGET_FILE:rawframe_host_studio_tool>
                         games/runners/runners.game field.terrain wheel=457,650,4 @field.terrain until=studio_terrain
                         "@radius#2:1.5" "@amount#2:1" "@map 2 2" "until=studio_said,status=\"raised at 2.5 2.5"
                         "@rock (layer)" "@radius#2:0.6" "@map 0 0" "until=studio_said,status=\"painted at 0.5 0.5"
                 WORKING_DIRECTORY ${PROJECT_SOURCE_DIR})
        set_tests_properties(studio_terrain PROPERTIES
                             SKIP_REGULAR_EXPRESSION "skip: no device"
                             PASS_REGULAR_EXPRESSION
                             "field.terrain written: yes\n.*heights\n0 0 0 0 0\n.*layers\n1000\n.*\"code\":\"studio_terrain\"[^\n]*\"samples\":25,\"layers\":2,\"blocks\":16,\"strokes\":2}.*\"code\":\"studio_summary\"[^\n]*\"scene\":\"field.terrain\",[^\n]*\"applied\":2,\"refused\":0,.*\"exit\":\"clean_stop\"")
        # A tilemap painted in Studio (D861): a room beside a copy of Runners,
        # its sheet's tiles as rows and its grid drawn; tile 1 chosen and a
        # cell painted with it through the session's tilemap.paint (D860),
        # then none set as the tile and a floor cell cleared, and written.
        add_test(NAME studio_tilemap
                 COMMAND ${CMAKE_COMMAND} -E env
                         "STUDIO_EXTRA_FILES=${CMAKE_CURRENT_SOURCE_DIR}/tests/tilemaps/room.tilemap=room.tilemap"
                         STUDIO_SHOW_WRITTEN=1
                         ${RAWFRAME_SHELL} ${PROJECT_SOURCE_DIR}/tools/xvfb_run.sh -s "-screen 0 1280x720x24 -br" ${RAWFRAME_SHELL}
                         ${CMAKE_CURRENT_SOURCE_DIR}/tests/clicked.sh $<TARGET_FILE:rawframe_host_studio_tool>
                         games/runners/runners.game room.tilemap wheel=457,650,4 @room.tilemap until=studio_tilemap
                         "@tile 1 (tile)" "@cell 2 1" "until=studio_said,status=\"painted tile 1 at 2 1"
                         "@tile#2:none" "@cell 0 2" "until=studio_said,status=\"cleared at 0 2"
                 WORKING_DIRECTORY ${PROJECT_SOURCE_DIR})
        set_tests_properties(studio_tilemap PROPERTIES
                             SKIP_REGULAR_EXPRESSION "skip: no device"
                             PASS_REGULAR_EXPRESSION
                             "room.tilemap written: yes\n.*tiles\n\\. \\. \\. \\.\n\\. \\. 1 \\.\n\\. 0 0 0\n.*\"code\":\"studio_tilemap\"[^\n]*\"cells\":12,\"sheet\":2,\"blocks\":12,\"painted\":2}.*\"code\":\"studio_summary\"[^\n]*\"scene\":\"room.tilemap\",[^\n]*\"applied\":2,\"refused\":0,.*\"exit\":\"clean_stop\"")
        # Paused and stepped (D806): Plaza played from Studio, its World
        # paused on its server, stepped three ticks one by one, each a tick
        # on, and let run again.
        add_test(NAME studio_pauses
                 COMMAND ${RAWFRAME_SHELL} ${PROJECT_SOURCE_DIR}/tools/xvfb_run.sh -s "-screen 0 1280x720x24 -br" ${RAWFRAME_SHELL}
                         ${CMAKE_CURRENT_SOURCE_DIR}/tests/plays.sh $<TARGET_FILE:rawframe_host_studio_tool>
                         $<TARGET_FILE:rawframe_host_dedicated_server> $<TARGET_FILE:rawframe_host_client>
                         games/plaza/plaza.game ${PROJECT_BINARY_DIR}/hosts/arena/plaza_server.conf
                         ${PROJECT_BINARY_DIR}/hosts/client/plaza_drawn.conf @gate.scene @Play until=studio_previewing
                         @Game "@Pause  F6" "until=studio_said,status=\"paused at tick" wait=1
                         @Game "@Step  F7" "until=studio_said,status=\"stepped from tick" wait=1
                         @Game "@Step  F7" "until=studio_said,status=\"stepped from tick" wait=1
                         @Game "@Step  F7" "until=studio_said,status=\"stepped from tick" wait=1
                         @Game "@Pause  F6" "until=studio_said,status=\"running from tick" @Stop until=studio_game_ended
                 WORKING_DIRECTORY ${PROJECT_SOURCE_DIR})
        set_tests_properties(studio_pauses PROPERTIES
                             FIXTURES_REQUIRED plaza_content
                             SKIP_REGULAR_EXPRESSION "skip: no device"
                             PASS_REGULAR_EXPRESSION
                             "\"code\":\"studio_played\"[^\n]*\"paused\":false,\"pauses\":2,\"steps\":3,[^\n]*\"ranPaused\":3,")
        # Two players (D804): Plaza played from Studio with two chosen in the
        # Game menu, the second client joining from its own window, the
        # server holding both at once.
        add_test(NAME studio_plays_two
                 COMMAND ${RAWFRAME_SHELL} ${PROJECT_SOURCE_DIR}/tools/xvfb_run.sh -s "-screen 0 1280x720x24 -br" ${RAWFRAME_SHELL}
                         ${CMAKE_CURRENT_SOURCE_DIR}/tests/plays.sh $<TARGET_FILE:rawframe_host_studio_tool>
                         $<TARGET_FILE:rawframe_host_dedicated_server> $<TARGET_FILE:rawframe_host_client>
                         games/plaza/plaza.game ${PROJECT_BINARY_DIR}/hosts/arena/plaza_server.conf
                         ${PROJECT_BINARY_DIR}/hosts/client/plaza_drawn.conf @Game "@Two players"
                         "until=studio_said,status=\"2 players" @gate.scene @Play until=studio_previewing wait=10
                         @Stop until=studio_game_ended
                 WORKING_DIRECTORY ${PROJECT_SOURCE_DIR})
        set_tests_properties(studio_plays_two PROPERTIES
                             FIXTURES_REQUIRED plaza_content
                             PROCESSORS 4
                             SKIP_REGULAR_EXPRESSION "skip: no device"
                             PASS_REGULAR_EXPRESSION
                             "\"code\":\"studio_played\"[^\n]*\"players\":2,\"othersStarted\":1.*\"code\":\"server_summary\"[^\n]*\"mostConnections\":[2-9],")
        # A view mode (D794): Plaza played in the viewport and drawn unlit
        # from the View menu, which its client's preview takes.
        add_test(NAME studio_view_modes
                 COMMAND ${CMAKE_COMMAND} -E env "STUDIO_SETTINGS=studio.play_in_viewport = true"
                         ${RAWFRAME_SHELL} ${PROJECT_SOURCE_DIR}/tools/xvfb_run.sh -s "-screen 0 1280x720x24 -br" ${RAWFRAME_SHELL}
                         ${CMAKE_CURRENT_SOURCE_DIR}/tests/plays.sh $<TARGET_FILE:rawframe_host_studio_tool>
                         $<TARGET_FILE:rawframe_host_dedicated_server> $<TARGET_FILE:rawframe_host_client>
                         games/plaza/plaza.game ${PROJECT_BINARY_DIR}/hosts/arena/plaza_server.conf
                         ${PROJECT_BINARY_DIR}/hosts/client/plaza_drawn.conf @gate.scene @Play until=studio_previewing
                         @View @Unlit "until=studio_said,status=\"unlit view" wait=2 @Stop until=studio_game_ended
                 WORKING_DIRECTORY ${PROJECT_SOURCE_DIR})
        set_tests_properties(studio_view_modes PROPERTIES
                             FIXTURES_REQUIRED plaza_content
                             PROCESSORS 3
                             SKIP_REGULAR_EXPRESSION "skip: no device"
                             PASS_REGULAR_EXPRESSION
                             "saw studio_said,status=\"unlit view.*\"code\":\"studio_viewport\"[^\n]*\"mode\":\"unlit\",\"modesShown\":[1-9]")
        # Split views (D797): Plaza played in the viewport and split three
        # ways from the View menu once it is looked at from a view, the
        # client drawing the session's top and front views beside the
        # preview's own.
        add_test(NAME studio_splits_views
                 COMMAND ${CMAKE_COMMAND} -E env "STUDIO_SETTINGS=studio.play_in_viewport = true"
                         ${RAWFRAME_SHELL} ${PROJECT_SOURCE_DIR}/tools/xvfb_run.sh -s "-screen 0 1280x720x24 -br" ${RAWFRAME_SHELL}
                         ${CMAKE_CURRENT_SOURCE_DIR}/tests/plays.sh $<TARGET_FILE:rawframe_host_studio_tool>
                         $<TARGET_FILE:rawframe_host_dedicated_server> $<TARGET_FILE:rawframe_host_client>
                         games/plaza/plaza.game ${PROJECT_BINARY_DIR}/hosts/arena/plaza_server.conf
                         ${PROJECT_BINARY_DIR}/hosts/client/plaza_drawn.conf @gate.scene @Play until=studio_previewing
                         "@eye#2:0,30,0.5" "until=studio_said,status=\"view set" @View "@Three views"
                         "until=studio_said,status=\"3 views" wait=3 @Stop until=studio_game_ended
                 WORKING_DIRECTORY ${PROJECT_SOURCE_DIR})
        set_tests_properties(studio_splits_views PROPERTIES
                             FIXTURES_REQUIRED plaza_content
                             PROCESSORS 3
                             SKIP_REGULAR_EXPRESSION "skip: no device"
                             PASS_REGULAR_EXPRESSION
                             "saw studio_said,status=\"3 views.*\"code\":\"studio_viewport\"[^\n]*\"views\":3,\"splitsShown\":[1-9].*\"code\":\"scene_placed_summary\"[^\n]*\"splitFrames\":[1-9]")
        # Overlays (D798): Plaza played in the viewport with its colliders and
        # lights shown from the View menu, the client drawing their lines
        # over the scene.
        add_test(NAME studio_shows_overlays
                 COMMAND ${CMAKE_COMMAND} -E env "STUDIO_SETTINGS=studio.play_in_viewport = true"
                         ${RAWFRAME_SHELL} ${PROJECT_SOURCE_DIR}/tools/xvfb_run.sh -s "-screen 0 1280x720x24 -br" ${RAWFRAME_SHELL}
                         ${CMAKE_CURRENT_SOURCE_DIR}/tests/plays.sh $<TARGET_FILE:rawframe_host_studio_tool>
                         $<TARGET_FILE:rawframe_host_dedicated_server> $<TARGET_FILE:rawframe_host_client>
                         games/plaza/plaza.game ${PROJECT_BINARY_DIR}/hosts/arena/plaza_server.conf
                         ${PROJECT_BINARY_DIR}/hosts/client/plaza_drawn.conf @gate.scene @Play until=studio_previewing
                         "@eye#2:0,30,0.5" "until=studio_said,status=\"view set" @View "@Show colliders"
                         "until=studio_said,status=\"colliders shown" @View "@Show lights"
                         "until=studio_said,status=\"lights shown" wait=3 @Stop until=studio_game_ended
                 WORKING_DIRECTORY ${PROJECT_SOURCE_DIR})
        set_tests_properties(studio_shows_overlays PROPERTIES
                             FIXTURES_REQUIRED plaza_content
                             PROCESSORS 3
                             SKIP_REGULAR_EXPRESSION "skip: no device"
                             PASS_REGULAR_EXPRESSION
                             "saw studio_said,status=\"lights shown.*\"code\":\"studio_viewport\"[^\n]*\"overlays\":2,\"overlaysShown\":[1-9].*\"code\":\"scene_placed_summary\"[^\n]*\"overlayLines\":[1-9]")
        # A 2D game seen in 2D (D793): Runners played in the viewport, the
        # view seen in 2D from the View menu, and the client's canvas drawn
        # from where the preview's eye is, not the player's camera.
        add_test(NAME studio_sees_in_2d
                 COMMAND ${CMAKE_COMMAND} -E env "STUDIO_SETTINGS=studio.play_in_viewport = true"
                         ${RAWFRAME_SHELL} ${PROJECT_SOURCE_DIR}/tools/xvfb_run.sh -s "-screen 0 1280x720x24 -br" ${RAWFRAME_SHELL}
                         ${CMAKE_CURRENT_SOURCE_DIR}/tests/plays.sh $<TARGET_FILE:rawframe_host_studio_tool>
                         $<TARGET_FILE:rawframe_host_dedicated_server> $<TARGET_FILE:rawframe_host_client>
                         games/runners/runners.game /dev/null ${PROJECT_BINARY_DIR}/hosts/client/runners_clicked.conf
                         @level.scene @Play until=studio_previewing @View "@2D view"
                         "until=studio_said,status=\"2D view" wait=3 @Stop until=studio_game_ended
                 WORKING_DIRECTORY ${PROJECT_SOURCE_DIR})
        set_tests_properties(studio_sees_in_2d PROPERTIES
                             FIXTURES_REQUIRED runners_content
                             PROCESSORS 3
                             SKIP_REGULAR_EXPRESSION "skip: no device"
                             PASS_REGULAR_EXPRESSION
                             "saw studio_said,status=\"2D view.*\"code\":\"canvas_summary\"[^\n]*\"framesPreviewed\":[1-9]")
        # A material edited while the game plays (D734): Gale cooked by
        # Studio and played from that content, its shore's tint typed, and
        # Studio cooks again on its own; the played client, following its
        # content, reads the game's materials again, waited for in the
        # client's own log: a loaded machine's client takes its time.
        add_test(NAME studio_recooks_gale
                 COMMAND ${CMAKE_COMMAND} -E env STUDIO_COOK=$<TARGET_FILE:rawframe_host_cook_tool>
                         ${RAWFRAME_SHELL} ${PROJECT_SOURCE_DIR}/hosts/client/tests/cooked.sh --then ${PROJECT_BINARY_DIR}/hosts/client/gale_content
                         ${RAWFRAME_SHELL} ${PROJECT_SOURCE_DIR}/tools/xvfb_run.sh -s "-screen 0 1280x720x24 -br" ${RAWFRAME_SHELL}
                         ${CMAKE_CURRENT_SOURCE_DIR}/tests/plays.sh $<TARGET_FILE:rawframe_host_studio_tool>
                         $<TARGET_FILE:rawframe_host_dedicated_server> $<TARGET_FILE:rawframe_host_client>
                         games/gale/gale.game /dev/null ${PROJECT_BINARY_DIR}/hosts/client/gale_drawn.conf
                         @Cook "until=studio_said,status=\"cooked ~600" @shore.rfmaterial @Play wait=6 @tint
                         "@override#2:[0.6,0.2,0.1]" "until=studio_said,status=\"cooked #2~600" until=materials_reloaded@play/client.log~600
                         @Stop until=studio_game_ended
                 WORKING_DIRECTORY ${PROJECT_SOURCE_DIR})
        set_tests_properties(studio_recooks_gale PROPERTIES
                             FIXTURES_REQUIRED gale_content
                             PROCESSORS 2
                             SKIP_REGULAR_EXPRESSION "skip: no device;skip: no shader toolchain"
                             PASS_REGULAR_EXPRESSION
                             "typed \\[0.6,0.2,0.1\\] at [0-9]+,[0-9]+.*\"code\":\"studio_summary\"[^\n]*\"applied\":1,[^\n]*\"played\":1,.*\"code\":\"materials_reloaded\"[^\n]*\"reloads\":1")
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
                         games/plaza/plaza.game ${PROJECT_BINARY_DIR}/hosts/arena/plaza_server.conf
                         ${PROJECT_BINARY_DIR}/hosts/client/plaza_drawn.conf @gate.scene @Play until=studio_previewing
                         "@eye#2:-30,3,-20" "@target#2:-30,2.5,-30" wait=3 980,520 "until=studio_said,status=\"picked "
                         @Stop until=studio_game_ended
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
                         games/plaza/plaza.game ${PROJECT_BINARY_DIR}/hosts/arena/plaza_server.conf
                         ${PROJECT_BINARY_DIR}/hosts/client/plaza_drawn.conf @gate.scene @Play until=studio_previewing
                         "@eye#2:-30,3,-20" "@target#2:-30,2.5,-30" wait=3 drag=980,520,1080,520 until=studio_dragged @Stop until=studio_game_ended
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
                         games/plaza/plaza.game ${PROJECT_BINARY_DIR}/hosts/arena/plaza_server.conf
                         ${PROJECT_BINARY_DIR}/hosts/client/plaza_drawn.conf @gate.scene @Play until=studio_previewing
                         "@eye#2:-30,3,-20" "@target#2:-30,2.5,-30" wait=3 980,520 until=studio_marked wait=1 drag=999,520,1079,540 until=studio_dragged @Stop until=studio_game_ended
                 WORKING_DIRECTORY ${PROJECT_SOURCE_DIR})
        set_tests_properties(studio_handles_plaza PROPERTIES
                             FIXTURES_REQUIRED plaza_content
                             SKIP_REGULAR_EXPRESSION "skip: no device"
                             PASS_REGULAR_EXPRESSION
                             "dragged from 999,520 to 1079,540.*\"code\":\"studio_summary\"[^\n]*\"applied\":1,[^\n]*\"dragged\":1,.*scene gate.scene: [^\n]*\"id\":\"6c632219-53a9-4af4-8781-bb2a8f4fa53b\",[^\n]*\"rawframe.physics3d.pose\":{\"x\":-27\\.[2-6][0-9]*,\"y\":2\\.5,\"z\":-30}")
        # The handle under the pointer drawn lit (D468): the post chosen and
        # marked, the pointer moved onto its X handle with no press, and the
        # played client draws the mark with a part lit.
        add_test(NAME studio_hovers_plaza
                 COMMAND ${RAWFRAME_SHELL} ${PROJECT_SOURCE_DIR}/tools/xvfb_run.sh -s "-screen 0 1280x720x24 -br" ${RAWFRAME_SHELL}
                         ${CMAKE_CURRENT_SOURCE_DIR}/tests/plays.sh $<TARGET_FILE:rawframe_host_studio_tool>
                         $<TARGET_FILE:rawframe_host_dedicated_server> $<TARGET_FILE:rawframe_host_client>
                         games/plaza/plaza.game ${PROJECT_BINARY_DIR}/hosts/arena/plaza_server.conf
                         ${PROJECT_BINARY_DIR}/hosts/client/plaza_drawn.conf @gate.scene @Play until=studio_previewing
                         "@eye#2:-30,3,-20" "@target#2:-30,2.5,-30" wait=3 980,520 until=studio_marked move=999,520 until=studio_lit wait=1
                         @Stop until=studio_game_ended
                 WORKING_DIRECTORY ${PROJECT_SOURCE_DIR})
        set_tests_properties(studio_hovers_plaza PROPERTIES
                             FIXTURES_REQUIRED plaza_content
                             SKIP_REGULAR_EXPRESSION "skip: no device"
                             PASS_REGULAR_EXPRESSION
                             "moved to 999,520.*\"code\":\"scene_summary\"[^\n]*\"marksLit\":[1-9]")
        # The view moved in the played game (D469): the wheel turned three
        # detents away over the played client's window brings the eye
        # nearer, a drag with the right button carries it round, and Studio
        # shows the view moved.
        add_test(NAME studio_orbits_plaza
                 COMMAND ${RAWFRAME_SHELL} ${PROJECT_SOURCE_DIR}/tools/xvfb_run.sh -s "-screen 0 1280x720x24 -br" ${RAWFRAME_SHELL}
                         ${CMAKE_CURRENT_SOURCE_DIR}/tests/plays.sh $<TARGET_FILE:rawframe_host_studio_tool>
                         $<TARGET_FILE:rawframe_host_dedicated_server> $<TARGET_FILE:rawframe_host_client>
                         games/plaza/plaza.game ${PROJECT_BINARY_DIR}/hosts/arena/plaza_server.conf
                         ${PROJECT_BINARY_DIR}/hosts/client/plaza_drawn.conf @gate.scene @Play until=studio_previewing
                         "@eye#2:-30,3,-20" "@target#2:-30,2.5,-30" wait=3 wheel=980,520,-3 orbit=980,520,1080,520 wait=3
                         @Stop until=studio_game_ended
                 WORKING_DIRECTORY ${PROJECT_SOURCE_DIR})
        set_tests_properties(studio_orbits_plaza PROPERTIES
                             FIXTURES_REQUIRED plaza_content
                             SKIP_REGULAR_EXPRESSION "skip: no device"
                             PASS_REGULAR_EXPRESSION
                             "orbited from 980,520 to 1080,520.*\"code\":\"studio_view_moved\"[^\n]*\"distance\":6\\.[0-9]*,.*\"code\":\"studio_summary\"[^\n]*\"viewsMoved\":[1-9]")
        # A drag snapped to the grid (D471): the snap set to a meter and 15
        # degrees in Studio's view fields, the post dragged right, and it
        # lands on a whole meter along X, Z as it was.
        add_test(NAME studio_snaps_plaza
                 COMMAND ${RAWFRAME_SHELL} ${PROJECT_SOURCE_DIR}/tools/xvfb_run.sh -s "-screen 0 1280x720x24 -br" ${RAWFRAME_SHELL}
                         ${CMAKE_CURRENT_SOURCE_DIR}/tests/plays.sh $<TARGET_FILE:rawframe_host_studio_tool>
                         $<TARGET_FILE:rawframe_host_dedicated_server> $<TARGET_FILE:rawframe_host_client>
                         games/plaza/plaza.game ${PROJECT_BINARY_DIR}/hosts/arena/plaza_server.conf
                         ${PROJECT_BINARY_DIR}/hosts/client/plaza_drawn.conf @gate.scene @Play until=studio_previewing
                         "@eye#2:-30,3,-20" "@target#2:-30,2.5,-30" "@snap#2:1 15" wait=3 drag=980,520,1080,520
                         until=studio_dragged @Stop until=studio_game_ended
                 WORKING_DIRECTORY ${PROJECT_SOURCE_DIR})
        set_tests_properties(studio_snaps_plaza PROPERTIES
                             FIXTURES_REQUIRED plaza_content
                             SKIP_REGULAR_EXPRESSION "skip: no device"
                             PASS_REGULAR_EXPRESSION
                             "\"code\":\"studio_summary\"[^\n]*\"snapped\":1,.*scene gate.scene: [^\n]*\"id\":\"6c632219-53a9-4af4-8781-bb2a8f4fa53b\",[^\n]*\"rawframe.physics3d.pose\":{\"x\":-2[0-9],\"y\":2\\.5,\"z\":-30}")
        # Freelook (D472): a drag with the right button and Shift held turns
        # the view about its eye, the eye staying where it was typed and the
        # target carried round it; the wheel turned away with Shift held then
        # flies eye and target along it, the eye leaving -30 along X, its
        # distance from the target as it was.
        add_test(NAME studio_looks_plaza
                 COMMAND ${RAWFRAME_SHELL} ${PROJECT_SOURCE_DIR}/tools/xvfb_run.sh -s "-screen 0 1280x720x24 -br" ${RAWFRAME_SHELL}
                         ${CMAKE_CURRENT_SOURCE_DIR}/tests/plays.sh $<TARGET_FILE:rawframe_host_studio_tool>
                         $<TARGET_FILE:rawframe_host_dedicated_server> $<TARGET_FILE:rawframe_host_client>
                         games/plaza/plaza.game ${PROJECT_BINARY_DIR}/hosts/arena/plaza_server.conf
                         ${PROJECT_BINARY_DIR}/hosts/client/plaza_drawn.conf @gate.scene @Play until=studio_previewing
                         "@eye#2:-30,3,-20" "@target#2:-30,2.5,-30" wait=3 orbit=980,520,1080,520,Shift_L
                         until=studio_view_moved wait=2 wheel=980,520,-3,Shift_L wait=2 @Stop until=studio_game_ended
                 WORKING_DIRECTORY ${PROJECT_SOURCE_DIR})
        set_tests_properties(studio_looks_plaza PROPERTIES
                             FIXTURES_REQUIRED plaza_content
                             SKIP_REGULAR_EXPRESSION "skip: no device"
                             PASS_REGULAR_EXPRESSION
                             "orbited from 980,520 to 1080,520 holding Shift_L.*turned the wheel -3 at 980,520 holding Shift_L.*\"code\":\"studio_view_moved\"[^\n]*\"eyeX\":-30,\"eyeY\":3,\"eyeZ\":-20,\"targetX\":-[23][0-9]\\.[0-9]+.*\"code\":\"studio_view_moved\"[^\n]*\"distance\":10\\.01[0-9]*,\"eyeX\":-2[0-9]\\.[0-9]+")
        # The mark's ring dragged (D467): seen from above, the post chosen,
        # its ring pressed left of it and let go in front of it, a quarter
        # turn about its upright.
        add_test(NAME studio_rings_plaza
                 COMMAND ${RAWFRAME_SHELL} ${PROJECT_SOURCE_DIR}/tools/xvfb_run.sh -s "-screen 0 1280x720x24 -br" ${RAWFRAME_SHELL}
                         ${CMAKE_CURRENT_SOURCE_DIR}/tests/plays.sh $<TARGET_FILE:rawframe_host_studio_tool>
                         $<TARGET_FILE:rawframe_host_dedicated_server> $<TARGET_FILE:rawframe_host_client>
                         games/plaza/plaza.game ${PROJECT_BINARY_DIR}/hosts/arena/plaza_server.conf
                         ${PROJECT_BINARY_DIR}/hosts/client/plaza_drawn.conf @gate.scene @Play until=studio_previewing
                         "@eye#2:-30,8,-25" "@target#2:-30,2.5,-30" wait=3 980,520 until=studio_marked wait=1 drag=928,520,980,564 until=studio_dragged @Stop until=studio_game_ended
                 WORKING_DIRECTORY ${PROJECT_SOURCE_DIR})
        set_tests_properties(studio_rings_plaza PROPERTIES
                             FIXTURES_REQUIRED plaza_content
                             SKIP_REGULAR_EXPRESSION "skip: no device"
                             PASS_REGULAR_EXPRESSION
                             "dragged from 928,520 to 980,564.*\"code\":\"studio_summary\"[^\n]*\"applied\":1,[^\n]*\"turned\":1,.*scene gate.scene: [^\n]*\"id\":\"6c632219-53a9-4af4-8781-bb2a8f4fa53b\",[^\n]*\"rawframe.physics3d.pose\":{\"qw\":0\\.70[0-9]*,\"qy\":0\\.70[0-9]*,\"x\":-30,\"y\":2\\.5,\"z\":-30}")
        # The post raised with Shift and turned with Control held through
        # the drag (D463): up the upright plane facing the eye, and about
        # the post's own place, each one transaction.
        add_test(NAME studio_raises_plaza
                 COMMAND ${RAWFRAME_SHELL} ${PROJECT_SOURCE_DIR}/tools/xvfb_run.sh -s "-screen 0 1280x720x24 -br" ${RAWFRAME_SHELL}
                         ${CMAKE_CURRENT_SOURCE_DIR}/tests/plays.sh $<TARGET_FILE:rawframe_host_studio_tool>
                         $<TARGET_FILE:rawframe_host_dedicated_server> $<TARGET_FILE:rawframe_host_client>
                         games/plaza/plaza.game ${PROJECT_BINARY_DIR}/hosts/arena/plaza_server.conf
                         ${PROJECT_BINARY_DIR}/hosts/client/plaza_drawn.conf @gate.scene @Play until=studio_previewing
                         "@eye#2:-30,3,-20" "@target#2:-30,2.5,-30" wait=3 drag=980,520,980,470,Shift_L until=studio_dragged @Stop until=studio_game_ended
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
                         games/plaza/plaza.game ${PROJECT_BINARY_DIR}/hosts/arena/plaza_server.conf
                         ${PROJECT_BINARY_DIR}/hosts/client/plaza_drawn.conf @gate.scene @Play until=studio_previewing
                         "@eye#2:-30,3,-20" "@target#2:-30,2.5,-30" wait=3 drag=980,520,1080,520,Control_L until=studio_dragged @Stop until=studio_game_ended
                 WORKING_DIRECTORY ${PROJECT_SOURCE_DIR})
        set_tests_properties(studio_turns_plaza PROPERTIES
                             FIXTURES_REQUIRED plaza_content
                             SKIP_REGULAR_EXPRESSION "skip: no device"
                             PASS_REGULAR_EXPRESSION
                             "dragged from 980,520 to 1080,520 holding Control_L.*\"code\":\"studio_summary\"[^\n]*\"entity\":\"6c632219-53a9-4af4-8781-bb2a8f4fa53b\",[^\n]*\"applied\":1,[^\n]*\"undoable\":1,[^\n]*\"picked\":1,\"dragged\":1,\"raised\":0,\"turned\":1,")
        # Each played test ends by stopping its game and waiting for Studio
        # to say so, so it is stopped half a second after, not five (D499).
        # A played game is three programs, Studio, its server, and a client
        # drawing on the device: each counts as three of the tree's jobs, so
        # a check starts fewer at once and none starves the others (D467;
        # two from D496, three again after four of them failed together in
        # one day's checks at a load of 50 to 80, D789).
        set_tests_properties(studio_plays_plaza studio_picks_plaza studio_drags_plaza studio_handles_plaza studio_hovers_plaza studio_orbits_plaza studio_snaps_plaza studio_looks_plaza
                             studio_rings_plaza studio_raises_plaza studio_turns_plaza studio_previews_plaza studio_cooks_plaza
                             studio_plays_in_viewport
                             PROPERTIES PROCESSORS 3)
    endif()
    # Every test here draws a window: labeled so the check can leave them
    # out of a tree they find nothing in (D493), and Studio's out of the
    # thread sanitizer's (D527). A record a test waited for and never saw
    # fails it, rather than costing three minutes unseen (D627).
    get_property(window_tests DIRECTORY PROPERTY TESTS)
    set_tests_properties(${window_tests} PROPERTIES LABELS "window;studio" FAIL_REGULAR_EXPRESSION "never saw")
    # The played tests that differ only in the gesture made in the game,
    # each three instrumented programs drawing for half a minute: the
    # address sanitizer's tree plays the played path through previews,
    # plays, cooks, and picks, and leaves these to the fast check (D645).
    if(TEST studio_drags_plaza)
        set_tests_properties(studio_drags_plaza studio_handles_plaza studio_hovers_plaza studio_orbits_plaza
                             studio_snaps_plaza studio_looks_plaza studio_rings_plaza studio_raises_plaza
                             studio_turns_plaza PROPERTIES LABELS "window;studio;gesture")
        # A gesture played into Plaza is read as frames come: each holds six
        # processors, as the arena's bot tests do, so the fast check does not
        # lay them over one another and the other played tests (D821).
        set_tests_properties(studio_drags_plaza studio_handles_plaza studio_hovers_plaza studio_orbits_plaza
                             studio_snaps_plaza studio_looks_plaza studio_rings_plaza studio_raises_plaza
                             studio_turns_plaza PROPERTIES PROCESSORS 6)
    endif()
endif()
