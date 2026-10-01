# A server and headless bots in one process over loopback, for measuring a
# networked Kest game (Tool role).
rawframe_host(NAME arena OUTPUT rawframe-arena
    SOURCES src/main.cpp
    DEPS host game_content input_kest world_audio world_localization network_loopback render_canvas world_ui render_scene physics2d physics3d world_animation world_replication world_kest world_runtime composition diagnostics result base
)
# Where Maul RHI is built, the arena can draw a client's canvas (D279) and
# scene (D284) offscreen on the one device; elsewhere it counts draws
# without one.
set(RAWFRAME_ARENA_DRAWS OFF)
if(TARGET rawframe_render_canvas_gpu)
    set(RAWFRAME_ARENA_DRAWS ON)
    target_link_libraries(rawframe_host_arena PRIVATE rawframe::render rawframe::render_canvas_gpu rawframe::render_scene_gpu)
endif()
target_compile_definitions(rawframe_host_arena PRIVATE RAWFRAME_ARENA_DRAWS=$<BOOL:${RAWFRAME_ARENA_DRAWS}>)

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
             COMMAND ${RAWFRAME_SHELL} ${CMAKE_CURRENT_SOURCE_DIR}/tests/players_saved.sh $<TARGET_FILE:rawframe_host_arena>
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
    # Eight bots play Stalls (D388): each joins as an owner and builds stalls,
    # each a static body of the lot beside its ground.
    add_test(NAME tycoon_bots
             COMMAND rawframe_host_arena --config ${CMAKE_CURRENT_SOURCE_DIR}/tests/tycoon.conf
             WORKING_DIRECTORY ${PROJECT_SOURCE_DIR})
    set_tests_properties(tycoon_bots PROPERTIES
                         PASS_REGULAR_EXPRESSION "\"bots\":8,\"admitted\":8.*\"bodiesMade\":([3-9]|[1-9][0-9]+),\"bodiesRemoved\":0,\"bodiesRefused\":0")
    # Stalls' sources cooked into the build tree: its font and text, read by
    # the drawn run (D389).
    set(TYCOON_CONTENT ${CMAKE_CURRENT_BINARY_DIR}/tycoon_content)
    add_test(NAME tycoon_cooked
             COMMAND rawframe_host_cook_tool ${PROJECT_SOURCE_DIR}/games/tycoon ${TYCOON_CONTENT}
                     ${CMAKE_CURRENT_BINARY_DIR}/tycoon_cook_cache)
    set_tests_properties(tycoon_cooked PROPERTIES
                         FIXTURES_SETUP tycoon_content
                         PASS_REGULAR_EXPRESSION "failed 0")
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
    # Heard is a peak of at least 0.1 here and below: on a busy machine,
    # where rollbacks replay effects, voices may sum past one.
    add_test(NAME runners_from_build
             COMMAND ${RAWFRAME_SHELL} ${CMAKE_CURRENT_SOURCE_DIR}/tests/runners_from_build.sh $<TARGET_FILE:rawframe_host_arena>
                     $<TARGET_FILE:rawframe_host_build_tool> ${PROJECT_SOURCE_DIR} ${CONTENT}
                     ${CMAKE_CURRENT_BINARY_DIR}/runners_from_build)
    set_tests_properties(runners_from_build PROPERTIES
                         FIXTURES_REQUIRED runners_content
                         PASS_REGULAR_EXPRESSION "\"peak\":(0\\.[1-9]|[1-9])[^,]*,\"cues\":[1-9]")
    # Runners composed of two Builds, the game and a Package holding its
    # shot's recording, each cooked, packed, and signed apart: shots are
    # heard with the Package, and without it nothing starts.
    add_test(NAME runners_with_package
             COMMAND ${RAWFRAME_SHELL} ${CMAKE_CURRENT_SOURCE_DIR}/tests/runners_with_package.sh $<TARGET_FILE:rawframe_host_arena>
                     $<TARGET_FILE:rawframe_host_cook_tool> $<TARGET_FILE:rawframe_host_build_tool>
                     ${PROJECT_SOURCE_DIR} ${CMAKE_CURRENT_BINARY_DIR}/runners_with_package)
    set_tests_properties(runners_with_package PROPERTIES
                         PASS_REGULAR_EXPRESSION "\"peak\":(0\\.[1-9]|[1-9])[^,]*,\"cues\":[1-9]")
    # Runners composed with the runners-timers and runners-sting mods, each
    # cooked, packed, and signed apart: the game takes them, the timers join
    # the World, and sting's untrusted handler counts every hit twice; a mod
    # outside the game's Mod API version is refused.
    add_test(NAME runners_with_mod
             COMMAND ${RAWFRAME_SHELL} ${CMAKE_CURRENT_SOURCE_DIR}/tests/runners_with_mod.sh $<TARGET_FILE:rawframe_host_arena>
                     $<TARGET_FILE:rawframe_host_cook_tool> $<TARGET_FILE:rawframe_host_build_tool>
                     ${PROJECT_SOURCE_DIR} ${CMAKE_CURRENT_BINARY_DIR}/runners_with_mod)
    set_tests_properties(runners_with_mod PROPERTIES
                         PROCESSORS 6
                         PASS_REGULAR_EXPRESSION "mod taken: [0-9]+ entities")
    # Runners' text read from its cooked content into one catalog, for a
    # player asking for Turkish.
    configure_file(tests/runners_text.conf.in ${CMAKE_CURRENT_BINARY_DIR}/runners_text.conf @ONLY)
    add_test(NAME runners_text
             COMMAND rawframe_host_arena --config ${CMAKE_CURRENT_BINARY_DIR}/runners_text.conf
             WORKING_DIRECTORY ${PROJECT_SOURCE_DIR})
    set_tests_properties(runners_text PROPERTIES
                         FIXTURES_REQUIRED runners_content
                         PASS_REGULAR_EXPRESSION "\"text_catalog\".*\"tables\":1,\"translations\":1,\"keys\":3,\"stale\":0,\"locale\":\"tr-TR\",\"default\":\"en\"")
    # Eight bots play runners and the first one's World is heard: its shots
    # and the others' within range sound, its own jumps sound as predicted
    # effects (D220), it winces when hit through its client's own emitter
    # (D272), and the recording is not silent.
    set(RECORD ${CMAKE_CURRENT_BINARY_DIR}/runners_heard.wav)
    configure_file(tests/runners_heard.conf.in ${CMAKE_CURRENT_BINARY_DIR}/runners_heard.conf @ONLY)
    add_test(NAME runners_heard
             COMMAND rawframe_host_arena --config ${CMAKE_CURRENT_BINARY_DIR}/runners_heard.conf
             WORKING_DIRECTORY ${PROJECT_SOURCE_DIR})
    set_tests_properties(runners_heard PROPERTIES
                         FIXTURES_REQUIRED runners_content
                         PASS_REGULAR_EXPRESSION
                         "\"written\":true,\"frames\":[0-9]+,\"peak\":(0\\.[1-9]|[1-9])[^,]*,\"cues\":[1-9][0-9]*,\"emitterCues\":\"[1-9][0-9]* [1-9][0-9]*\",\"effects\":[1-9]")
    # Four bots play runners and the first one's canvas is drawn offscreen
    # on the one device (D279): frames are drawn with their sprites, the
    # runners' trails and their shots' sparks (D357), each local player's
    # HUD over its view (D376) with its target image (D378) and shadow
    # (D381) and its score in words in the game's font (D386), and a frame
    # read back shows them. Only where Maul RHI is
    # built; a machine without an adapter skips unless the check requires
    # one.
    if(RAWFRAME_ARENA_DRAWS)
        set(CAPTURE ${CMAKE_CURRENT_BINARY_DIR}/runners_drawn.tga)
        configure_file(tests/runners_drawn.conf.in ${CMAKE_CURRENT_BINARY_DIR}/runners_drawn.conf @ONLY)
        add_test(NAME runners_drawn
                 COMMAND ${RAWFRAME_SHELL} ${CMAKE_CURRENT_SOURCE_DIR}/tests/runners_drawn.sh
                         $<TARGET_FILE:rawframe_host_arena> ${CMAKE_CURRENT_BINARY_DIR}/runners_drawn.conf
                 WORKING_DIRECTORY ${PROJECT_SOURCE_DIR})
        set_tests_properties(runners_drawn PROPERTIES
                             FIXTURES_REQUIRED runners_content
                             SKIP_REGULAR_EXPRESSION "skip: no device"
                             PASS_REGULAR_EXPRESSION
                             "\"code\":\"ui_drawing_summary\"[^\n]*\"frames\":[1-9][0-9]+,\"boxes\":[1-9][0-9]+,\"images\":[1-9][0-9]+,[^\n]*\"shadows\":[1-9][0-9]+.*\"code\":\"ui_summary\"[^\n]*\"leftOut\":0,\"mostNodes\":12,[^\n]*\"fontsRead\":1,\"texts\":[1-9][0-9]*,\"textsUnknown\":0,\"glyphRuns\":[1-9][0-9]*.*\"code\":\"drawing_summary\"[^\n]*\"frames\":[1-9][0-9]+,[^\n]*\"draws\":[1-9][0-9]+,[^\n]*\"emittersDrawn\":[1-9][0-9]*,\"emittersLeftOut\":0,\"particlesSpawned\":[1-9][0-9]*,\"ribbonsDrawn\":[1-9][0-9]*,\"playerFrames\":[1-9][0-9]*.*\"code\":\"frame_summary\"[^\n]*\"readBacks\":[1-9][0-9]*,\"coveredPixels\":[1-9][0-9]*,[^\n]*\"captured\":true")
        if(RAWFRAME_SANITIZE STREQUAL "thread")
            set_tests_properties(runners_drawn PROPERTIES ENVIRONMENT
                                 "TSAN_OPTIONS=suppressions=${PROJECT_SOURCE_DIR}/tools/lavapipe.tsan")
        endif()
    endif()
    # Runners' 16:9 view drawn into a 4:3 picture is letterboxed (D369): the
    # captured picture's bars are the game's slate and its view is not.
    if(RAWFRAME_ARENA_DRAWS)
        set(CAPTURE ${CMAKE_CURRENT_BINARY_DIR}/runners_boxed.tga)
        configure_file(tests/runners_boxed.conf.in ${CMAKE_CURRENT_BINARY_DIR}/runners_boxed.conf @ONLY)
        add_test(NAME runners_boxed
                 COMMAND ${RAWFRAME_SHELL} ${CMAKE_CURRENT_SOURCE_DIR}/tests/runners_boxed.sh
                         $<TARGET_FILE:rawframe_host_arena> ${CMAKE_CURRENT_BINARY_DIR}/runners_boxed.conf ${CAPTURE}
                 WORKING_DIRECTORY ${PROJECT_SOURCE_DIR})
        set_tests_properties(runners_boxed PROPERTIES
                             FIXTURES_REQUIRED runners_content
                             SKIP_REGULAR_EXPRESSION "skip: no device"
                             PASS_REGULAR_EXPRESSION "boxed: 120 of 120 bar rows slate, 0 view rows slate")
        if(RAWFRAME_SANITIZE STREQUAL "thread")
            set_tests_properties(runners_boxed PROPERTIES ENVIRONMENT
                                 "TSAN_OPTIONS=suppressions=${PROJECT_SOURCE_DIR}/tools/lavapipe.tsan")
        endif()
    endif()
    # Four bots play runners and the first one's World is played on a null
    # device: its thread renders the mixer, and shots reach the master bus.
    configure_file(tests/runners_played.conf.in ${CMAKE_CURRENT_BINARY_DIR}/runners_played.conf @ONLY)
    add_test(NAME runners_played
             COMMAND rawframe_host_arena --config ${CMAKE_CURRENT_BINARY_DIR}/runners_played.conf
             WORKING_DIRECTORY ${PROJECT_SOURCE_DIR})
    set_tests_properties(runners_played PROPERTIES
                         FIXTURES_REQUIRED runners_content
                         PASS_REGULAR_EXPRESSION "\"backend\":\"Null\",\"rate\":[0-9]+,\"lost\":false,\"callbacks\":[1-9][0-9]+,.*\"peak\":(0\\.[1-9]|[1-9])[^,]*,\"cues\":[1-9]")
    # Runners heard while its sources are recooked: the running client
    # publishes the changed manifest and the shot's new revision replaces
    # the old one, and shots are still heard. The arena, four bots, and a
    # recook run at once, so it holds processors as the other bot runs do:
    # squeezed beside the check's heaviest tests, its World fell behind
    # and the host stopped overloaded.
    add_test(NAME runners_recooked
             COMMAND ${RAWFRAME_SHELL} ${CMAKE_CURRENT_SOURCE_DIR}/tests/runners_recooked.sh $<TARGET_FILE:rawframe_host_arena>
                     $<TARGET_FILE:rawframe_host_cook_tool> ${PROJECT_SOURCE_DIR}
                     ${CMAKE_CURRENT_BINARY_DIR}/runners_recooked)
    set_tests_properties(runners_recooked PROPERTIES
                         PROCESSORS 6
                         PASS_REGULAR_EXPRESSION "\"peak\":(0\\.[1-9]|[1-9])[^,]*,\"cues\":[1-9]")
    # Four bots predict their runners: a thousand ticks confirmed, the
    # rollbacks only those of admission, their own jumps heard as
    # predicted effects (D219), and no checksum diverged (D274).
    add_test(NAME runners_prediction
             COMMAND rawframe_host_arena --config ${CMAKE_CURRENT_SOURCE_DIR}/tests/runners_prediction.conf
             WORKING_DIRECTORY ${PROJECT_SOURCE_DIR})
    set_tests_properties(runners_prediction PROPERTIES
                         PROCESSORS 6
                         PASS_REGULAR_EXPRESSION
                         "\"confirmed\":[1-9][0-9][0-9][0-9],\"rollbacks\":[0-9]?[0-9],.*\"effectsDelivered\":[1-9]"
                         FAIL_REGULAR_EXPRESSION "\"checksumsDiverged\":[1-9]")
    add_test(NAME runners_rollback_alarm
             COMMAND rawframe_host_arena --config ${CMAKE_CURRENT_SOURCE_DIR}/tests/runners_rollback_alarm.conf
             WORKING_DIRECTORY ${PROJECT_SOURCE_DIR})
    set_tests_properties(runners_rollback_alarm PROPERTIES
                         PROCESSORS 6
                         PASS_REGULAR_EXPRESSION "rollback_rate_alarm.*\"rollbackAlarms\":[1-9]")
    if(NOT RAWFRAME_SHIPPING)
        # Every record sent is found diverged, none is taken for verified,
        # and the players stay; both sides' captures of a diverged record
        # differ in the drill's one bit (D275).
        add_test(NAME runners_divergence_drill
                 COMMAND ${RAWFRAME_SHELL} ${CMAKE_CURRENT_SOURCE_DIR}/tests/runners_divergence_drill.sh $<TARGET_FILE:rawframe_host_arena>
                         ${PROJECT_SOURCE_DIR} ${CMAKE_CURRENT_BINARY_DIR}/runners_divergence_drill)
        set_tests_properties(runners_divergence_drill PROPERTIES
                             PROCESSORS 6
                             PASS_REGULAR_EXPRESSION "captured both sides of [1-9][0-9]* diverged records")
    endif()
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
                         PASS_REGULAR_EXPRESSION "\"bodiesMade\":33,\"bodiesRemoved\":0,\"bodiesRefused\":0,\"teleports\":0,.*\"targets\":[1-9][0-9]*,\"characterMoves\":[1-9][0-9]*,\"jointsMade\":1,\"jointsRemoved\":0,\"jointsRefused\":0.*\"instancesMade\":9,\"instancesRemoved\":0,\"animatorsRefused\":0,.*\"rootMotions\":[1-9][0-9]*,\"stagesRun\":[1-9][0-9]*")
    # Two bots play the sample 3D game while the first one's mirrored World
    # is seen in 3D (D283): its client dresses every body it mirrors by its
    # shape, the mound as the game's own mesh, and sees them through its
    # walker's camera; every model is drawn or culled, none malformed or of
    # a mesh unknown; the strollers' lanterns light the frames (D290);
    # the monitor's lens is seen into its render texture on demand, its
    # picture kept between (D361); and no record
    # loses fields past the 32 a record carries.
    configure_file(tests/plaza_seen.conf.in ${CMAKE_CURRENT_BINARY_DIR}/plaza_seen.conf @ONLY)
    add_test(NAME plaza_seen
             COMMAND rawframe_host_arena --config ${CMAKE_CURRENT_BINARY_DIR}/plaza_seen.conf
             WORKING_DIRECTORY ${PROJECT_SOURCE_DIR})
    set_tests_properties(plaza_seen PROPERTIES
                         FIXTURES_REQUIRED plaza_content
                         FAIL_REGULAR_EXPRESSION "\"truncated\""
                         PASS_REGULAR_EXPRESSION "\"frames\":[1-9][0-9]+,\"framesViewed\":[1-9][0-9]+,\"viewWidth\":1280,\"viewHeight\":720,\"modelsDrawn\":[1-9][0-9]+,\"culled\":[1-9][0-9]*,\"hidden\":[0-9]+,\"malformed\":0,\"unknownMeshes\":0,\"overLimit\":0,\"mostDraws\":[1-9][0-9]*,\"gameMeshes\":2,\"gameMaterials\":7,\"unknownMaterials\":0,\"materialTextures\":2,\"texturesReady\":2,\"pictures\":2,\"picturesReady\":2,\"lightsLit\":[1-9][0-9]*,\"lightsCulled\":[0-9]+,\"lightsOverLimit\":0,\"clusterOverflow\":0.*\"code\":\"scene_views_summary\"[^\n]*\"drawn\":[1-9][0-9]*,\"kept\":[1-9][0-9]+,\"missed\":[0-9]+,\"leftOut\":0,\"refused\":0")
    # At the low quality (D318), the paving samples no texture: the planks
    # are the one texture read.
    configure_file(tests/plaza_seen_low.conf.in ${CMAKE_CURRENT_BINARY_DIR}/plaza_seen_low.conf @ONLY)
    add_test(NAME plaza_seen_low
             COMMAND rawframe_host_arena --config ${CMAKE_CURRENT_BINARY_DIR}/plaza_seen_low.conf
             WORKING_DIRECTORY ${PROJECT_SOURCE_DIR})
    set_tests_properties(plaza_seen_low PROPERTIES
                         FIXTURES_REQUIRED plaza_content
                         PASS_REGULAR_EXPRESSION "\"gameMaterials\":7,\"unknownMaterials\":0,\"materialTextures\":1,\"texturesReady\":1,\"pictures\":2,")
    if(RAWFRAME_ARENA_DRAWS)
        # The plaza drawn on the one device offscreen (D284): frames drawn,
        # its models in instanced calls, its meshes uploaded once each, its
        # ambient occlusion found (D327), the monitor's lens drawn into its
        # render texture (D361), and the pictures read back lit in many
        # shades.
        set(CAPTURE ${CMAKE_CURRENT_BINARY_DIR}/plaza_drawn.tga)
        configure_file(tests/plaza_drawn.conf.in ${CMAKE_CURRENT_BINARY_DIR}/plaza_drawn.conf @ONLY)
        add_test(NAME plaza_drawn
                 COMMAND ${RAWFRAME_SHELL} ${CMAKE_CURRENT_SOURCE_DIR}/tests/runners_drawn.sh
                         $<TARGET_FILE:rawframe_host_arena> ${CMAKE_CURRENT_BINARY_DIR}/plaza_drawn.conf
                 WORKING_DIRECTORY ${PROJECT_SOURCE_DIR})
        set_tests_properties(plaza_drawn PROPERTIES
                             FIXTURES_REQUIRED plaza_content
                             SKIP_REGULAR_EXPRESSION "skip: no device"
                             PASS_REGULAR_EXPRESSION
                             "\"code\":\"scene_drawing_summary\"[^\n]*\"frames\":[1-9][0-9]+,[^\n]*\"models\":[1-9][0-9]+,\"drawCalls\":[1-9][0-9]+,\"modelsLeftOut\":0,\"meshesUploaded\":[3-9],[^\n]*\"framesOccluded\":[1-9][0-9]+,[^\n]*\"postProcessesRun\":[1-9][0-9]+,\"postProcessesLeftOut\":0,\"emittersDrawn\":[1-9][0-9]+,\"emittersLeftOut\":0,\"particlesSpawned\":[1-9][0-9]+,\"ribbonsDrawn\":[1-9][0-9]+,\"viewFrames\":[1-9][0-9]*.*\"code\":\"frame_summary\"[^\n]*\"readBacks\":[1-9][0-9]*,[^\n]*\"colors\":[1-9][0-9][0-9]+,\"captured\":true")
        if(RAWFRAME_SANITIZE STREQUAL "thread")
            set_tests_properties(plaza_drawn PROPERTIES ENVIRONMENT
                                 "TSAN_OPTIONS=suppressions=${PROJECT_SOURCE_DIR}/tools/lavapipe.tsan")
        endif()
        # Two bots play Stalls while the first one's view is drawn (D389): the
        # lot, the stalls, and its cursor as models seen from above, its
        # coins' words given in the game's font, and a picture read back.
        set(CAPTURE ${CMAKE_CURRENT_BINARY_DIR}/tycoon_drawn.tga)
        configure_file(tests/tycoon_drawn.conf.in ${CMAKE_CURRENT_BINARY_DIR}/tycoon_drawn.conf @ONLY)
        add_test(NAME tycoon_drawn
                 COMMAND ${RAWFRAME_SHELL} ${CMAKE_CURRENT_SOURCE_DIR}/tests/runners_drawn.sh
                         $<TARGET_FILE:rawframe_host_arena> ${CMAKE_CURRENT_BINARY_DIR}/tycoon_drawn.conf
                 WORKING_DIRECTORY ${PROJECT_SOURCE_DIR})
        set_tests_properties(tycoon_drawn PROPERTIES
                             FIXTURES_REQUIRED tycoon_content
                             SKIP_REGULAR_EXPRESSION "skip: no device"
                             PASS_REGULAR_EXPRESSION
                             "\"code\":\"ui_summary\"[^\n]*\"leftOut\":0,[^\n]*\"fontsRead\":1,\"texts\":[1-9][0-9]*,\"textsUnknown\":0,\"glyphRuns\":[1-9][0-9]*.*\"code\":\"scene_drawing_summary\"[^\n]*\"models\":[1-9][0-9]+,[^\n]*\"modelsLeftOut\":0,.*\"code\":\"frame_summary\"[^\n]*\"readBacks\":[1-9][0-9]*,[^\n]*\"colors\":[1-9][0-9]+,\"captured\":true")
        if(RAWFRAME_SANITIZE STREQUAL "thread")
            set_tests_properties(tycoon_drawn PROPERTIES ENVIRONMENT
                                 "TSAN_OPTIONS=suppressions=${PROJECT_SOURCE_DIR}/tools/lavapipe.tsan")
        endif()
        # Both bots' scenes drawn split-screen (D362): each local player's
        # view drawn into its half, and the picture read back in many
        # shades. The second's frames are not required: under a loaded full
        # check its bot may not see its World in the run (D365); the views'
        # placement is the GPU tests'.
        set(CAPTURE ${CMAKE_CURRENT_BINARY_DIR}/plaza_split.tga)
        configure_file(tests/plaza_split.conf.in ${CMAKE_CURRENT_BINARY_DIR}/plaza_split.conf @ONLY)
        add_test(NAME plaza_split
                 COMMAND ${RAWFRAME_SHELL} ${CMAKE_CURRENT_SOURCE_DIR}/tests/runners_drawn.sh
                         $<TARGET_FILE:rawframe_host_arena> ${CMAKE_CURRENT_BINARY_DIR}/plaza_split.conf
                 WORKING_DIRECTORY ${PROJECT_SOURCE_DIR})
        set_tests_properties(plaza_split PROPERTIES
                             FIXTURES_REQUIRED plaza_content
                             SKIP_REGULAR_EXPRESSION "skip: no device"
                             PASS_REGULAR_EXPRESSION
                             "\"code\":\"scene_drawing_summary\"[^\n]*\"frames\":[1-9][0-9]*,[^\n]*\"regionFrames\":[1-9][0-9]+.*\"code\":\"scene_summary\"[^\n]*\"players\":2,\"playerFrames\":[0-9]+.*\"code\":\"frame_summary\"[^\n]*\"readBacks\":[1-9][0-9]*,[^\n]*\"colors\":[1-9][0-9][0-9]+,\"captured\":true")
        if(RAWFRAME_SANITIZE STREQUAL "thread")
            set_tests_properties(plaza_split PROPERTIES ENVIRONMENT
                                 "TSAN_OPTIONS=suppressions=${PROJECT_SOURCE_DIR}/tools/lavapipe.tsan")
        endif()
    endif()
    # Four bots predict their walkers in 3D: at least a thousand ticks
    # confirmed, the rollbacks only those of admission, and no checksum
    # diverged (D274).
    add_test(NAME plaza_prediction
             COMMAND rawframe_host_arena --config ${CMAKE_CURRENT_BINARY_DIR}/plaza_prediction.conf
             WORKING_DIRECTORY ${PROJECT_SOURCE_DIR})
    set_tests_properties(plaza_prediction PROPERTIES
                         FIXTURES_REQUIRED plaza_content
                         PROCESSORS 6
                         PASS_REGULAR_EXPRESSION "\"confirmed\":[1-9][0-9][0-9][0-9],\"rollbacks\":[0-9]?[0-9],"
                         FAIL_REGULAR_EXPRESSION "\"checksumsDiverged\":[1-9]")
endif()
