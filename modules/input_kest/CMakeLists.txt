# A client's input the way ADR-0037 makes it for a Kest-scripted game: the
# game's action set and mapper, the `Input.*` doors its sample program reads,
# and input sources for bots with a hand on the controls. Client only: the
# dedicated server links none of it.
rawframe_module(NAME input_kest
    SOURCES src/participant.cpp src/sources.cpp
    DEPS input document world_kest world_replication kest world composition execution diagnostics result base
)

rawframe_module_tests(NAME input_kest SOURCES tests/sources_test.cpp)
if(TARGET rawframe_input_kest_tests)
    target_compile_definitions(rawframe_input_kest_tests PRIVATE
        RAWFRAME_SAMPLE_GAMES="${PROJECT_SOURCE_DIR}/games/")
endif()
