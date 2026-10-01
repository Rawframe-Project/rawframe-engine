# The OpenType Sanitizer, vendored by tools/update_ots.sh at the release
# recorded in third_party/README.md (D385). This file and rawframe/ are ours;
# everything else in this directory is upstream's own, unchanged. ADR-0078:
# fonts are sanitized in cook tooling only, so no runtime closure links this.
# rawframe/ holds its configuration and refusing stand-ins for zlib and the
# WOFF 2.0 decoder, which only WOFF fonts reach and the cook refuses.
file(GLOB rawframe_ots_sources CONFIGURE_DEPENDS ${CMAKE_CURRENT_SOURCE_DIR}/src/*.cc)
add_library(rawframe_ots STATIC ${rawframe_ots_sources})
target_include_directories(rawframe_ots SYSTEM PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/include)
target_include_directories(rawframe_ots PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/rawframe)
target_compile_definitions(rawframe_ots PRIVATE HAVE_CONFIG_H)
# Upstream's code, upstream's warnings: none of ours applies.
target_compile_options(rawframe_ots PRIVATE -w)
set_target_properties(rawframe_ots PROPERTIES POSITION_INDEPENDENT_CODE ON)
