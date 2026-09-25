# Kest-scripted games in the World: Kest functions run as World systems over
# lent columns (written columns journaled and committed only when every call
# succeeds), and a participant that loads a game description. Separate from
# rawframe.world and rawframe.kest because neither should know the other.
rawframe_module(NAME world_kest
    SOURCES src/game.cpp src/game_participant.cpp src/kest_systems.cpp src/physics_doors.cpp src/predictor.cpp src/replication.cpp
    DEPS physics2d physics3d physics world_replication world_runtime world_snapshot kest network world composition schema execution diagnostics result base
)

rawframe_module_tests(NAME world_kest SOURCES tests/game_test.cpp tests/kest_systems_test.cpp)
if(TARGET rawframe_world_kest_tests)
    target_compile_definitions(rawframe_world_kest_tests PRIVATE
        RAWFRAME_KEST_LIBRARY="${RAWFRAME_KEST_LIBRARY}"
        RAWFRAME_WORLD_KEST_GAMES="${CMAKE_CURRENT_SOURCE_DIR}/tests/game/"
        RAWFRAME_SAMPLE_GAMES="${PROJECT_SOURCE_DIR}/games/"
        RAWFRAME_WORLD_KEST_MODULES="${CMAKE_CURRENT_SOURCE_DIR}/kest/")
endif()
