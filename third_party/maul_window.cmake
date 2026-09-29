# Maul Unicode and Maul Window, vendored at the exact revisions recorded in
# third_party/README.md and built by their own CMake, unchanged. Only a
# client that has windows builds them: never the dedicated server's closure,
# and not on Apple platforms, which have no backend yet. On the web (the
# engine's wasm32-wasi client) the build also writes maul-window.mjs, the
# page's side of the backend (Maul Window's mwin-0022). Maul Window finds
# Maul Unicode through FetchContent; the source directory below points it
# at the vendored copy, and nothing is fetched.
set(RAWFRAME_MAUL_WINDOW OFF)
if(CMAKE_SYSTEM_NAME STREQUAL "Linux" OR WIN32 OR CMAKE_SYSTEM_NAME STREQUAL "WASI")
    set(RAWFRAME_MAUL_WINDOW ON)
endif()
if(NOT RAWFRAME_MAUL_WINDOW)
    return()
endif()

set(FETCHCONTENT_FULLY_DISCONNECTED ON)
set(FETCHCONTENT_SOURCE_DIR_MAUL-UNICODE "${CMAKE_CURRENT_LIST_DIR}/maul-unicode")
set(MAUL_UNICODE_INSTALL OFF CACHE BOOL "" FORCE)
set(MAUL_WINDOW_INSTALL OFF CACHE BOOL "" FORCE)
# The headless test backend, which a test selects explicitly, is built only
# where the engine's tests are.
set(MAUL_WINDOW_TEST_BACKEND ${RAWFRAME_BUILD_TESTS} CACHE BOOL "" FORCE)
add_subdirectory("${CMAKE_CURRENT_LIST_DIR}/maul-window" "${CMAKE_BINARY_DIR}/third_party/maul-window" EXCLUDE_FROM_ALL)
