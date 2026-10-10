# The one device (ADR-0045's rawframe.render, SPEC-0024) over Maul RHI,
# which only the rendering cluster declares. Client only: the dedicated
# server links none of it, and a platform Maul RHI is not built for has no
# render module at all.
if(NOT TARGET maul-rhi)
    return()
endif()

rawframe_module(NAME render SOURCES src/capture.cpp src/device.cpp src/display.cpp src/frame.cpp src/frames.cpp src/output.cpp src/participant.cpp src/textures.cpp DEPS texture window composition diagnostics result base)
rawframe_shader_containers(TARGET rawframe_render NAMES display)
target_link_libraries(rawframe_render PUBLIC maul-rhi)
# Vulkan's headers as Maul RHI vendors them, for the objects an OpenXR
# runtime makes (D591), and the loader opened at run time for them.
target_include_directories(rawframe_render SYSTEM PRIVATE ${CMAKE_SOURCE_DIR}/third_party/maul-rhi/khronos)
target_link_libraries(rawframe_render PRIVATE ${CMAKE_DL_LIBS})

rawframe_module_tests(NAME render SOURCES tests/device_test.cpp tests/frame_test.cpp tests/output_test.cpp)
