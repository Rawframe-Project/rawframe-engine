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
option(MAUL_UI_ACCESS_TREE
    "Build the accessibility tree's consumer, which platform adapters read (record mui-0008)" ON)
if(WIN32)
    option(MAUL_UI_UIA "Build the UI Automation adapter (record mui-0008)" ON)
else()
    set(MAUL_UI_UIA OFF)
endif()
if(CMAKE_SYSTEM_NAME STREQUAL "Linux")
    option(MAUL_UI_ATSPI "Build the AT-SPI adapter (record mui-0008)" ON)
else()
    set(MAUL_UI_ATSPI OFF)
endif()
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
    src/hit.c
    src/inherit.c
    src/event.c
    src/focus.c
    src/layer.c
    src/navigate.c
    src/pointer.c
    src/exit.c
    src/popup.c
    src/virtual.c
    src/virtual_edit.c
    src/access.c
    src/access_build.c
    src/access_action.c
    src/range.c
    src/scroll.c
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
if(MAUL_UI_ACCESS_TREE)
    target_sources(maul-ui PRIVATE src/access_apply.c src/access_tree.c src/access_view.c
                                   src/id_map.c)
endif()
if(MAUL_UI_ATSPI)
    if(NOT MAUL_UI_ACCESS_TREE)
        message(FATAL_ERROR "The AT-SPI adapter reads the accessibility tree: set MAUL_UI_ACCESS_TREE")
    endif()
    # libdbus-1 is opened at run time; dlopen is in libdl before glibc 2.34.
    target_sources(maul-ui PRIVATE src/atspi_app.c src/atspi_component.c src/atspi_objects.c
                                   src/atspi_properties.c src/atspi_roles.c src/dbus_api.c)
    target_link_libraries(maul-ui PUBLIC ${CMAKE_DL_LIBS})
    if(CMAKE_DL_LIBS)
        string(APPEND MAUL_PKG_LIBS_PRIVATE " -l${CMAKE_DL_LIBS}")
    endif()
endif()
if(MAUL_UI_UIA)
    if(NOT MAUL_UI_ACCESS_TREE)
        message(FATAL_ERROR "The UI Automation adapter reads the accessibility tree: set MAUL_UI_ACCESS_TREE")
    endif()
    # UI Automation's own functions are loaded at run time, as MinGW has
    # no import library of them; COM's come from ole32 and oleaut32.
    target_sources(maul-ui PRIVATE src/uia_adapter.c src/uia_events.c src/uia_node.c
                                   src/uia_patterns.c src/uia_props.c src/uia_values.c)
    target_link_libraries(maul-ui PUBLIC ole32 oleaut32)
    set(MAUL_PKG_LIBS_PRIVATE "-lole32 -loleaut32")
endif()

