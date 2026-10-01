# Maul UI, vendored at the exact revision recorded in third_party/README.md
# and built by its own CMake, unchanged: the retained UI core (ADR-0034,
# D374), its node tree, flex layout, style, and draw-command list. Its text
# component (FreeType, HarfBuzz, and Maul Unicode, fetched at configure
# time) is off until the engine draws glyphs. Portable C with no platform
# backend, so it builds everywhere; only a client's UI links it, never the
# dedicated server's closure.
set(MAUL_UI_TEXT OFF CACHE BOOL "" FORCE)
set(MAUL_UI_BUILD_TESTS OFF CACHE BOOL "" FORCE)
set(MAUL_UI_BUILD_BENCH OFF CACHE BOOL "" FORCE)
set(MAUL_UI_INSTALL OFF CACHE BOOL "" FORCE)
add_subdirectory("${CMAKE_CURRENT_LIST_DIR}/maul-ui" "${CMAKE_BINARY_DIR}/third_party/maul-ui" EXCLUDE_FROM_ALL)
