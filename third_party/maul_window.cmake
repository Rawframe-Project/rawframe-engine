# Maul Unicode and Maul Window, vendored at the exact revisions recorded in
# third_party/README.md and built by their own CMake, unchanged. Only a
# client that has windows builds them: never the dedicated server's closure.
# On macOS its AppKit backend (mwin-0024) gives the client windows and input
# since D405, without drawing: Maul RHI's Metal driver wants shader
# containers with Metal's language, which the engine's do not carry yet
# (third_party/maul_rhi.cmake). On the web (the
# engine's wasm32-wasi client) the build also writes maul-window.mjs, the
# page's side of the backend (Maul Window's mwin-0022). Maul Window finds
# Maul Unicode through FetchContent; the source directory below points it
# at the vendored copy, and nothing is fetched.
set(RAWFRAME_MAUL_WINDOW OFF)
if(CMAKE_SYSTEM_NAME MATCHES "^(Linux|Darwin|WASI)$" OR WIN32)
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
