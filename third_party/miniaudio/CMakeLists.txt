# miniaudio, vendored at the exact revision recorded in third_party/README.md.
# This file is ours; everything else in this directory is miniaudio's own,
# unchanged. ADR-0038 admits it as the device provider only, so everything
# but devices is compiled out: no decoding, encoding, generation, resource
# manager, node graph, or engine. Backends are loaded when a device opens, so
# nothing links against a system sound library.
find_package(Threads REQUIRED)
add_library(rawframe_miniaudio_vendor STATIC miniaudio.c)
target_include_directories(rawframe_miniaudio_vendor SYSTEM PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_compile_definitions(rawframe_miniaudio_vendor PUBLIC
    MA_NO_DECODING
    MA_NO_ENCODING
    MA_NO_GENERATION
    MA_NO_RESOURCE_MANAGER
    MA_NO_NODE_GRAPH
    MA_NO_ENGINE)
# Upstream's code, upstream's warnings: none of ours applies.
target_compile_options(rawframe_miniaudio_vendor PRIVATE -w)
set_target_properties(rawframe_miniaudio_vendor PROPERTIES C_STANDARD 99 POSITION_INDEPENDENT_CODE ON)
target_link_libraries(rawframe_miniaudio_vendor PRIVATE ${CMAKE_DL_LIBS} Threads::Threads m)
