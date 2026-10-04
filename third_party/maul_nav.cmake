# Maul Nav, vendored at the exact revision recorded in third_party/README.md
# and built by its own CMake, unchanged (ADR-0056 as amended by D126, D399):
# navmesh generation, path and spatial queries, flow fields, and avoidance,
# in portable C with no dependencies. Only the navigation module links it;
# its queries run where the World is authoritative.
set(MAUL_NAV_BUILD_TESTS OFF CACHE BOOL "" FORCE)
set(MAUL_NAV_BUILD_BENCH OFF CACHE BOOL "" FORCE)
set(MAUL_NAV_INSTALL OFF CACHE BOOL "" FORCE)
add_subdirectory("${CMAKE_CURRENT_LIST_DIR}/maul-nav" "${CMAKE_BINARY_DIR}/third_party/maul-nav" EXCLUDE_FROM_ALL)
