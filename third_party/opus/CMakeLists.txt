# libopus, vendored at the exact revision recorded in third_party/README.md.
# This file is ours; everything else in this directory is libopus's own,
# unchanged. ADR-0058 makes Opus the canonical cooked lossy codec: import
# tooling encodes with it and the client decodes. Built in floating point,
# portable C only (no run-time CPU dispatch, no intrinsics), without the
# deep-learning extensions (DRED, deep PLC, OSCE).

# The variable `name` of one of libopus's .mk source lists: its first line
# and each line a trailing backslash continues.
function(rawframe_opus_sources out list name)
    file(STRINGS ${CMAKE_CURRENT_SOURCE_DIR}/${list} lines)
    set(sources "")
    set(inside FALSE)
    foreach(line IN LISTS lines)
        if(line MATCHES "^${name} =")
            set(inside TRUE)
        elseif(NOT inside)
            continue()
        endif()
        string(REGEX MATCHALL "[A-Za-z0-9_/.]+\\.c" found "${line}")
        list(APPEND sources ${found})
        if(NOT line MATCHES "\\\\$")
            break()
        endif()
    endforeach()
    set(${out} ${sources} PARENT_SCOPE)
endfunction()

rawframe_opus_sources(opus opus_sources.mk OPUS_SOURCES)
rawframe_opus_sources(opus_float opus_sources.mk OPUS_SOURCES_FLOAT)
rawframe_opus_sources(celt celt_sources.mk CELT_SOURCES)
rawframe_opus_sources(silk silk_sources.mk SILK_SOURCES)
rawframe_opus_sources(silk_float silk_sources.mk SILK_SOURCES_FLOAT)

add_library(rawframe_opus_vendor STATIC ${opus} ${opus_float} ${celt} ${silk} ${silk_float})
target_include_directories(rawframe_opus_vendor SYSTEM PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/include)
target_include_directories(rawframe_opus_vendor PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${CMAKE_CURRENT_SOURCE_DIR}/celt
    ${CMAKE_CURRENT_SOURCE_DIR}/silk
    ${CMAKE_CURRENT_SOURCE_DIR}/silk/float)
target_compile_definitions(rawframe_opus_vendor PRIVATE OPUS_BUILD ENABLE_HARDENING VAR_ARRAYS HAVE_LRINT HAVE_LRINTF)
# Upstream's code, upstream's warnings: none of ours applies.
target_compile_options(rawframe_opus_vendor PRIVATE -w)
set_target_properties(rawframe_opus_vendor PROPERTIES C_STANDARD 99 POSITION_INDEPENDENT_CODE ON)
target_link_libraries(rawframe_opus_vendor PRIVATE m)
