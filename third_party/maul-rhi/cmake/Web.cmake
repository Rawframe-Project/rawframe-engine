# The WebGPU driver (mrhi-0003). Its browser side is JavaScript in the
# library's objects (EM_JS): with Emscripten the program's link takes it
# in and nothing else is needed. Without Emscripten (wasm32-wasi) the same
# functions are imports, and the build writes maul-rhi.mjs, which gives
# the page them (mrhi-0016); the library's MAUL_RHI_WEB_GLUE property
# names it.

set(MRHI_WEB_SOURCES
    src/driver_webgpu.c
    src/webgpu_device.c
    src/webgpu_frame.c
    src/webgpu_names.c
    src/webgpu_pipeline.c)
target_sources(maul-rhi PRIVATE ${MRHI_WEB_SOURCES})
target_compile_definitions(maul-rhi PRIVATE MAUL_RHI_WEBGPU_DRIVER)

# mrhi_web_glue(<target> <output> [<file.c>...])
#
# Writes the page's imports from the EM_JS functions of the library and
# of the files given, which a test adds its own to.
function(mrhi_web_glue target output)
    find_package(Python3 REQUIRED COMPONENTS Interpreter)
    set(sources "")
    foreach(source IN LISTS MRHI_WEB_SOURCES ARGN)
        list(APPEND sources ${PROJECT_SOURCE_DIR}/${source})
    endforeach()
    add_custom_command(OUTPUT ${output}
        COMMAND ${Python3_EXECUTABLE} ${PROJECT_SOURCE_DIR}/tools/gen_web_glue.py ${output} ${sources}
        DEPENDS ${sources} ${PROJECT_SOURCE_DIR}/tools/gen_web_glue.py
        VERBATIM)
    add_custom_target(${target} ALL DEPENDS ${output})
endfunction()

if(NOT EMSCRIPTEN)
    set(MRHI_WEB_GLUE ${PROJECT_BINARY_DIR}/maul-rhi.mjs)
    mrhi_web_glue(maul-rhi-glue ${MRHI_WEB_GLUE})
    add_dependencies(maul-rhi maul-rhi-glue)
    set_target_properties(maul-rhi PROPERTIES MAUL_RHI_WEB_GLUE ${MRHI_WEB_GLUE})
    if(MAUL_RHI_INSTALL)
        install(FILES ${MRHI_WEB_GLUE} DESTINATION ${CMAKE_INSTALL_DATADIR}/maul-rhi)
    endif()
endif()
