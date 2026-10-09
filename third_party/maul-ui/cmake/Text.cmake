# The text component (record mui-0006): its modules, built into maul-ui
# together with the objects of FreeType, HarfBuzz and Maul Unicode, so
# the library carries what it needs, installs as one archive and links
# with the C linker (HarfBuzz needs no C++ runtime). Only the text
# modules read their headers.

target_sources(maul-ui PRIVATE
    src/bitmap_glyph.c
    src/color_glyph.c
    src/colr_composite.c
    src/colr_gradient.c
    src/colr_paint.c
    src/distance_field.c
    src/flatten.c
    src/family_store.c
    src/font.c
    src/font_chain.c
    src/font_family.c
    src/font_instance.c
    src/font_store.c
    src/glyph_atlas.c
    src/glyph_image.c
    src/glyph_outline.c
    src/glyph_table.c
    src/image_scale.c
    src/inflate.c
    src/line_break.c
    src/multi_field.c
    src/paint_source.c
    src/png.c
    src/skyline.c
    src/text_block.c
    src/text_blocks.c
    src/text_boxes.c
    src/text_delete.c
    src/text_hit.c
    src/text_move.c
    src/text_layout.c
    src/text_lines.c
    src/text_paragraph.c
    src/text_service.c
    src/text_editing.c
    src/text_editor.c
    src/text_history.c
    src/text_keys.c
    src/text_mask.c
    src/text_rules.c
    src/text_runs.c
    src/text_shape.c
    $<TARGET_OBJECTS:maul-unicode>
    $<TARGET_OBJECTS:maul-unicode-harfbuzz>)
target_include_directories(maul-ui SYSTEM PRIVATE
    $<TARGET_PROPERTY:maul-unicode,INTERFACE_INCLUDE_DIRECTORIES>)

# HarfBuzz's objects would make the library, and programs linking it,
# link as C++; Visual Studio projects then apply only C++ settings and
# drop C23. It links as C, needing no C++ runtime.
set_target_properties(maul-ui PROPERTIES LINKER_LANGUAGE C)
# Linking as C, CMake still names the C++ compiler's own libraries,
# where the NDK's "c++" is a script for its shared runtime whatever
# ANDROID_STL chose: with the static runtime (the NDK's default), the
# static one is named instead.
if(ANDROID AND NOT ANDROID_STL MATCHES "_shared$")
    list(TRANSFORM CMAKE_CXX_IMPLICIT_LINK_LIBRARIES REPLACE "^c\\+\\+$" "c++_static;c++abi")
endif()

if(MAUL_UI_TEXT_SYSTEM_LIBRARIES)
    target_link_libraries(maul-ui PRIVATE Freetype::Freetype harfbuzz::harfbuzz)
else()
    target_sources(maul-ui PRIVATE
        $<TARGET_OBJECTS:maul_ui_freetype>
        $<TARGET_OBJECTS:maul_ui_harfbuzz>)
    target_include_directories(maul-ui SYSTEM PRIVATE
        $<TARGET_PROPERTY:maul_ui_freetype,INTERFACE_INCLUDE_DIRECTORIES>
        $<TARGET_PROPERTY:maul_ui_harfbuzz,INTERFACE_INCLUDE_DIRECTORIES>)
    # HarfBuzz's locks, where the C library does not hold them already;
    # the web builds HarfBuzz without threads (TextDependencies.cmake).
    if(NOT WIN32 AND NOT EMSCRIPTEN AND NOT CMAKE_SYSTEM_NAME STREQUAL "WASI")
        find_package(Threads REQUIRED)
        target_link_libraries(maul-ui PUBLIC ${CMAKE_THREAD_LIBS_INIT})
        string(STRIP "${MAUL_PKG_LIBS_PRIVATE} ${CMAKE_THREAD_LIBS_INIT}" MAUL_PKG_LIBS_PRIVATE)
    endif()
endif()

# A shared library exports Maul UI's functions alone: FreeType marks its
# own for export, which would put them beside a program's own FreeType.
if(MAUL_UI_BUILD_SHARED AND NOT WIN32)
    if(APPLE)
        file(WRITE ${PROJECT_BINARY_DIR}/maul-ui.exported "_mui*\n")
        target_link_options(maul-ui PRIVATE
            "LINKER:-exported_symbols_list,${PROJECT_BINARY_DIR}/maul-ui.exported")
    else()
        file(WRITE ${PROJECT_BINARY_DIR}/maul-ui.map "{\n  global: mui*;\n  local: *;\n};\n")
        target_link_options(maul-ui PRIVATE "LINKER:--version-script=${PROJECT_BINARY_DIR}/maul-ui.map")
    endif()
endif()

# mui_link_programs_as_c() links the executables of this directory and
# its subdirectories as C: the library they link holds HarfBuzz's C++
# objects, which would make them link as C++ and, in Visual Studio
# projects, lose their C standard.
function(mui_link_programs_as_c)
    get_property(directories DIRECTORY PROPERTY SUBDIRECTORIES)
    foreach(directory ${CMAKE_CURRENT_SOURCE_DIR} ${directories})
        get_property(targets DIRECTORY ${directory} PROPERTY BUILDSYSTEM_TARGETS)
        foreach(target ${targets})
            get_target_property(type ${target} TYPE)
            if(type STREQUAL "EXECUTABLE")
                set_target_properties(${target} PROPERTIES LINKER_LANGUAGE C)
            endif()
        endforeach()
    endforeach()
endfunction()

# mui_embed_file(<file> <output> <name>) writes a file's bytes as a C
# array named name, for tests that read fonts on every platform, the web
# included.
function(mui_embed_file file output name)
    file(READ ${file} hex HEX)
    string(REGEX REPLACE "([0-9a-f][0-9a-f])" "0x\\1," bytes "${hex}")
    file(WRITE ${output}.new "static const unsigned char ${name}[] = {${bytes}};\n")
    configure_file(${output}.new ${output} COPYONLY)
endfunction()
