cmake_minimum_required(VERSION 3.25)

# The library's identity. The family's CMake modules and tools read these.
set(MAUL_LIBRARY maul-unicode)
set(MAUL_API_PREFIX muni)
set(MAUL_MACRO_PREFIX MUNI)
set(MAUL_TITLE "Maul Unicode")
set(MAUL_PREFIX MAUL_UNICODE)

include(cmake/ReadVersion.cmake)
maul_read_version(${CMAKE_CURRENT_SOURCE_DIR}/include/maul-unicode/base.h MUNI MAUL_VERSION)
project(maul-unicode
    VERSION ${MAUL_VERSION}
    DESCRIPTION "Unicode algorithms and character properties: validation, segmentation, line breaking, bidi"
    LANGUAGES C)

option(MAUL_UNICODE_BUILD_TESTS "Build the test suites" ${PROJECT_IS_TOP_LEVEL})
option(MAUL_UNICODE_BUILD_BENCH "Build the benchmarks" ${PROJECT_IS_TOP_LEVEL})
option(MAUL_UNICODE_BUILD_SAMPLES "Build the samples" ${PROJECT_IS_TOP_LEVEL})
option(MAUL_UNICODE_BUILD_TOOLS "Build the table generator" ${PROJECT_IS_TOP_LEVEL})
option(MAUL_UNICODE_BUILD_SHARED "Build a shared library instead of a static one" OFF)
option(MAUL_UNICODE_INSTALL "Generate install rules" ${PROJECT_IS_TOP_LEVEL})
option(MAUL_UNICODE_WERROR "Treat compiler warnings as errors" OFF)
option(MAUL_UNICODE_SANITIZE "Instrument with AddressSanitizer and UBSan" OFF)
option(MAUL_UNICODE_TSAN "Instrument with ThreadSanitizer" OFF)
option(MAUL_UNICODE_COVERAGE "Instrument for gcov coverage" OFF)
option(MAUL_UNICODE_FUZZ "Build the libFuzzer targets (Clang only)" OFF)
option(MAUL_UNICODE_FUZZ_ICU "With the fuzz targets, build fuzz_icu, which compares with ICU" OFF)
# Components a build may leave out, with their code and their tables.
option(MAUL_UNICODE_NORMALIZATION "Include normalization (UAX #15)" ON)
option(MAUL_UNICODE_CASE "Include case mapping and folding" ON)
option(MAUL_UNICODE_SECURITY "Include confusable and mixed-script detection (UTS #39)" ON)
option(MAUL_UNICODE_HARFBUZZ "Build maul-unicode-harfbuzz, Unicode functions for HarfBuzz" OFF)

# Every executable (tests, samples, benchmarks, tools) lands in the top of the
# build tree, so scripts and CI find them in one place.
if(PROJECT_IS_TOP_LEVEL)
    set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${PROJECT_BINARY_DIR})
endif()

include(GNUInstallDirs)
include(cmake/CompilerFlags.cmake)
include(cmake/Library.cmake)
include(cmake/Testing.cmake)

if(MAUL_UNICODE_BUILD_SHARED)
    set(MAUL_LIBRARY_KIND SHARED)
else()
    set(MAUL_LIBRARY_KIND STATIC)
endif()
set(MUNI_SOURCES
    src/base.c
    src/bidi.c
    src/bidi_brackets.c
    src/bidi_explicit.c
    src/bidi_implicit.c
    src/bidi_line.c
    src/complex.c
    src/cursor.c
    src/encoding.c
    src/generated/bidi_bracket.c
    src/generated/bidi_class.c
    src/generated/bidi_mirror.c
    src/generated/bidi_mirror_deltas.c
    src/generated/canonical_combining_class.c
    src/generated/decimal_zeros.c
    src/generated/east_asian_width.c
    src/generated/emoji.c
    src/generated/general_category.c
    src/generated/grapheme_cluster_break.c
    src/generated/identifier.c
    src/generated/indic_conjunct_break.c
    src/generated/line_break.c
    src/generated/script.c
    src/generated/script_extensions.c
    src/generated/script_sets.c
    src/generated/script_tags.c
    src/generated/sentence_break.c
    src/generated/white_space.c
    src/generated/default_ignorable.c
    src/generated/word_break.c
    src/grapheme.c
    src/identifier.c
    src/line.c
    src/properties.c
    src/script_runs.c
    src/segment.c
    src/sentence.c
    src/word.c)
