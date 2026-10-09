# Texture import (ADR-0058, D253): PNG, JPEG, BMP, and TGA sources decoded
# by Wuffs, halved into levels, and block-compressed by bc7enc into the
# runtime's texture. Import tooling only; no client or server closure may
# depend on it. Wuffs is memory-safe by construction, so the decoder may
# face files that are not an author's own.
rawframe_module(NAME texture_import
    SOURCES src/environment.cpp src/grading.cpp src/import.cpp src/radiance.cpp
    DEPS texture result base
)
target_link_libraries(rawframe_texture_import PRIVATE rawframe_wuffs rawframe_bc7enc)

rawframe_module_tests(NAME texture_import SOURCES tests/environment_test.cpp tests/grading_test.cpp tests/import_test.cpp)
if(TARGET rawframe_texture_import_tests)
    target_compile_definitions(rawframe_texture_import_tests PRIVATE
        RAWFRAME_TEXTURE_IMPORT_DATA="${RAWFRAME_TEST_FILES}/modules/texture_import/tests/data/")
endif()
rawframe_module_fuzz(NAME texture_import TARGET image RUNS 25000 MAX_LEN 16384 SEEDS tests/data SOURCES tests/fuzz_image.cpp)
rawframe_module_fuzz(NAME texture_import TARGET radiance RUNS 12000 MAX_LEN 4096 SEEDS tests/seeds/radiance SOURCES tests/fuzz_radiance.cpp)
rawframe_module_fuzz(NAME texture_import TARGET grading RUNS 12000 MAX_LEN 4096 SEEDS tests/seeds/grading SOURCES tests/fuzz_grading.cpp)
