# Navigation (ADR-0056 as amended by D126, D399) over Maul Nav, private to
# this module: navmeshes baked headlessly from triangle geometry, and paths
# over them. Where the World is authoritative; no client draws with it.
rawframe_module(NAME navigation SOURCES src/navmesh.cpp DEPS result base)
target_link_libraries(rawframe_navigation PRIVATE maul-nav)

rawframe_module_tests(NAME navigation SOURCES tests/navmesh_test.cpp)
