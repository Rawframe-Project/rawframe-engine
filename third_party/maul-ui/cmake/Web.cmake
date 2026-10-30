# The ARIA adapter's page side without Emscripten. Its
# JavaScript is in its C files (EM_JS): with Emscripten the program's
# link takes it in and nothing else is needed. Without Emscripten
# (wasm32-wasi) the same functions are imports, and the build writes
# maul-ui.mjs, which gives the page them; the library's MAUL_UI_WEB_GLUE
# property names it.

set(MUI_WEB_SOURCES src/aria_page.c)

# mui_web_glue(<target> <output> [<file.c>...])
#
# Writes the page's imports from the EM_JS functions of the library and
# of the files given, which a test adds its own to.
function(mui_web_glue target output)
    find_package(Python3 REQUIRED COMPONENTS Interpreter)
    set(sources "")
    foreach(source IN LISTS MUI_WEB_SOURCES ARGN)
        list(APPEND sources ${PROJECT_SOURCE_DIR}/${source})
    endforeach()
    add_custom_command(OUTPUT ${output}
        COMMAND ${Python3_EXECUTABLE} ${PROJECT_SOURCE_DIR}/tools/gen_web_glue.py ${output} ${sources}
        DEPENDS ${sources} ${PROJECT_SOURCE_DIR}/tools/gen_web_glue.py
        VERBATIM)
    add_custom_target(${target} ALL DEPENDS ${output})
endfunction()

set(MUI_WEB_GLUE ${PROJECT_BINARY_DIR}/maul-ui.mjs)
mui_web_glue(maul-ui-glue ${MUI_WEB_GLUE})
add_dependencies(maul-ui maul-ui-glue)
set_target_properties(maul-ui PROPERTIES MAUL_UI_WEB_GLUE ${MUI_WEB_GLUE})
if(MAUL_UI_INSTALL)
    install(FILES ${MUI_WEB_GLUE} DESTINATION ${CMAKE_INSTALL_DATADIR}/maul-ui)
endif()