set(MUNI_NORMALIZATION_SOURCES
    src/decompose.c
    src/normalize.c
    src/generated/compatibility_blocks.c
    src/generated/compatibility_data.c
    src/generated/decomposition_blocks.c
    src/generated/decomposition_data.c)
set(MUNI_CASE_SOURCES
    src/case.c
    src/case_map.c
    src/generated/case.c
    src/generated/case_data.c
    src/generated/soft_dotted.c)
set(MUNI_SECURITY_SOURCES
    src/security.c
    src/skeleton.c
    src/generated/security.c
    src/generated/security_data.c)
if(MAUL_UNICODE_SECURITY AND NOT MAUL_UNICODE_NORMALIZATION)
    message(FATAL_ERROR "MAUL_UNICODE_SECURITY needs MAUL_UNICODE_NORMALIZATION")
endif()
if(MAUL_UNICODE_NORMALIZATION)
    list(APPEND MUNI_SOURCES ${MUNI_NORMALIZATION_SOURCES})
endif()
if(MAUL_UNICODE_CASE)
    list(APPEND MUNI_SOURCES ${MUNI_CASE_SOURCES})
endif()
if(MAUL_UNICODE_CASE AND MAUL_UNICODE_NORMALIZATION)
    list(APPEND MUNI_SOURCES src/nfkc_casefold.c)
endif()
if(MAUL_UNICODE_SECURITY)
    list(APPEND MUNI_SOURCES ${MUNI_SECURITY_SOURCES})
endif()
add_library(maul-unicode ${MAUL_LIBRARY_KIND} ${MUNI_SOURCES})
maul_configure_library(maul-unicode)

# The HarfBuzz functions (cmake/HarfBuzz.cmake).
if(MAUL_UNICODE_HARFBUZZ)
    if(NOT MAUL_UNICODE_NORMALIZATION)
        message(FATAL_ERROR "MAUL_UNICODE_HARFBUZZ needs MAUL_UNICODE_NORMALIZATION")
    endif()
    include(cmake/HarfBuzz.cmake)
endif()

if(MAUL_UNICODE_BUILD_TESTS)
    enable_testing()
    maul_add_test(base)
    maul_add_test(encoding)
    maul_add_test(properties WHITEBOX)
    maul_add_test(segment)
    maul_add_test(bidi)
    maul_add_test(script)
    maul_add_test(identifier WHITEBOX)
    set(MUNI_DATA_SUITES segment bidi)
    if(MAUL_UNICODE_NORMALIZATION)
        maul_add_test(normalize)
        list(APPEND MUNI_DATA_SUITES normalize)
    endif()
    if(MAUL_UNICODE_CASE)
        # With normalization, the case suite checks NFKC_Casefold against
        # DerivedNormalizationProps.txt, one of the generator's inputs.
        maul_add_test(case WHITEBOX)
        if(MAUL_UNICODE_NORMALIZATION AND TARGET test_case)
            target_compile_definitions(test_case PRIVATE MUNI_UCD_DATA="${PROJECT_SOURCE_DIR}/tools/ucd")
            list(APPEND MUNI_DATA_SUITES case)
        endif()
    endif()
    if(MAUL_UNICODE_SECURITY)
        # The security suite reads confusables.txt, one of the generator's
        # inputs; as a white-box suite, it is skipped in shared builds.
        maul_add_test(security WHITEBOX)
        if(TARGET test_security)
            target_compile_definitions(test_security PRIVATE MUNI_UCD_DATA="${PROJECT_SOURCE_DIR}/tools/ucd")
            list(APPEND MUNI_DATA_SUITES security)
        endif()
    endif()
    if(MAUL_UNICODE_HARFBUZZ)
        maul_add_test(harfbuzz)
        target_link_libraries(test_harfbuzz PRIVATE maul-unicode-harfbuzz)
    endif()
    # Conformance suites read the Unicode test files in test/data/.
    foreach(suite ${MUNI_DATA_SUITES})
        target_compile_definitions(test_${suite} PRIVATE MUNI_TEST_DATA="${PROJECT_SOURCE_DIR}/test/data")
        if(EMSCRIPTEN)
            target_link_options(test_${suite} PRIVATE -sNODERAWFS=1)
        endif()
        if(WIN32)
            target_compile_definitions(test_${suite} PRIVATE _CRT_SECURE_NO_WARNINGS)
        endif()
    endforeach()
    maul_add_header_tests()
