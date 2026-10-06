# The install tool (SPEC-0038): a launcher's installing and updating as a
# process over rawframe.install, from a mirror on disk or over HTTP (D414).
# A launcher program: no engine closure contains it.
rawframe_host(NAME install_tool OUTPUT rawframe-install
    SOURCES src/http_origin.cpp src/main.cpp
    DEPS http install content result base
)

if(RAWFRAME_BUILD_TESTS)
    add_test(NAME install_usage COMMAND rawframe_host_install_tool)
    set_tests_properties(install_usage PROPERTIES WILL_FAIL TRUE)
    # Runners published twice to a mirror and a player's library updated,
    # played, rolled back, healed, and collected, process by process.
    add_test(NAME runners_updated
             COMMAND ${RAWFRAME_SHELL} ${CMAKE_CURRENT_SOURCE_DIR}/tests/runners_updated.sh
                     $<TARGET_FILE:rawframe_host_install_tool> $<TARGET_FILE:rawframe_host_build_tool>
                     $<TARGET_FILE:rawframe_host_arena> ${PROJECT_SOURCE_DIR}
                     ${PROJECT_BINARY_DIR}/hosts/arena/runners_content ${CMAKE_CURRENT_BINARY_DIR}/runners_updated)
    set_tests_properties(runners_updated PROPERTIES FIXTURES_REQUIRED runners_content)
endif()
