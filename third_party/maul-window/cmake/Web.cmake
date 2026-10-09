# The web backend. Its page side is JavaScript in the library's objects
# (EM_JS): with Emscripten the program's link takes it in and nothing
# else is needed. Without Emscripten (wasm32-wasi) the same functions are
# imports, and the build writes maul-window.mjs, which gives the page
# them (mwin-0022); the library's MAUL_WINDOW_WEB_GLUE property names it.

set(MWIN_WEB_SOURCES
    src/backend_web.c
    src/web_clipboard.c
    src/web_cursor.c
    src/web_drop.c
    src/web_input.c
    src/web_message_box.c
    src/web_page.c
    src/web_services.c
    src/web_text.c
    src/web_window.c)
if(MAUL_WINDOW_GAMEPAD)
    list(APPEND MWIN_WEB_SOURCES src/web_pad.c)
endif()
target_sources(maul-window PRIVATE ${MWIN_WEB_SOURCES})
target_compile_definitions(maul-window PRIVATE MAUL_WINDOW_WEB)

# mwin_web_glue(<target> <output> [<file.c>...])
#
# Writes the page's imports from the EM_JS functions of the library and
# of the files given, which a test adds its own to.
function(mwin_web_glue target output)
    find_package(Python3 REQUIRED COMPONENTS Interpreter)
    set(sources "")
    foreach(source IN LISTS MWIN_WEB_SOURCES ARGN)
        list(APPEND sources ${PROJECT_SOURCE_DIR}/${source})
    endforeach()
    add_custom_command(OUTPUT ${output}
        COMMAND ${Python3_EXECUTABLE} ${PROJECT_SOURCE_DIR}/tools/gen_web_glue.py ${output} ${sources}
        DEPENDS ${sources} ${PROJECT_SOURCE_DIR}/tools/gen_web_glue.py
        VERBATIM)
    add_custom_target(${target} ALL DEPENDS ${output})
endfunction()

if(NOT EMSCRIPTEN)
    set(MWIN_WEB_GLUE ${PROJECT_BINARY_DIR}/maul-window.mjs)
    mwin_web_glue(maul-window-glue ${MWIN_WEB_GLUE})
    add_dependencies(maul-window maul-window-glue)
    set_target_properties(maul-window PROPERTIES MAUL_WINDOW_WEB_GLUE ${MWIN_WEB_GLUE})
    if(MAUL_WINDOW_INSTALL)
        install(FILES ${MWIN_WEB_GLUE} DESTINATION ${CMAKE_INSTALL_DATADIR}/maul-window)
    endif()
endif()
