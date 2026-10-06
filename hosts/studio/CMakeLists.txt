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
                         "\"code\":\"ui_drawing_summary\"[^\n]*\"frames\":[1-9][0-9]+,\"boxes\":[1-9][0-9]+.*\"code\":\"studio_summary\"[^\n]*\"records\":1,\"scenes\":3,\"scene\":\"\",[^\n]*\"framesDrawn\":[1-9][0-9]+,\"boxes\":9,\"glyphs\":[1-9][0-9]+}.*\"code\":\"frame_summary\"[^\n]*\"framesShown\":[1-9][0-9]+,.*\"exit\":\"clean_stop\"")
    # Studio clicked: runners' level chosen shows its entities, read from
    # the session, and its ground chosen shows its four components; the
    # ground is chosen in the session too.
    add_test(NAME studio_clicked
             COMMAND ${RAWFRAME_SHELL} ${PROJECT_SOURCE_DIR}/tools/xvfb_run.sh -s "-screen 0 1280x720x24 -br" ${RAWFRAME_SHELL}
                     ${CMAKE_CURRENT_SOURCE_DIR}/tests/clicked.sh $<TARGET_FILE:rawframe_host_studio_tool>
                     games/runners/runners.game 200,126 640,94
             WORKING_DIRECTORY ${PROJECT_SOURCE_DIR})
    set_tests_properties(studio_clicked PROPERTIES
                         SKIP_REGULAR_EXPRESSION "skip: no device"
                         PASS_REGULAR_EXPRESSION
                         "clicked at 200,126.*clicked at 640,94.*\"code\":\"studio_summary\"[^\n]*\"records\":4,\"scenes\":3,\"scene\":\"level.scene\",\"entities\":[1-9][0-9]*,\"entity\":\"c5400cf8-4b07-4d16-84c2-951852c66b40\",\"components\":4,.*\"exit\":\"clean_stop\"")
endif()
