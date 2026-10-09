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
option(MAUL_UI_BUILD_SAMPLES
    "Build the samples, where the renderer, the Maul Window glue and text are built" ${PROJECT_IS_TOP_LEVEL})
option(MAUL_UI_BUILD_SHARED "Build a shared library instead of a static one" OFF)
option(MAUL_UI_INSTALL "Generate install rules" ${PROJECT_IS_TOP_LEVEL})
option(MAUL_UI_WERROR "Treat compiler warnings as errors" OFF)
option(MAUL_UI_SANITIZE "Instrument with AddressSanitizer and UBSan" OFF)
option(MAUL_UI_TSAN "Instrument with ThreadSanitizer" OFF)
option(MAUL_UI_COVERAGE "Instrument for gcov coverage" OFF)
option(MAUL_UI_TEXT "Build the text component: fonts, shaping and paragraphs (record mui-0006)" ON)
option(MAUL_UI_FUZZ "Build the libFuzzer targets (Clang, with the text component)" OFF)
option(MAUL_UI_RHI "Build maul-ui-rhi, the reference renderer on Maul RHI (record mui-0005)" OFF)
option(MAUL_UI_WINDOW "Build maul-ui-window, the glue to Maul Window (record mui-0007)" OFF)
option(MAUL_UI_ACCESS_TREE
    "Build the accessibility tree's consumer, which platform adapters read (record mui-0008)" ON)
if(WIN32)
    option(MAUL_UI_UIA "Build the UI Automation adapter (record mui-0008)" ON)
else()
    set(MAUL_UI_UIA OFF)
endif()
if(CMAKE_SYSTEM_NAME STREQUAL "Darwin")
    option(MAUL_UI_NSACCESSIBILITY "Build the NSAccessibility adapter (record mui-0008)" ON)
else()
    set(MAUL_UI_NSACCESSIBILITY OFF)
endif()
if(CMAKE_SYSTEM_NAME STREQUAL "iOS")
    option(MAUL_UI_UIACCESSIBILITY "Build the UIAccessibility adapter (record mui-0008)" ON)
else()
    set(MAUL_UI_UIACCESSIBILITY OFF)
endif()
if(ANDROID)
    option(MAUL_UI_ANDROID_ACCESSIBILITY "Build the Android accessibility adapter (record mui-0008)" ON)
else()
    set(MAUL_UI_ANDROID_ACCESSIBILITY OFF)
endif()
if(EMSCRIPTEN)
    option(MAUL_UI_ARIA "Build the ARIA adapter (record mui-0008)" ON)
else()
    set(MAUL_UI_ARIA OFF)
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
    src/chars.c
    src/color.c
    src/condition.c
    src/context.c
    src/draw.c
    src/draw_transform.c
    src/easing.c
    src/flex.c
    src/flex_item.c
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
    src/layout_bound.c
    src/motion_math.c
    src/node.c
    src/node_style.c
    src/paint.c
    src/paint_host.c
    src/place.c
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
if(MAUL_UI_RHI)
    include(cmake/Rhi.cmake)
endif()
if(MAUL_UI_WINDOW)
    include(cmake/Window.cmake)
endif()
if(MAUL_UI_ACCESS_TREE)
    target_sources(maul-ui PRIVATE src/access_apply.c src/access_record.c src/access_tree.c
                                   src/access_view.c src/id_map.c)
endif()
if(MAUL_UI_NSACCESSIBILITY)
    if(NOT MAUL_UI_ACCESS_TREE)
        message(FATAL_ERROR "The NSAccessibility adapter reads the accessibility tree: set MAUL_UI_ACCESS_TREE")
    endif()
    # Objective-C with manual retain and release, as Maul Window's macOS
    # code.
    enable_language(OBJC)
    target_sources(maul-ui PRIVATE src/ns_adapter.m src/ns_events.m src/ns_node.m
                                   src/ns_roles.m)
    set_target_properties(maul-ui PROPERTIES
        OBJC_STANDARD 23
        OBJC_STANDARD_REQUIRED ON
        OBJC_EXTENSIONS OFF
        OBJC_VISIBILITY_PRESET hidden)
    target_compile_options(maul-ui PRIVATE
        $<$<COMPILE_LANGUAGE:OBJC>:-fno-objc-arc -Wmissing-prototypes>)
    target_link_libraries(maul-ui PRIVATE "-framework AppKit")
    string(APPEND MAUL_PKG_LIBS_PRIVATE " -framework AppKit")
