# Semantic input (ADR-0037, SPEC-0029): action sets, and the mapping from
# device controls to per-player action state. Client only: the dedicated
# server links none of it.
rawframe_module(NAME input
    SOURCES src/controls.cpp src/actions.cpp src/mapper.cpp src/overrides.cpp src/conflicts.cpp src/feed.cpp src/pairing.cpp src/touch.cpp
    DEPS document result base
)

rawframe_module_tests(NAME input SOURCES tests/actions_test.cpp tests/mapper_test.cpp tests/overrides_test.cpp tests/pairing_test.cpp tests/touch_test.cpp)
rawframe_module_fuzz(NAME input TARGET actions RUNS 20000 SEEDS tests/seeds SOURCES tests/fuzz_actions.cpp)
if(TARGET rawframe_input_fuzz_actions)
    # Overrides are read over the runners game's action set.
    target_compile_definitions(rawframe_input_fuzz_actions PRIVATE
        RAWFRAME_RUNNERS_ACTIONS="${PROJECT_SOURCE_DIR}/games/runners/runners.actions")
endif()
