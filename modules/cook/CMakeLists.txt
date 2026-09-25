# Cooking (ADR-0024): sidecar-identified sources through registered
# importers into content-addressed artifacts, a content manifest, and a
# receipt. Import tooling only; no client or server closure may depend on it.
rawframe_module(NAME cook
    SOURCES src/audio.cpp src/cook.cpp src/game.cpp src/kest.cpp
    DEPS scene world_kest input kest_library kest audio_import audio content document result base
)

rawframe_module_tests(NAME cook SOURCES tests/cook_test.cpp)
if(TARGET rawframe_cook_tests)
    target_compile_definitions(rawframe_cook_tests PRIVATE
        RAWFRAME_COOK_DATA="${PROJECT_SOURCE_DIR}/modules/audio_import/tests/data/"
        RAWFRAME_SAMPLE_GAMES="${PROJECT_SOURCE_DIR}/games/")
endif()
