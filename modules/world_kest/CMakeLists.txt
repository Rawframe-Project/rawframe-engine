# Kest-scripted games in the World: Kest functions run as World systems over
# lent columns (written columns journaled and committed only when every call
# succeeds), and a participant that loads a game description. Separate from
# rawframe.world and rawframe.kest because neither should know the other.
rawframe_module(NAME world_kest
    SOURCES src/cooked_game.cpp src/game.cpp src/game_files.cpp src/game_participant.cpp src/kest_systems.cpp src/layouts.cpp src/spawn_scene.cpp src/physics_doors.cpp src/predictor.cpp src/replication.cpp
    DEPS scene game_content content kest_library physics2d physics3d physics world_replication world_runtime world_snapshot kest network world composition schema execution diagnostics document result base
)

rawframe_module_tests(NAME world_kest SOURCES tests/cooked_game_test.cpp tests/game_scene_test.cpp tests/game_test.cpp tests/kest_systems_test.cpp)
if(TARGET rawframe_world_kest_tests)
    target_compile_definitions(rawframe_world_kest_tests PRIVATE
        RAWFRAME_WORLD_KEST_GAMES="${CMAKE_CURRENT_SOURCE_DIR}/tests/game/"
        RAWFRAME_SAMPLE_GAMES="${PROJECT_SOURCE_DIR}/games/"
        RAWFRAME_WORLD_KEST_MODULES="${PROJECT_SOURCE_DIR}/modules/kest_library/kest/")
endif()
