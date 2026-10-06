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
    # Stalls exported under a studio's key and played from its folder with
    # the real client in a window (D396): the key set pinned and the secret
    # left out, its font read from the folder's library, the player
    # admitted, frames shown, the server stopping cleanly, and the launcher
    # ending with the client's code (a clean stop, or under load a
    # controlled overload of the software rasterizer's window).
    if(TARGET rawframe_host_client AND RAWFRAME_XVFB)
        add_test(NAME stalls_exported
                 COMMAND ${RAWFRAME_SHELL} ${PROJECT_SOURCE_DIR}/tools/xvfb_run.sh -s "-screen 0 1280x720x24 -br" ${RAWFRAME_SHELL}
                         ${CMAKE_CURRENT_SOURCE_DIR}/tests/exported.sh $<TARGET_FILE:rawframe_host_export_tool>
                         $<TARGET_FILE:rawframe_host_cook_tool> $<TARGET_FILE:rawframe_host_build_tool>
                         $<TARGET_FILE:rawframe_host_dedicated_server> $<TARGET_FILE:rawframe_host_client>
                         $<TARGET_FILE:rawframe_host_play_tool> games/tycoon ${CMAKE_CURRENT_BINARY_DIR}/stalls
                 WORKING_DIRECTORY ${PROJECT_SOURCE_DIR})
        set_tests_properties(stalls_exported PROPERTIES
                             SKIP_REGULAR_EXPRESSION "skip: no device"
                             PASS_REGULAR_EXPRESSION
                             "export: keys studio.keys fonts read 1, admitted 1, frames shown [1-9][0-9]+, client stopped (clean_stop|controlled_overload), server stopped clean_stop, the launcher's code the client's: yes")
        if(RAWFRAME_SANITIZE MATCHES "address")
            # Fifteen seconds drawn leave a thread's allocations in a library
            # unloaded before LeakSanitizer reports, so no suppression can
            # name them; the report stays in client.log, but does not change
            # the code the launcher must pass on (D433b).
            set_tests_properties(stalls_exported PROPERTIES ENVIRONMENT "LSAN_OPTIONS=exitcode=0")
        endif()
    endif()
endif()