endif()

# The fuzz targets: test/fuzz_<area>.c, run as fuzz_<area> <corpus dir>.
if(MAUL_UNICODE_FUZZ)
    # The library is instrumented too: its coverage guides libFuzzer, and
    # the sanitizers watch its code, not only the targets'. Whatever links
    # it (samples, benchmarks, tools) links the sanitizer runtimes.
    target_compile_options(maul-unicode PRIVATE -fsanitize=fuzzer-no-link,address,undefined)
    target_link_options(maul-unicode PUBLIC -fsanitize=address,undefined)
    set(MUNI_FUZZ_AREAS encoding segment bidi script)
    if(MAUL_UNICODE_NORMALIZATION)
        list(APPEND MUNI_FUZZ_AREAS normalize)
    endif()
    if(MAUL_UNICODE_CASE)
        list(APPEND MUNI_FUZZ_AREAS case)
    endif()
    if(MAUL_UNICODE_SECURITY)
        list(APPEND MUNI_FUZZ_AREAS security)
    endif()
    if(MAUL_UNICODE_NORMALIZATION AND MAUL_UNICODE_CASE)
        list(APPEND MUNI_FUZZ_AREAS casefold)
    endif()
    if(MAUL_UNICODE_FUZZ_ICU AND MAUL_UNICODE_NORMALIZATION AND MAUL_UNICODE_CASE)
        list(APPEND MUNI_FUZZ_AREAS icu)
    endif()
    foreach(area ${MUNI_FUZZ_AREAS})
        add_executable(fuzz_${area} test/fuzz_${area}.c)
        target_link_libraries(fuzz_${area} PRIVATE maul-unicode)
        maul_apply_flags(fuzz_${area})
        target_compile_options(fuzz_${area} PRIVATE -fsanitize=fuzzer,address,undefined)
        target_link_options(fuzz_${area} PRIVATE -fsanitize=fuzzer,address,undefined)
    endforeach()
    # ICU is an oracle for the tests alone, never linked into the library.
    if(TARGET fuzz_icu)
        find_package(PkgConfig REQUIRED)
        pkg_check_modules(MUNI_ICU REQUIRED IMPORTED_TARGET icu-uc)
        target_link_libraries(fuzz_icu PRIVATE PkgConfig::MUNI_ICU)
    endif()
endif()

# The generator writes src/generated/ from tools/ucd/:
#   munigen tools/ucd src/generated
if(MAUL_UNICODE_BUILD_TOOLS)
    add_executable(munigen tools/munigen/case.c tools/munigen/main.c tools/munigen/normalization.c
                           tools/munigen/properties.c tools/munigen/security.c
                           tools/munigen/table.c tools/munigen/ucd.c)
    maul_apply_flags(munigen)
    # The generator uses standard fopen, which the Windows C runtime
    # reports as deprecated in favor of its own fopen_s.
    if(WIN32)
        target_compile_definitions(munigen PRIVATE _CRT_SECURE_NO_WARNINGS)
    endif()
endif()

if(MAUL_UNICODE_BUILD_SAMPLES)
    add_subdirectory(samples)
endif()

# The benchmarks measure every component, so they need them all.
if(MAUL_UNICODE_BUILD_BENCH AND MAUL_UNICODE_NORMALIZATION AND MAUL_UNICODE_CASE AND
   MAUL_UNICODE_SECURITY AND
   EXISTS ${PROJECT_SOURCE_DIR}/bench/bench_main.c)
    add_subdirectory(bench)
    # The benchmark reads its baseline with standard fopen, which the
    # Windows C runtime reports as deprecated in favor of fopen_s.
    if(WIN32)
        target_compile_definitions(${PROJECT_NAME}_bench PRIVATE _CRT_SECURE_NO_WARNINGS)
    endif()
endif()

if(MAUL_UNICODE_INSTALL)
    include(cmake/Install.cmake)
endif()