if(MAUL_UI_BUILD_TESTS)
    enable_testing()
    maul_add_test(base)
    maul_add_test(condition)
    maul_add_test(context WHITEBOX)
    maul_add_test(draw)
    maul_add_test(interaction)
    maul_add_test(pointer)
    maul_add_test(focus)
    maul_add_test(navigate)
    maul_add_test(event)
    maul_add_test(scroll)
    maul_add_test(wheel)
    maul_add_test(scroll_keys)
    maul_add_test(drag)
    maul_add_test(range)
    maul_add_test(popup)
    maul_add_test(exit)
    maul_add_test(virtual)
    maul_add_test(access)
    if(MAUL_UI_ACCESS_TREE)
        maul_add_test(access_tree)
        maul_add_test(id_map WHITEBOX)
    endif()
    if(MAUL_UI_ATSPI)
        # On a private bus, the test playing the registry and the client.
        maul_add_test(atspi WHITEBOX POSIX)
    endif()
    if(MAUL_UI_UIA)
        # Driven through IUIAutomation from a second thread.
        maul_add_test(uia WHITEBOX)
        target_link_libraries(test_uia PRIVATE user32 ole32 oleaut32 uuid)
    endif()
    maul_add_test(pan)
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
        mui_embed_file(${PROJECT_SOURCE_DIR}/test/fonts/LiberationSans-Regular.ttf
            ${PROJECT_BINARY_DIR}/test_fonts/liberation_sans.inc s_liberationSans)
        mui_embed_file(${PROJECT_SOURCE_DIR}/test/fonts/MaulBreakTest.ttf
            ${PROJECT_BINARY_DIR}/test_fonts/break_test.inc s_breakTest)
        mui_embed_file(${PROJECT_SOURCE_DIR}/test/fonts/MaulLargeGlyph.ttf
            ${PROJECT_BINARY_DIR}/test_fonts/large_glyph.inc s_largeGlyph)
        mui_embed_file(${PROJECT_SOURCE_DIR}/test/fonts/MaulOverlap.ttf
            ${PROJECT_BINARY_DIR}/test_fonts/overlap.inc s_overlap)
        mui_embed_file(${PROJECT_SOURCE_DIR}/test/fonts/MaulVariable.ttf
            ${PROJECT_BINARY_DIR}/test_fonts/variable.inc s_variable)
        mui_embed_file(${PROJECT_SOURCE_DIR}/test/fonts/MaulVariableSlant.ttf
            ${PROJECT_BINARY_DIR}/test_fonts/variable_slant.inc s_variableSlant)
        mui_embed_file(${PROJECT_SOURCE_DIR}/test/fonts/MaulVariableItalic.ttf
            ${PROJECT_BINARY_DIR}/test_fonts/variable_italic.inc s_variableItalic)
        mui_embed_file(${PROJECT_SOURCE_DIR}/test/fonts/MaulItalic.ttf
            ${PROJECT_BINARY_DIR}/test_fonts/italic.inc s_italic)
        mui_embed_file(${PROJECT_SOURCE_DIR}/test/fonts/MaulCoverage.ttf
            ${PROJECT_BINARY_DIR}/test_fonts/coverage.inc s_coverage)
        maul_add_test(text_block)
        target_include_directories(test_text_block PRIVATE ${PROJECT_BINARY_DIR}/test_fonts)
        maul_add_test(text_edit)
        target_include_directories(test_text_edit PRIVATE ${PROJECT_BINARY_DIR}/test_fonts)
        maul_add_test(text_fuzz)
        target_include_directories(test_text_fuzz PRIVATE ${PROJECT_BINARY_DIR}/test_fonts)
        maul_add_test(font_fallback)
        target_include_directories(test_font_fallback PRIVATE ${PROJECT_BINARY_DIR}/test_fonts)
        maul_add_test(font_family)
        target_include_directories(test_font_family PRIVATE ${PROJECT_BINARY_DIR}/test_fonts)
        maul_add_test(font_instance)
        target_include_directories(test_font_instance PRIVATE ${PROJECT_BINARY_DIR}/test_fonts)
        maul_add_test(glyph_image)
        target_include_directories(test_glyph_image PRIVATE ${PROJECT_BINARY_DIR}/test_fonts)
        maul_add_test(glyph_atlas)
        target_include_directories(test_glyph_atlas PRIVATE ${PROJECT_BINARY_DIR}/test_fonts)
        maul_add_test(skyline WHITEBOX)
        maul_add_test(distance_field WHITEBOX)
        # Whitebox tests are not built for a shared library.
        if(TARGET test_distance_field AND TARGET maul_ui_freetype)
            target_include_directories(test_distance_field SYSTEM PRIVATE
                $<TARGET_PROPERTY:maul_ui_freetype,INTERFACE_INCLUDE_DIRECTORIES>)
        elseif(TARGET test_distance_field)
            target_link_libraries(test_distance_field PRIVATE Freetype::Freetype)
        endif()
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
    # Without the text component its headers would declare functions the
    # library lacks (cmake/Install.cmake's MAUL_PUBLIC_HEADERS).
    if(NOT MAUL_UI_TEXT)
        file(GLOB MAUL_PUBLIC_HEADERS RELATIVE ${PROJECT_SOURCE_DIR}/include/maul-ui
             ${PROJECT_SOURCE_DIR}/include/maul-ui/*.h)
        list(REMOVE_ITEM MAUL_PUBLIC_HEADERS font.h glyph_atlas.h glyph_image.h text.h
             text_block.h text_edit.h)
    endif()
    include(cmake/Install.cmake)
endif()
