# Kest-scripted games in the World: Kest functions run as World systems over
# lent columns (written columns journaled and committed only when every call
# succeeds), and a participant that loads a game description. Separate from
# rawframe.world and rawframe.kest because neither should know the other.
rawframe_module(NAME world_kest
    SOURCES src/animation_doors.cpp src/animation_plan.cpp src/cooked_game.cpp src/cooked_mod.cpp src/game.cpp src/admission.cpp src/game_files.cpp src/game_participant.cpp src/field_kinds.cpp src/game_scenes.cpp src/kest_systems.cpp src/layouts.cpp src/mod.cpp src/mod_api.cpp src/mod_handlers.cpp src/mod_set.cpp src/spawn_scene.cpp src/physics_doors.cpp src/predictor.cpp src/replication.cpp
    DEPS world_animation animation mesh scene game_content content kest_library physics2d physics3d physics world_replication world_runtime world_save world_snapshot kest network world composition schema execution diagnostics document result base
)

# Games read from directories, written by the test or in the tree, where
# there are files; the web build runs the rest, which play the test games
# held in memory as a web client holds its files (D167).
set(rawframe_world_kest_directory_tests "")
if(NOT CMAKE_SYSTEM_NAME STREQUAL "WASI")
    set(rawframe_world_kest_directory_tests tests/animation_game_test.cpp tests/game_scene_test.cpp tests/game_test.cpp tests/persistent_test.cpp tests/save_game_test.cpp)
endif()
rawframe_module_tests(NAME world_kest SOURCES tests/admission_test.cpp tests/cooked_game_test.cpp tests/door_test.cpp tests/mod_api_test.cpp
    tests/held_game_test.cpp tests/kest_systems_test.cpp
    ${rawframe_world_kest_directory_tests})
if(TARGET rawframe_world_kest_tests)
    target_compile_definitions(rawframe_world_kest_tests PRIVATE
        RAWFRAME_WORLD_KEST_GAMES="${CMAKE_CURRENT_SOURCE_DIR}/tests/game/"
        RAWFRAME_SAMPLE_GAMES="${PROJECT_SOURCE_DIR}/games/"
        RAWFRAME_WORLD_KEST_MODULES="${PROJECT_SOURCE_DIR}/modules/kest_library/kest/")
endif()
