# Cooking (ADR-0024): sidecar-identified sources through registered
# importers into content-addressed artifacts, a content manifest, and a
# receipt. Import tooling only; no client or server closure may depend on it.
rawframe_module(NAME cook
    SOURCES src/animation.cpp src/audio.cpp src/cook.cpp src/font.cpp src/game.cpp src/kest.cpp src/material.cpp src/mesh.cpp src/mod.cpp src/project.cpp src/scene.cpp
            src/text.cpp src/texture.cpp
    DEPS process localization animation material graph mesh_import mesh texture_import texture font_import ui scene world_kest input kest_library kest audio_import audio content document result base
)

rawframe_module_tests(NAME cook SOURCES tests/cook_test.cpp tests/font_test.cpp tests/material_test.cpp tests/texture_test.cpp)
if(TARGET rawframe_cook_tests)
    target_compile_definitions(rawframe_cook_tests PRIVATE
        RAWFRAME_COOK_DATA="${PROJECT_SOURCE_DIR}/modules/audio_import/tests/data/"
        RAWFRAME_SAMPLE_GAMES="${PROJECT_SOURCE_DIR}/games/"
        RAWFRAME_MATERIAL_SEEDS="${PROJECT_SOURCE_DIR}/modules/material/tests/canvas_seeds/"
        RAWFRAME_SURFACE_SEEDS="${PROJECT_SOURCE_DIR}/modules/material/tests/seeds/"
        RAWFRAME_SHADER_GENERATOR="${PROJECT_SOURCE_DIR}/tools/gen_shaders.py"
        RAWFRAME_TEXTURE_DATA="${PROJECT_SOURCE_DIR}/modules/texture_import/tests/data/"
        RAWFRAME_UI_FONTS="${PROJECT_SOURCE_DIR}/modules/ui/tests/fonts/")
endif()
