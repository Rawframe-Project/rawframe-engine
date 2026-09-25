# A server and headless bots in one process over loopback, for measuring a
# networked Kest game (Tool role).
rawframe_host(NAME arena OUTPUT rawframe-arena
    SOURCES src/main.cpp
    DEPS host game_content input_kest world_audio network_loopback physics2d physics3d world_replication world_kest world_runtime composition diagnostics result base
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
    # Runners' sources cooked into the build tree: the content its heard
    # and played runs read their sounds from, by identity.
    set(CONTENT ${CMAKE_CURRENT_BINARY_DIR}/runners_content)
    add_test(NAME runners_cooked
             COMMAND rawframe_host_cook_tool ${PROJECT_SOURCE_DIR}/games/runners ${CONTENT}
                     ${CMAKE_CURRENT_BINARY_DIR}/runners_cook_cache)
    set_tests_properties(runners_cooked PROPERTIES
                         FIXTURES_SETUP runners_content
                         PASS_REGULAR_EXPRESSION "failed 0")
    # Runners' cooked content packed into a Build from its receipt.
    add_test(NAME runners_built
             COMMAND rawframe_host_build_tool ${CONTENT} ${CMAKE_CURRENT_BINARY_DIR}/runners_build rawframe/runners
                     0.1.0 linux x86_64 client build.development tool)
    set_tests_properties(runners_built PROPERTIES
                         FIXTURES_REQUIRED runners_content
                         PASS_REGULAR_EXPRESSION "build sha256:[0-9a-f]+, manifest sha256:[0-9a-f]+, 1 resources")
    # Eight bots play runners and the first one's World is heard: its shots
    # and the others' within range sound, and the recording is not silent.
    set(RECORD ${CMAKE_CURRENT_BINARY_DIR}/runners_heard.wav)
    configure_file(tests/runners_heard.conf.in ${CMAKE_CURRENT_BINARY_DIR}/runners_heard.conf @ONLY)
    add_test(NAME runners_heard
             COMMAND rawframe_host_arena --config ${CMAKE_CURRENT_BINARY_DIR}/runners_heard.conf
             WORKING_DIRECTORY ${PROJECT_SOURCE_DIR})
    set_tests_properties(runners_heard PROPERTIES
                         FIXTURES_REQUIRED runners_content
                         PASS_REGULAR_EXPRESSION "\"written\":true,\"frames\":[0-9]+,\"peak\":0\\.[1-9][^,]*,\"cues\":[1-9]")
    # Four bots play runners and the first one's World is played on a null
    # device: its thread renders the mixer, and shots reach the master bus.
    configure_file(tests/runners_played.conf.in ${CMAKE_CURRENT_BINARY_DIR}/runners_played.conf @ONLY)
    add_test(NAME runners_played
             COMMAND rawframe_host_arena --config ${CMAKE_CURRENT_BINARY_DIR}/runners_played.conf
             WORKING_DIRECTORY ${PROJECT_SOURCE_DIR})
    set_tests_properties(runners_played PROPERTIES
                         FIXTURES_REQUIRED runners_content
                         PASS_REGULAR_EXPRESSION "\"backend\":\"Null\",\"rate\":[0-9]+,\"lost\":false,\"callbacks\":[1-9][0-9]+,.*\"peak\":0\\.[1-9][^,]*,\"cues\":[1-9]")
    # Runners heard while its sources are recooked: the running client
    # publishes the changed manifest and the shot's new revision replaces
    # the old one, and shots are still heard.
    add_test(NAME runners_recooked
             COMMAND ${CMAKE_CURRENT_SOURCE_DIR}/tests/runners_recooked.sh $<TARGET_FILE:rawframe_host_arena>
                     $<TARGET_FILE:rawframe_host_cook_tool> ${PROJECT_SOURCE_DIR}
                     ${CMAKE_CURRENT_BINARY_DIR}/runners_recooked)
    set_tests_properties(runners_recooked PROPERTIES
                         PASS_REGULAR_EXPRESSION "\"peak\":0\\.[1-9][^,]*,\"cues\":[1-9]")
    # Four bots predict their runners: a thousand ticks confirmed, the
    # rollbacks only those of admission.
    add_test(NAME runners_prediction
             COMMAND rawframe_host_arena --config ${CMAKE_CURRENT_SOURCE_DIR}/tests/runners_prediction.conf
             WORKING_DIRECTORY ${PROJECT_SOURCE_DIR})
    set_tests_properties(runners_prediction PROPERTIES
                         PASS_REGULAR_EXPRESSION "\"confirmed\":1[0-9][0-9][0-9],\"rollbacks\":[0-9]?[0-9],")
    # Sixteen bots play the sample 3D game: ten bodies of the plaza and one
    # walker for each player are made, and the walkers move as characters.
    add_test(NAME plaza_bots
             COMMAND rawframe_host_arena --config ${CMAKE_CURRENT_SOURCE_DIR}/tests/plaza.conf
             WORKING_DIRECTORY ${PROJECT_SOURCE_DIR})
    set_tests_properties(plaza_bots PROPERTIES
                         PASS_REGULAR_EXPRESSION "\"bodiesMade\":26,\"bodiesRemoved\":0,\"bodiesRefused\":0,.*\"characterMoves\":[1-9]")
    # Four bots predict their walkers in 3D: a thousand ticks confirmed, the
    # rollbacks only those of admission.
    add_test(NAME plaza_prediction
             COMMAND rawframe_host_arena --config ${CMAKE_CURRENT_SOURCE_DIR}/tests/plaza_prediction.conf
             WORKING_DIRECTORY ${PROJECT_SOURCE_DIR})
    set_tests_properties(plaza_prediction PROPERTIES
                         PASS_REGULAR_EXPRESSION "\"confirmed\":1[0-9][0-9][0-9],\"rollbacks\":[0-9]?[0-9],")
endif()
