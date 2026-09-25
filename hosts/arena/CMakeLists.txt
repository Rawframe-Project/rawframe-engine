# A server and headless bots in one process over loopback, for measuring a
# networked Kest game (Tool role).
rawframe_host(NAME arena OUTPUT rawframe-arena
    SOURCES src/main.cpp
    DEPS host network_loopback physics2d physics3d world_replication world_kest world_runtime composition diagnostics result base
)

if(RAWFRAME_BUILD_TESTS)
    # Sixteen bots join the sample arena game, play, and the run stops in
    # order with every bot admitted. Paths are relative to the repository.
    add_test(NAME arena_bots
             COMMAND rawframe_host_arena --config ${CMAKE_CURRENT_SOURCE_DIR}/tests/arena.conf
             WORKING_DIRECTORY ${PROJECT_SOURCE_DIR})
    set_tests_properties(arena_bots PROPERTIES
                         PASS_REGULAR_EXPRESSION "\"bots\":16,\"admitted\":16")
    # Sixteen bots play the sample physics game: eleven bodies of the level
    # and one ball for each player are made, and nothing is refused.
    add_test(NAME crates_bots
             COMMAND rawframe_host_arena --config ${CMAKE_CURRENT_SOURCE_DIR}/tests/crates.conf
             WORKING_DIRECTORY ${PROJECT_SOURCE_DIR})
    set_tests_properties(crates_bots PROPERTIES
                         PASS_REGULAR_EXPRESSION "\"bodiesMade\":27,\"bodiesRemoved\":0,\"bodiesRefused\":0")
    # One bot predicts its ball: hundreds of ticks confirmed, the few
    # rollbacks those of admission.
    add_test(NAME crates_prediction
             COMMAND rawframe_host_arena --config ${CMAKE_CURRENT_SOURCE_DIR}/tests/crates_prediction.conf
             WORKING_DIRECTORY ${PROJECT_SOURCE_DIR})
    set_tests_properties(crates_prediction PROPERTIES
                         PASS_REGULAR_EXPRESSION "\"confirmed\":[1-9][0-9][0-9],\"rollbacks\":[0-9][0-9]?,")
    # Sixteen bots play the sample platformer: nine bodies of the level and
    # one runner for each player are made, and the runners move as
    # characters.
    add_test(NAME runners_bots
             COMMAND rawframe_host_arena --config ${CMAKE_CURRENT_SOURCE_DIR}/tests/runners.conf
             WORKING_DIRECTORY ${PROJECT_SOURCE_DIR})
    set_tests_properties(runners_bots PROPERTIES
                         PASS_REGULAR_EXPRESSION "\"bodiesMade\":25,\"bodiesRemoved\":0,\"bodiesRefused\":0,.*\"characterMoves\":[1-9]")
    # Four bots predict their runners: a thousand ticks confirmed, the
    # rollbacks only those of admission.
    add_test(NAME runners_prediction
             COMMAND rawframe_host_arena --config ${CMAKE_CURRENT_SOURCE_DIR}/tests/runners_prediction.conf
             WORKING_DIRECTORY ${PROJECT_SOURCE_DIR})
    set_tests_properties(runners_prediction PROPERTIES
                         PASS_REGULAR_EXPRESSION "\"confirmed\":1[0-9][0-9][0-9],\"rollbacks\":[0-9]?[0-9],")
endif()
