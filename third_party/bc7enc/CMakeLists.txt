# bc7enc_rdo's BC7 encoder and decoder, vendored at the exact revision
# recorded in third_party/README.md. This file is ours; everything else in
# this directory is upstream's own, unchanged. ADR-0058: textures are
# encoded in cook tooling only, and the decoder serves the tests that check
# what was encoded, so no runtime closure links this.
add_library(rawframe_bc7enc STATIC ${CMAKE_CURRENT_SOURCE_DIR}/bc7enc.cpp ${CMAKE_CURRENT_SOURCE_DIR}/bc7decomp.cpp)
target_include_directories(rawframe_bc7enc SYSTEM PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
# Upstream's code, upstream's warnings: none of ours applies.
target_compile_options(rawframe_bc7enc PRIVATE -w)
set_target_properties(rawframe_bc7enc PROPERTIES POSITION_INDEPENDENT_CODE ON)
