# The probe bake (D326): a server and a bot over loopback, the bot's scene
# drawn offscreen, each reflection probe's picture written (Tool role).
# Only where Maul RHI is built.
if(NOT TARGET rawframe_scene_bake)
    return()
endif()

rawframe_host(NAME bake OUTPUT rawframe-bake
    SOURCES src/main.cpp
    DEPS scene_bake host game_content input_kest world_audio world_localization network_loopback render render_canvas world_ui render_canvas_gpu render_scene render_scene_gpu physics2d physics3d world_animation world_replication world_kest world_runtime composition diagnostics result base
)

if(RAWFRAME_BUILD_TESTS)
    # The plaza's probe baked from its cooked content: its picture drawn six
    # ways and written into the build tree, the one probe baked and none
    # left. The arena's wrapper skips a machine without an adapter unless
    # the check requires one.
    set(PLAZA_CONTENT ${PROJECT_BINARY_DIR}/hosts/arena/plaza_content)
    set(BAKED ${CMAKE_CURRENT_BINARY_DIR}/plaza_baked)
    configure_file(tests/plaza_baked.conf.in ${CMAKE_CURRENT_BINARY_DIR}/plaza_baked.conf @ONLY)
    add_test(NAME plaza_baked
             COMMAND ${RAWFRAME_SHELL} ${PROJECT_SOURCE_DIR}/hosts/arena/tests/runners_drawn.sh
                     $<TARGET_FILE:rawframe_host_bake> ${CMAKE_CURRENT_BINARY_DIR}/plaza_baked.conf
             WORKING_DIRECTORY ${PROJECT_SOURCE_DIR})
    set_tests_properties(plaza_baked PROPERTIES
                         FIXTURES_REQUIRED plaza_content
                         SKIP_REGULAR_EXPRESSION "skip: no device"
                         PASS_REGULAR_EXPRESSION
                         "\"code\":\"probe_baked\"[^\n]*\"texture\":\"ac452b5e41f745bf\",\"path\":\"[^\"]*plaza_baked/hall.hdr\",\"width\":256.*\"code\":\"bake_summary\"[^\n]*\"probes\":1,\"baked\":1,\"notBaked\":0")
    if(RAWFRAME_SANITIZE STREQUAL "thread")
        set_tests_properties(plaza_baked PROPERTIES ENVIRONMENT
                             "TSAN_OPTIONS=suppressions=${PROJECT_SOURCE_DIR}/tools/lavapipe.tsan")
    endif()
endif()
