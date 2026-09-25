# cgltf, vendored at the exact revision recorded in third_party/README.md.
# This file and cgltf.c are ours; everything else in this directory is
# cgltf's own, unchanged. ADR-0058: glTF is decoded in import tooling only,
# so only the cook links this, and no runtime closure does.
add_library(rawframe_cgltf STATIC ${CMAKE_CURRENT_SOURCE_DIR}/cgltf.c)
target_include_directories(rawframe_cgltf SYSTEM PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
# Upstream's code, upstream's warnings: none of ours applies.
target_compile_options(rawframe_cgltf PRIVATE -w)
set_target_properties(rawframe_cgltf PROPERTIES C_STANDARD 99 POSITION_INDEPENDENT_CODE ON)