endif()
if(MAUL_UI_UIACCESSIBILITY)
    if(NOT MAUL_UI_ACCESS_TREE)
        message(FATAL_ERROR "The UIAccessibility adapter reads the accessibility tree: set MAUL_UI_ACCESS_TREE")
    endif()
    # Objective-C with manual retain and release, as Maul Window's iOS
    # code.
    enable_language(OBJC)
    target_sources(maul-ui PRIVATE src/uikit_adapter.m src/uikit_events.m src/uikit_node.m
                                   src/uikit_traits.m)
    set_target_properties(maul-ui PROPERTIES
        OBJC_STANDARD 23
        OBJC_STANDARD_REQUIRED ON
        OBJC_EXTENSIONS OFF
        OBJC_VISIBILITY_PRESET hidden)
    target_compile_options(maul-ui PRIVATE
        $<$<COMPILE_LANGUAGE:OBJC>:-fno-objc-arc -Wmissing-prototypes>)
    # UIKit re-exports neither Foundation, Core Graphics nor the
    # Objective-C runtime, which a program linked as C needs named.
    target_link_libraries(maul-ui PRIVATE "-framework UIKit" "-framework Foundation"
                          "-framework CoreGraphics" objc)
    string(APPEND MAUL_PKG_LIBS_PRIVATE
           " -framework UIKit -framework Foundation -framework CoreGraphics -lobjc")
endif()
if(MAUL_UI_ANDROID_ACCESSIBILITY)
    if(NOT MAUL_UI_ACCESS_TREE)
        message(FATAL_ERROR "The Android accessibility adapter reads the accessibility tree: set MAUL_UI_ACCESS_TREE")
    endif()
    # The provider's Java is java/maul/ui/AccessProvider.java, which the
    # host builds into its application.
    target_sources(maul-ui PRIVATE src/android_adapter.c src/android_events.c src/android_jni.c
                                   src/android_node.c)
endif()
if(MAUL_UI_ARIA)
    if(NOT MAUL_UI_ACCESS_TREE)
        message(FATAL_ERROR "The ARIA adapter reads the accessibility tree: set MAUL_UI_ACCESS_TREE")
    endif()
    target_sources(maul-ui PRIVATE src/aria_adapter.c src/aria_elements.c src/aria_page.c)
