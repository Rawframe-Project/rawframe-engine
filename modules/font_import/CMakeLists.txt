# Font import (ADR-0049, ADR-0078, D385): TrueType and OpenType fonts and
# collections rebuilt by the OpenType Sanitizer, so a font's raw bytes never
# reach the runtime's FreeType and HarfBuzz. Import tooling only; no client
# or server closure may depend on it.
rawframe_module(NAME font_import SOURCES src/sanitize.cpp DEPS result base)
target_link_libraries(rawframe_font_import PRIVATE rawframe_ots)

rawframe_module_tests(NAME font_import SOURCES tests/sanitize_test.cpp)
if(TARGET rawframe_font_import_tests)
    target_compile_definitions(rawframe_font_import_tests PRIVATE
        RAWFRAME_FONT_IMPORT_FONTS="${RAWFRAME_TEST_FILES}/modules/ui/tests/fonts/")
endif()
rawframe_module_fuzz(NAME font_import TARGET sanitize RUNS 4000 MAX_LEN 32768 SEEDS ../ui/tests/fonts
    SOURCES tests/fuzz_sanitize.cpp)
