# A server and headless bots in one process over loopback, for measuring a
# networked Kest game (Tool role).
rawframe_host(NAME arena OUTPUT rawframe-arena
    SOURCES src/main.cpp
    DEPS host game_content input_kest world_audio network_loopback physics2d physics3d world_animation world_replication world_kest world_runtime composition diagnostics result base
)

if(RAWFRAME_BUILD_TESTS)
    # Sixteen bots join the sample arena game, play, and the run stops in
    # order with every bot admitted. Paths are relative to the repository.
    add_test(NAME arena_bots
             COMMAND rawframe_host_arena --config ${CMAKE_CURRENT_SOURCE_DIR}/tests/arena.conf
             WORKING_DIRECTORY ${PROJECT_SOURCE_DIR})
    set_tests_properties(arena_bots PROPERTIES
                         PASS_REGULAR_EXPRESSION "\"bots\":16,\"admitted\":16")
    # Runners' players keep their scores from one arena run to the next.
    add_test(NAME arena_players_saved
             COMMAND ${CMAKE_CURRENT_SOURCE_DIR}/tests/players_saved.sh $<TARGET_FILE:rawframe_host_arena>
             WORKING_DIRECTORY ${PROJECT_SOURCE_DIR})
    set_tests_properties(arena_players_saved PROPERTIES PASS_REGULAR_EXPRESSION "players kept across runs")
    # A server with room for two players tells a third it is full.
    add_test(NAME arena_full
             COMMAND rawframe_host_arena --config ${CMAKE_CURRENT_SOURCE_DIR}/tests/arena_full.conf
             WORKING_DIRECTORY ${PROJECT_SOURCE_DIR})
    set_tests_properties(arena_full PROPERTIES
                         PASS_REGULAR_EXPRESSION "\"bots\":3,\"admitted\":2,\"unavailable\":0,\"serverStopping\":0,\"full\":1,")
    # A game's own admission rule, in Kest, admits bots with the right
    # ticket and tells the others their ticket is invalid.
    add_test(NAME arena_gate_pass
             COMMAND rawframe_host_arena --config ${CMAKE_CURRENT_SOURCE_DIR}/tests/gate_pass.conf
             WORKING_DIRECTORY ${PROJECT_SOURCE_DIR})
    set_tests_properties(arena_gate_pass PROPERTIES
                         PASS_REGULAR_EXPRESSION "\"bots\":2,\"admitted\":2,.*\"ticketInvalid\":0")
    add_test(NAME arena_gate_nope
             COMMAND rawframe_host_arena --config ${CMAKE_CURRENT_SOURCE_DIR}/tests/gate_nope.conf
             WORKING_DIRECTORY ${PROJECT_SOURCE_DIR})
    set_tests_properties(arena_gate_nope PROPERTIES
                         PASS_REGULAR_EXPRESSION "\"bots\":2,\"admitted\":0,.*\"ticketInvalid\":2")
    # Sixteen bots play the sample physics game: eleven bodies of the level
    # and one ball for each player are made, and nothing is refused.
    add_test(NAME crates_bots
             COMMAND rawframe_host_arena --config ${CMAKE_CURRENT_SOURCE_DIR}/tests/crates.conf
             WORKING_DIRECTORY ${PROJECT_SOURCE_DIR})
    set_tests_properties(crates_bots PROPERTIES
                         PASS_REGULAR_EXPRESSION "\"bodiesMade\":27,\"bodiesRemoved\":0,\"bodiesRefused\":0")
    # The prediction runs count ticks confirmed in wall time, so each holds
    # six of ctest's processors: a machine running every preset at once
    # starved them into too few confirmations (D83).
    # One bot predicts its ball: hundreds of ticks confirmed, the few
    # rollbacks those of admission.
    add_test(NAME crates_prediction
             COMMAND rawframe_host_arena --config ${CMAKE_CURRENT_SOURCE_DIR}/tests/crates_prediction.conf
             WORKING_DIRECTORY ${PROJECT_SOURCE_DIR})
    set_tests_properties(crates_prediction PROPERTIES
                         PROCESSORS 6
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
    # Runners' cooked content packed into a Build from its receipt, and a
    # process started outside the repository that reads the game itself and
    # its sounds from the Build, named by its Composition: shots are heard.
    add_test(NAME runners_from_build
             COMMAND ${CMAKE_CURRENT_SOURCE_DIR}/tests/runners_from_build.sh $<TARGET_FILE:rawframe_host_arena>
                     $<TARGET_FILE:rawframe_host_build_tool> ${PROJECT_SOURCE_DIR} ${CONTENT}
                     ${CMAKE_CURRENT_BINARY_DIR}/runners_from_build)
    set_tests_properties(runners_from_build PROPERTIES
                         FIXTURES_REQUIRED runners_content
                         PASS_REGULAR_EXPRESSION "\"peak\":0\\.[1-9][^,]*,\"cues\":[1-9]")
    # Runners composed of two Builds, the game and a Package holding its
    # shot's recording, each cooked, packed, and signed apart: shots are
    # heard with the Package, and without it nothing starts.
    add_test(NAME runners_with_package
             COMMAND ${CMAKE_CURRENT_SOURCE_DIR}/tests/runners_with_package.sh $<TARGET_FILE:rawframe_host_arena>
                     $<TARGET_FILE:rawframe_host_cook_tool> $<TARGET_FILE:rawframe_host_build_tool>
                     ${PROJECT_SOURCE_DIR} ${CMAKE_CURRENT_BINARY_DIR}/runners_with_package)
    set_tests_properties(runners_with_package PROPERTIES
                         PASS_REGULAR_EXPRESSION "\"peak\":0\\.[1-9][^,]*,\"cues\":[1-9]")
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
                         PROCESSORS 6
                         PASS_REGULAR_EXPRESSION "\"confirmed\":1[0-9][0-9][0-9],\"rollbacks\":[0-9]?[0-9],")
    # The plaza's sources cooked into the build tree: its mound's mesh.
    set(PLAZA_CONTENT ${CMAKE_CURRENT_BINARY_DIR}/plaza_content)
    add_test(NAME plaza_cooked
             COMMAND rawframe_host_cook_tool ${PROJECT_SOURCE_DIR}/games/plaza ${PLAZA_CONTENT}
                     ${CMAKE_CURRENT_BINARY_DIR}/plaza_cook_cache)
    set_tests_properties(plaza_cooked PROPERTIES
                         FIXTURES_SETUP plaza_content
                         PASS_REGULAR_EXPRESSION "failed 0")
    configure_file(tests/plaza.conf.in ${CMAKE_CURRENT_BINARY_DIR}/plaza.conf @ONLY)
    configure_file(tests/plaza_prediction.conf.in ${CMAKE_CURRENT_BINARY_DIR}/plaza_prediction.conf @ONLY)
    # Sixteen bots play the sample 3D game: fourteen bodies of the plaza, its
    # mound a cooked mesh among them, its gate's hinge, and one walker for
    # each player are made, and the walkers move as characters. The blade's
    # swing is played from its cooked graph, and its body carried to its
    # bone by kinematic targets, never teleported. Eight strollers walk by
    # root motion through their blend space, their heads turned by a stage
    # the server runs.
    add_test(NAME plaza_bots
             COMMAND rawframe_host_arena --config ${CMAKE_CURRENT_BINARY_DIR}/plaza.conf
             WORKING_DIRECTORY ${PROJECT_SOURCE_DIR})
    set_tests_properties(plaza_bots PROPERTIES
                         FIXTURES_REQUIRED plaza_content
                         PASS_REGULAR_EXPRESSION "\"bodiesMade\":30,\"bodiesRemoved\":0,\"bodiesRefused\":0,\"teleports\":0,.*\"targets\":[1-9][0-9]*,\"characterMoves\":[1-9][0-9]*,\"jointsMade\":1,\"jointsRemoved\":0,\"jointsRefused\":0.*\"instancesMade\":9,\"instancesRemoved\":0,\"animatorsRefused\":0,.*\"rootMotions\":[1-9][0-9]*,\"stagesRun\":[1-9][0-9]*")
    # Four bots predict their walkers in 3D: a thousand ticks confirmed, the
    # rollbacks only those of admission.
    add_test(NAME plaza_prediction
             COMMAND rawframe_host_arena --config ${CMAKE_CURRENT_BINARY_DIR}/plaza_prediction.conf
             WORKING_DIRECTORY ${PROJECT_SOURCE_DIR})
    set_tests_properties(plaza_prediction PROPERTIES
                         FIXTURES_REQUIRED plaza_content
                         PROCESSORS 6
                         PASS_REGULAR_EXPRESSION "\"confirmed\":1[0-9][0-9][0-9],\"rollbacks\":[0-9]?[0-9],")
endif()
