# The export tool (D396): a game made into a folder that plays standalone,
# through the cook and the build tool run as processes. Packaging tooling;
# no runtime closure contains it.
rawframe_host(NAME export_tool OUTPUT rawframe-export
    SOURCES src/main.cpp
    DEPS process document result base
)

if(RAWFRAME_BUILD_TESTS)
    add_test(NAME export_usage COMMAND rawframe_host_export_tool)
    set_tests_properties(export_usage PROPERTIES WILL_FAIL TRUE)
    # Stalls exported and played from its folder with the real client in a
    # window (D396): its font read from the folder's library, the player
    # admitted, frames shown, and both sides stopping cleanly.
    if(TARGET rawframe_host_client AND RAWFRAME_XVFB_RUN)
        add_test(NAME stalls_exported
                 COMMAND ${RAWFRAME_XVFB_RUN} -a -s "-screen 0 1280x720x24 -br" ${RAWFRAME_SHELL}
                         ${CMAKE_CURRENT_SOURCE_DIR}/tests/exported.sh $<TARGET_FILE:rawframe_host_export_tool>
                         $<TARGET_FILE:rawframe_host_cook_tool> $<TARGET_FILE:rawframe_host_build_tool>
                         $<TARGET_FILE:rawframe_host_dedicated_server> $<TARGET_FILE:rawframe_host_client>
                         $<TARGET_FILE:rawframe_host_play_tool> games/tycoon ${CMAKE_CURRENT_BINARY_DIR}/stalls
                 WORKING_DIRECTORY ${PROJECT_SOURCE_DIR})
        set_tests_properties(stalls_exported PROPERTIES
                             SKIP_REGULAR_EXPRESSION "skip: no device"
                             PASS_REGULAR_EXPRESSION
                             "export: fonts read 1, admitted 1, frames shown [1-9][0-9]+, client clean_stop, server clean_stop, play exit 0")
        if(RAWFRAME_SANITIZE STREQUAL "thread")
            set_tests_properties(stalls_exported PROPERTIES ENVIRONMENT
                                 "TSAN_OPTIONS=suppressions=${PROJECT_SOURCE_DIR}/tools/lavapipe.tsan")
        endif()
    endif()
endif()
