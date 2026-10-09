# Maul UI, vendored at the exact revision recorded in third_party/README.md
# and built by its own CMake, unchanged: the retained UI core (ADR-0034,
# D374), its node tree, flex layout, style, and draw-command list, and its
# text component (D383): fonts, shaping, line breaking, and bidi order over
# FreeType and HarfBuzz at the releases Maul UI pins and Maul Unicode, each
# vendored, which its FetchContent finds here and never fetches. Portable C
# (HarfBuzz is C++) with no platform backend, so it builds everywhere; only
# a client's UI links it, never the dedicated server's closure.
#
# Included before Maul Window, which brings Maul Unicode too: Maul UI's
# needs Maul Unicode's HarfBuzz functions, so it is the one that configures
# it.
set(FETCHCONTENT_FULLY_DISCONNECTED ON)
set(FETCHCONTENT_SOURCE_DIR_MAUL_UI_FREETYPE "${CMAKE_CURRENT_LIST_DIR}/freetype")
set(FETCHCONTENT_SOURCE_DIR_MAUL_UI_HARFBUZZ "${CMAKE_CURRENT_LIST_DIR}/harfbuzz")
set(FETCHCONTENT_SOURCE_DIR_MAUL-UNICODE "${CMAKE_CURRENT_LIST_DIR}/maul-unicode")
set(MAUL_UNICODE_INSTALL OFF CACHE BOOL "" FORCE)
set(MAUL_UI_TEXT ON CACHE BOOL "" FORCE)
set(MAUL_UI_TEXT_SYSTEM_LIBRARIES OFF CACHE BOOL "" FORCE)
# Its accessibility tree (record mui-0008), which the engine's UI hands its
# updates to (D571), and the adapters the engine drives so far: AT-SPI's on
# Linux, which opens libdbus at run time, and Android's (D576), whose Java
# provider (java/maul/ui) the application carries. The other platforms'
# adapters follow, each when its platform's run shows it.
set(MAUL_UI_ACCESS_TREE ON CACHE BOOL "" FORCE)
if(CMAKE_SYSTEM_NAME STREQUAL "Linux")
    set(MAUL_UI_ATSPI ON CACHE BOOL "" FORCE)
else()
    set(MAUL_UI_ATSPI OFF CACHE BOOL "" FORCE)
endif()
set(MAUL_UI_UIA OFF CACHE BOOL "" FORCE)
set(MAUL_UI_NSACCESSIBILITY OFF CACHE BOOL "" FORCE)
set(MAUL_UI_UIACCESSIBILITY OFF CACHE BOOL "" FORCE)
if(ANDROID)
    set(MAUL_UI_ANDROID_ACCESSIBILITY ON CACHE BOOL "" FORCE)
else()
    set(MAUL_UI_ANDROID_ACCESSIBILITY OFF CACHE BOOL "" FORCE)
endif()
set(MAUL_UI_ARIA OFF CACHE BOOL "" FORCE)
set(MAUL_UI_BUILD_TESTS OFF CACHE BOOL "" FORCE)
set(MAUL_UI_BUILD_BENCH OFF CACHE BOOL "" FORCE)
set(MAUL_UI_INSTALL OFF CACHE BOOL "" FORCE)
add_subdirectory("${CMAKE_CURRENT_LIST_DIR}/maul-ui" "${CMAKE_BINARY_DIR}/third_party/maul-ui" EXCLUDE_FROM_ALL)

# On the web, FreeType's setjmp and longjmp (a damaged font's validation
# jumps out) run through WebAssembly's exception handling: its sources are
# lowered so, and the runtime they call is linked with Maul UI (D383).
# FreeType and HarfBuzz are built for size there, which the download
# budget counts and their speed does not need (D384).
if(CMAKE_SYSTEM_NAME STREQUAL "WASI")
    target_compile_options(maul_ui_freetype PRIVATE -mllvm -wasm-enable-sjlj -mexception-handling -Oz)
    target_compile_options(maul_ui_harfbuzz PRIVATE -Oz)
    add_library(rawframe_wasi_setjmp STATIC "${PROJECT_SOURCE_DIR}/cmake/wasi/setjmp.c")
    target_compile_options(rawframe_wasi_setjmp PRIVATE -mllvm -wasm-enable-sjlj -mexception-handling)
    target_link_libraries(maul-ui PRIVATE rawframe_wasi_setjmp)
endif()

# On Android the client is a shared library (D551), which FreeType's and
# HarfBuzz's objects go into: Maul UI builds them position-dependent unless
# it is shared itself.
if(ANDROID)
    set_target_properties(maul_ui_freetype maul_ui_harfbuzz PROPERTIES POSITION_INDEPENDENT_CODE ON)
endif()