endif()
if(MAUL_UI_ATSPI)
    if(NOT MAUL_UI_ACCESS_TREE)
        message(FATAL_ERROR "The AT-SPI adapter reads the accessibility tree: set MAUL_UI_ACCESS_TREE")
    endif()
    # libdbus-1 is opened at run time; dlopen is in libdl before glibc 2.34.
    target_sources(maul-ui PRIVATE src/atspi_actions.c src/atspi_app.c src/atspi_component.c
                                   src/atspi_events.c src/atspi_objects.c
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
    if(MAUL_UI_RHI)
        # The reference renderer on Maul RHI's test driver.
        maul_add_test(rhi_renderer)
        target_link_libraries(test_rhi_renderer PRIVATE maul-ui-rhi)
        # Its culling, packing lists without a device.
        maul_add_test(rhi_pack)
        target_link_libraries(test_rhi_pack PRIVATE maul-ui-rhi)
        target_include_directories(test_rhi_pack PRIVATE ${PROJECT_SOURCE_DIR}/rhi/src)
        # Its pixels on a real device; skips without one. On the web, in
        # headless Chrome's WebGPU through the runner, its waits sleeping
        # through JSPI, as the samples do.
        if(EMSCRIPTEN)
            maul_add_test(rhi_pixels WHITEBOX MANUAL)
            target_link_options(test_rhi_pixels PRIVATE -sJSPI -sEXIT_RUNTIME
                                                        -sEXPORTED_RUNTIME_METHODS=ENV)
            add_test(NAME rhi_pixels COMMAND node ${PROJECT_SOURCE_DIR}/test/web_runner.cjs
                                             $<TARGET_FILE:test_rhi_pixels>)
        else()
            maul_add_test(rhi_pixels WHITEBOX)
        endif()
        target_link_libraries(test_rhi_pixels PRIVATE maul-ui-rhi)
        set_tests_properties(rhi_pixels PROPERTIES SKIP_RETURN_CODE 77)
        # Glyph runs, in Ahem, where Maul UI has its text component.
        if(MAUL_UI_TEXT)
            foreach(test rhi_renderer rhi_pixels)
                target_include_directories(test_${test} PRIVATE ${PROJECT_BINARY_DIR}/test_fonts)
                target_compile_definitions(test_${test} PRIVATE MUI_TEST_TEXT=1)
            endforeach()
        endif()
    endif()
    if(MAUL_UI_WINDOW)
        # The glue on Maul Window's headless test backend.
        maul_add_test(window_glue)
        target_link_libraries(test_window_glue PRIVATE maul-ui-window)
        # Accessibility, a tree alone on the test backend.
        if(MAUL_UI_ACCESS_TREE)
            maul_add_test(window_access)
            target_link_libraries(test_window_access PRIVATE maul-ui-window)
        endif()
        # Compositions and text carets, in Ahem, where Maul UI has its text
        # component.
        if(MAUL_UI_TEXT)
            maul_add_test(window_text)
            target_link_libraries(test_window_text PRIVATE maul-ui-window)
            target_include_directories(test_window_text PRIVATE ${PROJECT_BINARY_DIR}/test_fonts)
        endif()
        # C23 asked of clang-cl directly, as for the glue (cmake/Window.cmake).
        if(MSVC)
            foreach(test window_glue window_access window_text)
                if(TARGET test_${test})
                    target_compile_options(test_${test} PRIVATE /clang:-std=c23)
                endif()
            endforeach()
        endif()
    endif()
    # The guide's snippets as written (family record 0019).
    maul_add_test(guide_model)
    maul_add_test(guide)
    maul_add_test(condition)
    maul_add_test(context WHITEBOX)
    maul_add_test(chars WHITEBOX)
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
    maul_add_test(work)
    maul_add_test(access)
    if(MAUL_UI_ACCESS_TREE)
        maul_add_test(access_tree)
        maul_add_test(access_wrap WHITEBOX)
        maul_add_test(id_map WHITEBOX)
    endif()
    if(MAUL_UI_NSACCESSIBILITY AND NOT MAUL_UI_BUILD_SHARED)
        # Asks the objects as the accessibility server does; whitebox.
        add_executable(test_nsaccessibility test/test_nsaccessibility.m)
        target_link_libraries(test_nsaccessibility PRIVATE maul-ui "-framework AppKit")
        target_include_directories(test_nsaccessibility PRIVATE ${PROJECT_SOURCE_DIR}/src)
        maul_apply_flags(test_nsaccessibility)
        set_target_properties(test_nsaccessibility PROPERTIES OBJC_STANDARD 23
                              OBJC_STANDARD_REQUIRED ON OBJC_EXTENSIONS OFF)
        target_compile_options(test_nsaccessibility PRIVATE -fno-objc-arc)
        add_test(NAME nsaccessibility COMMAND test_nsaccessibility)
    endif()
    if(MAUL_UI_UIACCESSIBILITY AND NOT MAUL_UI_BUILD_SHARED)
        # An application in the simulator, run by tools/run_ios_app.sh,
        # asking the objects as VoiceOver does; whitebox.
        add_executable(test_uiaccessibility MACOSX_BUNDLE test/test_uiaccessibility.m)
        target_link_libraries(test_uiaccessibility PRIVATE maul-ui "-framework UIKit")
        target_include_directories(test_uiaccessibility PRIVATE ${PROJECT_SOURCE_DIR}/src)
        maul_apply_flags(test_uiaccessibility)
        set_target_properties(test_uiaccessibility PROPERTIES OBJC_STANDARD 23
            OBJC_STANDARD_REQUIRED ON OBJC_EXTENSIONS OFF
            MACOSX_BUNDLE_INFO_PLIST ${PROJECT_SOURCE_DIR}/test/ios/Info.plist.in
            MACOSX_BUNDLE_GUI_IDENTIFIER org.maul.ui.test.uiaccessibility
            MACOSX_BUNDLE_BUNDLE_NAME test_uiaccessibility
            MACOSX_BUNDLE_BUNDLE_VERSION ${PROJECT_VERSION}
            MACOSX_BUNDLE_SHORT_VERSION_STRING ${PROJECT_VERSION})
        target_compile_options(test_uiaccessibility PRIVATE -fno-objc-arc)
        add_test(NAME uiaccessibility COMMAND ${PROJECT_SOURCE_DIR}/tools/run_ios_app.sh
                 $<TARGET_BUNDLE_DIR:test_uiaccessibility>)
        # The first application launch on a booted simulator has taken from
        # 20 seconds to over 220 (CI, October 2026); the boot step warms
        # it, and this is the bound past that.
        set_tests_properties(uiaccessibility PROPERTIES TIMEOUT 600)
    endif()
    if(MAUL_UI_ANDROID_ACCESSIBILITY AND NOT MAUL_UI_BUILD_SHARED AND DEFINED ENV{ANDROID_HOME})
        # An application in the emulator, built by tools/build_android_app.sh
        # without Gradle and run by tools/run_android_app.sh; its Java asks
        # the provider as clients do; whitebox.
        set(package maul.ui.tests.android_access)
        add_library(test_android_access SHARED test/test_android_access.c)
        target_link_libraries(test_android_access PRIVATE maul-ui)
        target_include_directories(test_android_access PRIVATE ${PROJECT_SOURCE_DIR}/src)
        maul_apply_flags(test_android_access)
        file(GLOB_RECURSE MUI_JAVA_SOURCES ${PROJECT_SOURCE_DIR}/java/*.java
             ${PROJECT_SOURCE_DIR}/test/android/java/*.java)
        add_custom_command(OUTPUT ${PROJECT_BINARY_DIR}/test_android_access.apk
            COMMAND ${CMAKE_COMMAND} -E env ANDROID_HOME=$ENV{ANDROID_HOME}
                    ${PROJECT_SOURCE_DIR}/tools/build_android_app.sh
                    ${PROJECT_BINARY_DIR}/test_android_access.apk
                    $<TARGET_FILE:test_android_access> ${package} ${ANDROID_ABI}
            DEPENDS test_android_access ${MUI_JAVA_SOURCES}
                    ${PROJECT_SOURCE_DIR}/test/android/AndroidManifest.xml
                    ${PROJECT_SOURCE_DIR}/tools/build_android_app.sh
            VERBATIM)
        add_custom_target(test_android_access_apk ALL
                          DEPENDS ${PROJECT_BINARY_DIR}/test_android_access.apk)
        add_test(NAME android_access COMMAND ${PROJECT_SOURCE_DIR}/tools/run_android_app.sh
                 ${PROJECT_BINARY_DIR}/test_android_access.apk ${package})
        set_tests_properties(android_access PROPERTIES TIMEOUT 240)
    endif()
    if(MAUL_UI_ARIA)
        # In headless Chrome through the runner, which waits on the test
        # (Asyncify lets it sleep); skipped without Puppeteer.
        maul_add_test(aria WHITEBOX MANUAL)
        target_link_options(test_aria PRIVATE -sASYNCIFY)
        add_test(NAME aria COMMAND ${CMAKE_CROSSCOMPILING_EMULATOR}
                                   ${PROJECT_SOURCE_DIR}/test/web_runner.cjs
                                   $<TARGET_FILE:test_aria>)
        set_tests_properties(aria PROPERTIES SKIP_RETURN_CODE 77)
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
    maul_add_test(layout_bound)
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
        mui_embed_file(${PROJECT_SOURCE_DIR}/test/fonts/MaulCff.otf
            ${PROJECT_BINARY_DIR}/test_fonts/cff.inc s_cff)
        mui_embed_file(${PROJECT_SOURCE_DIR}/test/fonts/MaulColor.ttf
            ${PROJECT_BINARY_DIR}/test_fonts/color.inc s_color)
        mui_embed_file(${PROJECT_SOURCE_DIR}/test/fonts/MaulBitmap.ttf
            ${PROJECT_BINARY_DIR}/test_fonts/bitmap.inc s_bitmap)
        mui_embed_file(${PROJECT_SOURCE_DIR}/test/fonts/MaulSbix.ttf
            ${PROJECT_BINARY_DIR}/test_fonts/sbix.inc s_sbix)
        mui_embed_file(${PROJECT_SOURCE_DIR}/test/images/pngs.bin
            ${PROJECT_BINARY_DIR}/test_fonts/pngs.inc s_pngs)
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
        maul_add_test(frame_memory)
        target_include_directories(test_frame_memory PRIVATE ${PROJECT_BINARY_DIR}/test_fonts)
        # Contexts on threads at once (record mui-0001): POSIX threads,
        # so not on Windows or the web.
        if(NOT EMSCRIPTEN)
            maul_add_test(threads POSIX)
            if(TARGET test_threads)
                target_include_directories(test_threads PRIVATE ${PROJECT_BINARY_DIR}/test_fonts)
            endif()
        endif()
        # Edits' analysis against the whole text's; the block's headers
        # reach FreeType's and HarfBuzz's.
        maul_add_test(text_analysis WHITEBOX)
        if(TARGET test_text_analysis)
            target_include_directories(test_text_analysis PRIVATE ${PROJECT_BINARY_DIR}/test_fonts)
        endif()
        if(TARGET test_text_analysis AND TARGET maul_ui_freetype)
            target_include_directories(test_text_analysis SYSTEM PRIVATE
                $<TARGET_PROPERTY:maul_ui_freetype,INTERFACE_INCLUDE_DIRECTORIES>
                $<TARGET_PROPERTY:harfbuzz::harfbuzz,INTERFACE_INCLUDE_DIRECTORIES>)
        elseif(TARGET test_text_analysis)
            target_link_libraries(test_text_analysis PRIVATE Freetype::Freetype harfbuzz::harfbuzz)
        endif()
        maul_add_test(text_edit)
        target_include_directories(test_text_edit PRIVATE ${PROJECT_BINARY_DIR}/test_fonts)
        maul_add_test(text_spans)
        target_include_directories(test_text_spans PRIVATE ${PROJECT_BINARY_DIR}/test_fonts)
        maul_add_test(guide_text)
        target_include_directories(test_guide_text PRIVATE ${PROJECT_BINARY_DIR}/test_fonts)
        maul_add_test(text_editor)
        target_include_directories(test_text_editor PRIVATE ${PROJECT_BINARY_DIR}/test_fonts)
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
        maul_add_test(field_quality)
        target_include_directories(test_field_quality PRIVATE ${PROJECT_BINARY_DIR}/test_fonts)
        maul_add_test(colr_composite WHITEBOX)
        maul_add_test(bitmap_glyph)
        target_include_directories(test_bitmap_glyph PRIVATE ${PROJECT_BINARY_DIR}/test_fonts)
        maul_add_test(color_glyph)
        target_include_directories(test_color_glyph PRIVATE ${PROJECT_BINARY_DIR}/test_fonts)
        maul_add_test(glyph_atlas)
        target_include_directories(test_glyph_atlas PRIVATE ${PROJECT_BINARY_DIR}/test_fonts)
        maul_add_test(skyline WHITEBOX)
        maul_add_test(glyph_table WHITEBOX)
        maul_add_test(font_key WHITEBOX)
        # Whitebox tests are not built for a shared library.
        if(TARGET test_font_key AND TARGET maul_ui_freetype)
            target_include_directories(test_font_key SYSTEM PRIVATE
                $<TARGET_PROPERTY:maul_ui_freetype,INTERFACE_INCLUDE_DIRECTORIES>
                $<TARGET_PROPERTY:harfbuzz::harfbuzz,INTERFACE_INCLUDE_DIRECTORIES>)
        elseif(TARGET test_font_key)
            target_link_libraries(test_font_key PRIVATE Freetype::Freetype harfbuzz::harfbuzz)
        endif()
        maul_add_test(inflate WHITEBOX)
        # Whitebox: not built against a shared library.
        maul_add_test(png WHITEBOX)
        maul_add_test(image_scale WHITEBOX)
        if(TARGET test_png)
            target_include_directories(test_png PRIVATE ${PROJECT_BINARY_DIR}/test_fonts)
        endif()
        maul_add_test(distance_field WHITEBOX)
        maul_add_test(multi_field WHITEBOX)
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

# The samples draw with the reference renderer through the Maul Window
# glue, with text (record mui-0005).
if(MAUL_UI_BUILD_SAMPLES AND MAUL_UI_RHI AND MAUL_UI_WINDOW AND MAUL_UI_TEXT)
    add_subdirectory(samples)
endif()

# The fuzz targets: test/fuzz_<area>.c, run as fuzz_<area> <corpus dir>,
# over the bytes the text component reads from fonts; tools/fuzz_seed.py
# writes their seeds.
if(MAUL_UI_FUZZ AND MAUL_UI_TEXT)
    # The library is instrumented too: its coverage guides libFuzzer, and
    # the sanitizers watch its code, not only the targets'.
    target_compile_options(maul-ui PRIVATE -fsanitize=fuzzer-no-link,address,undefined)
    target_link_options(maul-ui PUBLIC -fsanitize=address,undefined)
    foreach(area inflate png font text)
        add_executable(fuzz_${area} test/fuzz_${area}.c)
        target_link_libraries(fuzz_${area} PRIVATE maul-ui)
        # The targets reach the library's own functions.
        target_include_directories(fuzz_${area} PRIVATE ${PROJECT_SOURCE_DIR}/src)
        maul_apply_flags(fuzz_${area})
        target_compile_options(fuzz_${area} PRIVATE -fsanitize=fuzzer,address,undefined)
        target_link_options(fuzz_${area} PRIVATE -fsanitize=fuzzer,address,undefined)
    endforeach()
    # Text is laid out in a real font, kerned and with a line gap.
    mui_embed_file(${PROJECT_SOURCE_DIR}/test/fonts/LiberationSans-Regular.ttf
        ${PROJECT_BINARY_DIR}/test_fonts/liberation_sans.inc s_liberationSans)
    target_include_directories(fuzz_text PRIVATE ${PROJECT_BINARY_DIR}/test_fonts)
    # AT-SPI calls, answered with libdbus's message functions, which the
    # library loads at run time.
    if(MAUL_UI_ATSPI)
        add_executable(fuzz_atspi test/fuzz_atspi.c)
        target_link_libraries(fuzz_atspi PRIVATE maul-ui ${CMAKE_DL_LIBS})
        target_include_directories(fuzz_atspi PRIVATE ${PROJECT_SOURCE_DIR}/src)
        maul_apply_flags(fuzz_atspi)
        target_compile_options(fuzz_atspi PRIVATE -fsanitize=fuzzer,address,undefined)
        target_link_options(fuzz_atspi PRIVATE -fsanitize=fuzzer,address,undefined)
    endif()
endif()

if(MAUL_UI_TEXT)
    mui_link_programs_as_c()
endif()

if(MAUL_UI_INSTALL)
    # Without the text component its headers would declare functions the
    # library lacks (cmake/Install.cmake's MAUL_PUBLIC_HEADERS); they are
    # tools/text-headers.txt's, which check_text_headers.py keeps whole.
    if(NOT MAUL_UI_TEXT)
        file(GLOB MAUL_PUBLIC_HEADERS RELATIVE ${PROJECT_SOURCE_DIR}/include/maul-ui
             ${PROJECT_SOURCE_DIR}/include/maul-ui/*.h)
        file(STRINGS ${PROJECT_SOURCE_DIR}/tools/text-headers.txt mui_text_headers
             REGEX "^[a-z_]+\\.h$")
        list(REMOVE_ITEM MAUL_PUBLIC_HEADERS ${mui_text_headers})
    endif()
    include(cmake/Install.cmake)
endif()
