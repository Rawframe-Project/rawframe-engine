# The authoring tool (ADR-0032, SPEC-0040): the operation surface from a
# command line. Tooling only; no client or server closure contains it.
rawframe_host(NAME author OUTPUT rawframe-author
    SOURCES src/main.cpp src/connect.cpp
    DEPS authoring_session authoring process world_kest kest content scene schema document execution result base
)

if(RAWFRAME_BUILD_TESTS)
    # A bad argument is a usage error, not a crash.
    add_test(NAME author_usage COMMAND rawframe_host_author)
    set_tests_properties(author_usage PROPERTIES WILL_FAIL TRUE)
    # A scene of the runners game changed through requests: a new entity
    # with a body at a place, stale and failing requests refused whole, and
    # a dry run that writes nothing.
    add_test(NAME author_runners
             COMMAND ${RAWFRAME_SHELL} ${CMAKE_CURRENT_SOURCE_DIR}/tests/author_runners.sh $<TARGET_FILE:rawframe_host_author>
                     ${PROJECT_SOURCE_DIR} ${CMAKE_CURRENT_BINARY_DIR}/author_runners)
    set_tests_properties(author_runners PROPERTIES PASS_REGULAR_EXPRESSION "authored runners")
    # Runners' scenes migrated when its Age component grows a field, and
    # refused, unchanged, when it loses one the hall gives a value.
    add_test(NAME author_migrate
             COMMAND ${RAWFRAME_SHELL} ${CMAKE_CURRENT_SOURCE_DIR}/tests/author_migrate.sh $<TARGET_FILE:rawframe_host_author>
                     $<TARGET_FILE:rawframe_host_arena> ${PROJECT_SOURCE_DIR}
                     ${CMAKE_CURRENT_BINARY_DIR}/author_migrate)
    set_tests_properties(author_migrate PROPERTIES PASS_REGULAR_EXPRESSION "migrated runners")
    # Runners' level read through queries, typed by the game's catalog,
    # and a value read set back through a request to no change.
    add_test(NAME author_read
             COMMAND ${RAWFRAME_SHELL} ${CMAKE_CURRENT_SOURCE_DIR}/tests/author_read.sh $<TARGET_FILE:rawframe_host_author>
                     ${PROJECT_SOURCE_DIR} ${CMAKE_CURRENT_BINARY_DIR}/author_read)
    set_tests_properties(author_read PROPERTIES PASS_REGULAR_EXPRESSION "read runners")
    # Runners' platforms moved and reverted through their instances'
    # patches, and the game started on the changed level.
    add_test(NAME author_instances
             COMMAND ${RAWFRAME_SHELL} ${CMAKE_CURRENT_SOURCE_DIR}/tests/author_instances.sh $<TARGET_FILE:rawframe_host_author>
                     $<TARGET_FILE:rawframe_host_arena> ${PROJECT_SOURCE_DIR}
                     ${CMAKE_CURRENT_BINARY_DIR}/author_instances)
    set_tests_properties(author_instances PROPERTIES PASS_REGULAR_EXPRESSION "authored runners' instances")
    # Patrol's guards made from nothing, their enum field set by its cases'
    # names, and the game started on the scene made.
    add_test(NAME author_cases
             COMMAND ${RAWFRAME_SHELL} ${CMAKE_CURRENT_SOURCE_DIR}/tests/author_cases.sh $<TARGET_FILE:rawframe_host_author>
                     $<TARGET_FILE:rawframe_host_arena> ${PROJECT_SOURCE_DIR}
                     ${CMAKE_CURRENT_BINARY_DIR}/author_cases)
    set_tests_properties(author_cases PROPERTIES PASS_REGULAR_EXPRESSION "authored patrol's cases")
    # An authoring session on runners' level: undone to its very bytes,
    # redone, a stale step refused, the file changed under it opened again,
    # and records out of form or naming a scene outside its root refused.
    add_test(NAME author_session
             COMMAND ${RAWFRAME_SHELL} ${CMAKE_CURRENT_SOURCE_DIR}/tests/author_session.sh $<TARGET_FILE:rawframe_host_author>
                     ${PROJECT_SOURCE_DIR} ${CMAKE_CURRENT_BINARY_DIR}/author_session)
    set_tests_properties(author_session PROPERTIES PASS_REGULAR_EXPRESSION "authored runners in a session")
    # A session cooking its game as a long-running operation (D502): the
    # plaza cooked source by source while records are answered, then from
    # the cache; a second cook refused, one stopped as asked, one into the
    # game refused, and one stopped by the session's end.
    add_test(NAME author_cooks
             COMMAND ${RAWFRAME_SHELL} ${CMAKE_CURRENT_SOURCE_DIR}/tests/author_cooks.sh $<TARGET_FILE:rawframe_host_author>
                     $<TARGET_FILE:rawframe_host_cook_tool> ${PROJECT_SOURCE_DIR} ${CMAKE_CURRENT_BINARY_DIR}/author_cooks)
    # Files from outside made the game's assets through a session (D503):
    # a texture and a glTF with its image imported into a copy of the
    # plaza, declared, listed, and cooked; paths out of place refused.
    add_test(NAME author_imports
             COMMAND ${RAWFRAME_SHELL} ${CMAKE_CURRENT_SOURCE_DIR}/tests/author_imports.sh $<TARGET_FILE:rawframe_host_author>
                     $<TARGET_FILE:rawframe_host_cook_tool> ${PROJECT_SOURCE_DIR} ${CMAKE_CURRENT_BINARY_DIR}/author_imports)
    set_tests_properties(author_imports PROPERTIES
                         PASS_REGULAR_EXPRESSION "imported a texture and a mesh into the plaza and cooked them")
    set_tests_properties(author_cooks PROPERTIES
                         PASS_REGULAR_EXPRESSION "cooked the plaza through a session, [1-9][0-9]* sources")
    # A running dedicated server's World read through its tooling endpoint
    # (D408): admitted by its token, status twice as it ticks, an unknown
    # verb answered with an error, and another token refused.
    add_test(NAME author_tooling
             COMMAND ${RAWFRAME_SHELL} ${CMAKE_CURRENT_SOURCE_DIR}/tests/author_tooling.sh $<TARGET_FILE:rawframe_host_author>
                     $<TARGET_FILE:rawframe_host_dedicated_server> ${PROJECT_SOURCE_DIR}
                     ${CMAKE_CURRENT_BINARY_DIR}/author_tooling)
    set_tests_properties(author_tooling PROPERTIES PASS_REGULAR_EXPRESSION "inspected runners through the tooling endpoint")
    # A ray cast into a running World names what it meets and where an
    # author wrote it (D456): the plaza's gate post, by its id in the gate
    # scene.
    if(TARGET rawframe_host_client)
        add_test(NAME author_picks
                 COMMAND ${RAWFRAME_SHELL} ${CMAKE_CURRENT_SOURCE_DIR}/tests/author_picks.sh $<TARGET_FILE:rawframe_host_author>
                         $<TARGET_FILE:rawframe_host_dedicated_server> ${PROJECT_SOURCE_DIR}
                         ${PROJECT_BINARY_DIR}/hosts/arena/plaza_server.conf ${CMAKE_CURRENT_BINARY_DIR}/author_picks)
        set_tests_properties(author_picks PROPERTIES FIXTURES_REQUIRED plaza_content
                             PASS_REGULAR_EXPRESSION "picked the plaza's gate post through the tooling endpoint")
        # A game's scripts debugged through the tooling endpoint (D460): a
        # breakpoint at plaza's stroll stops the server in its tick, the
        # stopped frame read, other verbs refused, and the game carries on.
        add_test(NAME author_debugs
                 COMMAND ${RAWFRAME_SHELL} ${CMAKE_CURRENT_SOURCE_DIR}/tests/author_debugs.sh $<TARGET_FILE:rawframe_host_author>
                         $<TARGET_FILE:rawframe_host_dedicated_server> ${PROJECT_SOURCE_DIR}
                         ${PROJECT_BINARY_DIR}/hosts/arena/plaza_server.conf ${CMAKE_CURRENT_BINARY_DIR}/author_debugs)
        set_tests_properties(author_debugs PROPERTIES FIXTURES_REQUIRED plaza_content
                             PASS_REGULAR_EXPRESSION "stopped the plaza at its stroll system and carried on")
    endif()
    # A scene edited by an authoring session while a dedicated server plays
    # it (D410): the running World makes, writes, and lets go of the crate
    # as the scene changes, read through the tooling endpoint.
    add_test(NAME author_live
             COMMAND ${RAWFRAME_SHELL} ${CMAKE_CURRENT_SOURCE_DIR}/tests/author_live.sh $<TARGET_FILE:rawframe_host_author>
                     $<TARGET_FILE:rawframe_host_dedicated_server> ${PROJECT_SOURCE_DIR}
                     ${CMAKE_CURRENT_BINARY_DIR}/author_live)
    set_tests_properties(author_live PROPERTIES PASS_REGULAR_EXPRESSION "runners' level followed live")
endif()
