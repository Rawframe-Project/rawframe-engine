# The authoring tool (ADR-0032, SPEC-0040): the operation surface from a
# command line. Tooling only; no client or server closure contains it.
rawframe_host(NAME author OUTPUT rawframe-author
    SOURCES src/main.cpp
    DEPS authoring world_kest kest scene schema document result base
)

if(RAWFRAME_BUILD_TESTS)
    # A bad argument is a usage error, not a crash.
    add_test(NAME author_usage COMMAND rawframe_host_author)
    set_tests_properties(author_usage PROPERTIES WILL_FAIL TRUE)
    # A scene of the runners game changed through requests: a new entity
    # with a body at a place, stale and failing requests refused whole, and
    # a dry run that writes nothing.
    add_test(NAME author_runners
             COMMAND ${CMAKE_CURRENT_SOURCE_DIR}/tests/author_runners.sh $<TARGET_FILE:rawframe_host_author>
                     ${PROJECT_SOURCE_DIR} ${CMAKE_CURRENT_BINARY_DIR}/author_runners)
    set_tests_properties(author_runners PROPERTIES PASS_REGULAR_EXPRESSION "authored runners")
    # Runners' scenes migrated when its Age component grows a field, and
    # refused, unchanged, when it loses one the hall gives a value.
    add_test(NAME author_migrate
             COMMAND ${CMAKE_CURRENT_SOURCE_DIR}/tests/author_migrate.sh $<TARGET_FILE:rawframe_host_author>
                     $<TARGET_FILE:rawframe_host_arena> ${PROJECT_SOURCE_DIR}
                     ${CMAKE_CURRENT_BINARY_DIR}/author_migrate)
    set_tests_properties(author_migrate PROPERTIES PASS_REGULAR_EXPRESSION "migrated runners")
    # Runners' level read through queries, typed by the game's catalog,
    # and a value read set back through a request to no change.
    add_test(NAME author_read
             COMMAND ${CMAKE_CURRENT_SOURCE_DIR}/tests/author_read.sh $<TARGET_FILE:rawframe_host_author>
                     ${PROJECT_SOURCE_DIR} ${CMAKE_CURRENT_BINARY_DIR}/author_read)
    set_tests_properties(author_read PROPERTIES PASS_REGULAR_EXPRESSION "read runners")
    # Runners' platforms moved and reverted through their instances'
    # patches, and the game started on the changed level.
    add_test(NAME author_instances
             COMMAND ${CMAKE_CURRENT_SOURCE_DIR}/tests/author_instances.sh $<TARGET_FILE:rawframe_host_author>
                     $<TARGET_FILE:rawframe_host_arena> ${PROJECT_SOURCE_DIR}
                     ${CMAKE_CURRENT_BINARY_DIR}/author_instances)
    set_tests_properties(author_instances PROPERTIES PASS_REGULAR_EXPRESSION "authored runners' instances")
endif()
