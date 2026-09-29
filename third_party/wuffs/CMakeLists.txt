# Wuffs, vendored at the exact revision recorded in third_party/README.md.
# This file and wuffs.cpp are ours; everything else in this directory is
# Wuffs's own, unchanged. ADR-0058: source images are decoded in import
# tooling only, so only the cook links this, and no runtime closure does.
# Wuffs is memory-safe by construction, which is why it may face an
# author's or a player's files alike.
add_library(rawframe_wuffs STATIC ${CMAKE_CURRENT_SOURCE_DIR}/wuffs.cpp)
target_include_directories(rawframe_wuffs SYSTEM PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
# The admitted sources (PNG, JPEG, BMP, TGA), what PNG needs beneath it,
# and the auxiliary API that picks a decoder by the file's first bytes.
target_compile_definitions(rawframe_wuffs PUBLIC
    WUFFS_CONFIG__MODULES
    WUFFS_CONFIG__MODULE__BASE
    WUFFS_CONFIG__MODULE__ADLER32
    WUFFS_CONFIG__MODULE__CRC32
    WUFFS_CONFIG__MODULE__DEFLATE
    WUFFS_CONFIG__MODULE__ZLIB
    WUFFS_CONFIG__MODULE__PNG
    WUFFS_CONFIG__MODULE__JPEG
    WUFFS_CONFIG__MODULE__BMP
    WUFFS_CONFIG__MODULE__TARGA
    WUFFS_CONFIG__MODULE__AUX__BASE
    WUFFS_CONFIG__MODULE__AUX__IMAGE)
# Upstream's code, upstream's warnings: none of ours applies.
target_compile_options(rawframe_wuffs PRIVATE -w)
set_target_properties(rawframe_wuffs PROPERTIES POSITION_INDEPENDENT_CODE ON)
