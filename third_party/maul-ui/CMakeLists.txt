cmake_minimum_required(VERSION 3.25)

# The library's identity. The family's CMake modules and tools read these.
set(MAUL_LIBRARY maul-ui)
set(MAUL_API_PREFIX mui)
set(MAUL_MACRO_PREFIX MUI)
set(MAUL_TITLE "Maul UI")
set(MAUL_PREFIX MAUL_UI)

include(cmake/ReadVersion.cmake)
maul_read_version(${CMAKE_CURRENT_SOURCE_DIR}/include/maul-ui/base.h MUI MAUL_VERSION)
project(maul-ui
    VERSION ${MAUL_VERSION}
    DESCRIPTION "An engine-agnostic retained UI core: layout, style, text, interaction, accessibility and a draw-command list"
    LANGUAGES C)

option(MAUL_UI_BUILD_TESTS "Build the test suites" ${PROJECT_IS_TOP_LEVEL})
option(MAUL_UI_BUILD_BENCH "Build the benchmarks" ${PROJECT_IS_TOP_LEVEL})
option(MAUL_UI_BUILD_SHARED "Build a shared library instead of a static one" OFF)
option(MAUL_UI_INSTALL "Generate install rules" ${PROJECT_IS_TOP_LEVEL})
option(MAUL_UI_WERROR "Treat compiler warnings as errors" OFF)
option(MAUL_UI_SANITIZE "Instrument with AddressSanitizer and UBSan" OFF)
option(MAUL_UI_TSAN "Instrument with ThreadSanitizer" OFF)
option(MAUL_UI_COVERAGE "Instrument for gcov coverage" OFF)
option(MAUL_UI_TEXT "Build the text component: fonts, shaping and paragraphs (record mui-0006)" ON)
option(MAUL_UI_TEXT_SYSTEM_LIBRARIES
    "Link the installed FreeType and HarfBuzz instead of building the pinned releases" OFF)

# Every executable (tests, benchmarks, tools) lands in the top of the
# build tree, so scripts and CI find them in one place.
if(PROJECT_IS_TOP_LEVEL)
    set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${PROJECT_BINARY_DIR})
endif()

# The text component's dependencies come before the family modules
# (cmake/TextDependencies.cmake says why).
if(MAUL_UI_TEXT)
    include(cmake/TextDependencies.cmake)
endif()

include(GNUInstallDirs)
include(cmake/CompilerFlags.cmake)
include(cmake/Library.cmake)
include(cmake/Testing.cmake)

if(MAUL_UI_BUILD_SHARED)
    set(MAUL_LIBRARY_KIND SHARED)
else()
    set(MAUL_LIBRARY_KIND STATIC)
endif()
add_library(maul-ui ${MAUL_LIBRARY_KIND}
    src/absolute.c
    src/animation.c
    src/allocator.c
    src/base.c
    src/color.c
    src/condition.c
    src/context.c
    src/draw.c
    src/easing.c
    src/flex.c
    src/flex_resolve.c
    src/inherit.c
    src/layout.c
    src/motion_math.c
    src/node.c
    src/node_style.c
    src/paint.c
    src/paint_host.c
    src/notify.c
    src/pool.c
    src/property.c
    src/restyle.c
    src/solve.c
    src/spring.c
    src/style.c
    src/style_edit.c
    src/transition.c
    src/token.c
    src/token_store.c
    src/theme.c
    src/theme_store.c
    src/tree.c)
maul_configure_library(maul-ui)
# fmaxf, fminf and fabsf, the exactly rounded functions the solver takes
# from libm (mui-0001).
if(NOT WIN32 AND NOT APPLE)
    target_link_libraries(maul-ui PUBLIC m)
    set(MAUL_PKG_LIBS_PRIVATE "-lm")
endif()
if(MAUL_UI_TEXT)
    include(cmake/Text.cmake)
endif()

if(MAUL_UI_BUILD_TESTS)
    enable_testing()
    maul_add_test(base)
    maul_add_test(condition)
    maul_add_test(context WHITEBOX)
    maul_add_test(draw)
    maul_add_test(layout)
    maul_add_test(layout_fixtures)
    maul_add_test(motion WHITEBOX)
    maul_add_test(node)
    maul_add_test(style)
    maul_add_test(text_style WHITEBOX)
    maul_add_test(theme)
    maul_add_test(token)
    maul_add_test(transition)
    maul_add_test(tree WHITEBOX)
    maul_add_test(visual WHITEBOX)
    if(MAUL_UI_TEXT)
        maul_add_test(text)
        mui_embed_file(${PROJECT_SOURCE_DIR}/test/fonts/Ahem.ttf
            ${PROJECT_BINARY_DIR}/test_fonts/ahem.inc s_ahem)
        target_include_directories(test_text PRIVATE ${PROJECT_BINARY_DIR}/test_fonts)
        maul_add_test(text_block)
        target_include_directories(test_text_block PRIVATE ${PROJECT_BINARY_DIR}/test_fonts)
    endif()
    # winnls.h defines MUI_ macros; this checks that none clash (family
    # record 0007).
    if(WIN32)
        maul_add_test(windows_headers)
    endif()
    maul_add_header_tests()
    # On the web, suites hold several default contexts (8.4 MB each) at
    # once, past the 16 MiB Emscripten starts with: they grow their
    # memory, as a page does.
    if(EMSCRIPTEN)
        get_property(mui_targets DIRECTORY PROPERTY BUILDSYSTEM_TARGETS)
        foreach(mui_target ${mui_targets})
            if(mui_target MATCHES "^test_")
                target_link_options(${mui_target} PRIVATE -sALLOW_MEMORY_GROWTH=1)
            endif()
        endforeach()
    endif()
endif()

if(MAUL_UI_BUILD_BENCH AND EXISTS ${PROJECT_SOURCE_DIR}/bench/bench_main.c)
    add_subdirectory(bench)
endif()

if(MAUL_UI_TEXT)
    mui_link_programs_as_c()
endif()

if(MAUL_UI_INSTALL)
    include(cmake/Install.cmake)
endif()
