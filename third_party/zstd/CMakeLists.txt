# Zstandard, vendored at the exact revision recorded in third_party/README.md.
# This file is ours; everything else in this directory is Zstandard's own,
# unchanged. SPEC-0021 stores a Build's chunks as single Zstandard frames:
# packaging tooling compresses and every reader decompresses, so the two
# halves are separate libraries over one common part, and a runtime links
# the decoder alone. Portable C only: no assembly, no threads, no legacy
# formats, no dictionary builder.
file(GLOB rawframe_zstd_common_sources CONFIGURE_DEPENDS ${CMAKE_CURRENT_SOURCE_DIR}/lib/common/*.c)
file(GLOB rawframe_zstd_decompress_sources CONFIGURE_DEPENDS ${CMAKE_CURRENT_SOURCE_DIR}/lib/decompress/*.c)
file(GLOB rawframe_zstd_compress_sources CONFIGURE_DEPENDS ${CMAKE_CURRENT_SOURCE_DIR}/lib/compress/*.c)

foreach(part common decompress compress)
    add_library(rawframe_zstd_${part} STATIC ${rawframe_zstd_${part}_sources})
    target_include_directories(rawframe_zstd_${part} SYSTEM PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/lib)
    target_compile_definitions(rawframe_zstd_${part} PRIVATE ZSTD_DISABLE_ASM ZSTD_LEGACY_SUPPORT=0)
    # Upstream's code, upstream's warnings: none of ours applies.
    target_compile_options(rawframe_zstd_${part} PRIVATE -w)
    set_target_properties(rawframe_zstd_${part} PROPERTIES C_STANDARD 99 POSITION_INDEPENDENT_CODE ON)
endforeach()
target_link_libraries(rawframe_zstd_decompress PUBLIC rawframe_zstd_common)
target_link_libraries(rawframe_zstd_compress PUBLIC rawframe_zstd_common)
