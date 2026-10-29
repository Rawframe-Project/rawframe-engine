cmake_minimum_required(VERSION 3.25)

# The library's identity. The family's CMake modules and tools read these.
set(MAUL_LIBRARY maul-nav)
set(MAUL_API_PREFIX mnav)
set(MAUL_MACRO_PREFIX MNAV)
set(MAUL_TITLE "Maul Nav")
set(MAUL_PREFIX MAUL_NAV)

include(cmake/ReadVersion.cmake)
maul_read_version(${CMAKE_CURRENT_SOURCE_DIR}/include/maul-nav/base.h MNAV MAUL_VERSION)
project(maul-nav
    VERSION ${MAUL_VERSION}
    DESCRIPTION "Navigation: navmesh generation, path and spatial queries, flow fields and avoidance"
    LANGUAGES C)

option(MAUL_NAV_BUILD_TESTS "Build the test suites" ${PROJECT_IS_TOP_LEVEL})
option(MAUL_NAV_BUILD_BENCH "Build the benchmarks" ${PROJECT_IS_TOP_LEVEL})
option(MAUL_NAV_BUILD_SAMPLES "Build the samples" ${PROJECT_IS_TOP_LEVEL})
option(MAUL_NAV_BUILD_SHARED "Build a shared library instead of a static one" OFF)
option(MAUL_NAV_INSTALL "Generate install rules" ${PROJECT_IS_TOP_LEVEL})
option(MAUL_NAV_WERROR "Treat compiler warnings as errors" OFF)
option(MAUL_NAV_SANITIZE "Instrument with AddressSanitizer and UBSan" OFF)
option(MAUL_NAV_TSAN "Instrument with ThreadSanitizer" OFF)
option(MAUL_NAV_COVERAGE "Instrument for gcov coverage" OFF)
option(MAUL_NAV_FUZZ "Build the libFuzzer targets (Clang)" OFF)
option(MAUL_NAV_DIFFERENTIAL "Build the differential harness against Recast (test only)" OFF)
option(MAUL_NAV_TEST_PACKAGE "Test the installed package by building samples/minimal against it" OFF)

# Every executable (tests, benchmarks, tools) lands in the top of the
# build tree, so scripts and CI find them in one place.
if(PROJECT_IS_TOP_LEVEL)
    set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${PROJECT_BINARY_DIR})
endif()

include(GNUInstallDirs)
include(cmake/CompilerFlags.cmake)
include(cmake/Library.cmake)
include(cmake/Testing.cmake)

if(MAUL_NAV_BUILD_SHARED)
    set(MAUL_LIBRARY_KIND SHARED)
else()
    set(MAUL_LIBRARY_KIND STATIC)
endif()
add_library(maul-nav ${MAUL_LIBRARY_KIND}
    src/allocator.c
    src/avoidance.c
    src/avoidance3.c
    src/bake_def.c
    src/bake_input.c
    src/base.c
    src/baker.c
    src/border_vertices.c
    src/clip.c
    src/commit.c
    src/compact.c
    src/corridor.c
    src/contour.c
    src/crowd.c
    src/debug.c
    src/delaunay.c
    src/draw.c
    src/detail.c
    src/erode.c
    src/fields.c
    src/filter.c
    src/fingerprint.c
    src/flight.c
    src/flight_baker.c
    src/flight_debug.c
    src/flight_def.c
    src/flight_frame.c
    src/flight_probe.c
    src/flight_search.c
    src/flight_space.c
    src/flight_tile.c
    src/flight_volume.c
    src/fpenv.c
    src/hash.c
    src/funnel.c
    src/flow.c
    src/flow_repair.c
    src/grid.c
    src/modifiers.c
    src/height_patch.c
    src/heightfield.c
    src/hierarchy.c
    src/hierarchy_path.c
    src/linkgen.c
    src/holes.c
    src/input.c
    src/navflow.c
    src/navmesh.c
    src/nearest.c
    src/obstacle.c
    src/offmesh.c
    src/orca.c
    src/orca3.c
    src/outline.c
    src/path.c
    src/query.c
    src/query_filter.c
    src/raycast.c
    src/planar.c
    src/polymesh.c
    src/raster.c
    src/region.c
    src/scalar.c
    src/shortest.c
    src/sort.c
    src/steer.c
    src/spatial.c
    src/surface.c
    src/terrain.c
    src/field_pack.c
    src/lz.c
    src/tile_cache.c
    src/tile_index.c
    src/tile.c
    src/volume.c
    src/triangulate.c)
maul_configure_library(maul-nav)
# floor, ceil, fabs and sqrt, the exactly rounded functions the library
# takes from libm.
if(NOT WIN32 AND NOT APPLE)
    target_link_libraries(maul-nav PUBLIC m)
endif()

if(MAUL_NAV_BUILD_TESTS)
    enable_testing()
    # maul_add_dense(<name> [WHITEBOX]): test_<name> with the argument
    # dense, as <name>_dense, the suite's sweeps in full (test/dense.h).
    # Labeled dense: every cell runs it but the AddressSanitizer one, the
    # pull request's slowest, and the dense-asan preset runs it there at
    # night. WHITEBOX: only the dense form reaches src/, so it is built
    # apart as test_<name>_dense with TEST_DENSE_WHITEBOX defined, and left
    # out of shared builds like every white-box suite.
    function(maul_add_dense name)
        cmake_parse_arguments(ARG "WHITEBOX" "" "" ${ARGN})
        set(target test_${name})
        if(ARG_WHITEBOX)
            if(MAUL_NAV_BUILD_SHARED)
                return()
            endif()
            set(target test_${name}_dense)
            add_executable(${target} ${PROJECT_SOURCE_DIR}/test/test_${name}.c)
            target_link_libraries(${target} PRIVATE ${PROJECT_NAME})
            target_include_directories(${target} PRIVATE ${PROJECT_SOURCE_DIR}/src)
            target_compile_definitions(${target} PRIVATE TEST_DENSE_WHITEBOX)
            maul_apply_flags(${target})
        endif()
        if(TARGET ${target})
            add_test(NAME ${name}_dense COMMAND ${target} dense)
            set_tests_properties(${name}_dense PROPERTIES LABELS "${name};dense")
        endif()
    endfunction()
    maul_add_test(base)
    # Writes and checks test/hand_tiles.h (test_hand_tiles.c); the checks
    # job and tools/check.sh run its --check.
    maul_add_test(hand_tiles WHITEBOX MANUAL)
    maul_add_test(bake)
    maul_add_test(baker)
    maul_add_test(border_vertices)
    # Hand-built rings for the removals no bake reaches
    # (test_border_vertices_rings.c).
    maul_add_test(border_vertices_rings WHITEBOX)
    maul_add_test(compact)
    maul_add_test(contour)
    maul_add_test(delaunay)
    maul_add_test(detail)
    maul_add_test(filter)
    maul_add_test(filter_random WHITEBOX)
    maul_add_test(height_patch)
    maul_add_test(height_patch_hand WHITEBOX)
    maul_add_test(heightfield)
    # The cuts of rasterizing running out of room, which no bake reaches.
    maul_add_test(raster_cut WHITEBOX)
    maul_add_test(holes)
    # Hand-built contour sets for dropping a hole, which no bake reaches
    # (test_holes_rings.c).
    maul_add_test(holes_rings WHITEBOX)
    maul_add_test(navmesh)
    # The navmesh's link and slot tables, which no public call shows
    # (test_navmesh_tables.c).
    maul_add_test(navmesh_tables WHITEBOX)
    maul_add_test(nearest)
    maul_add_test(nearest_rules)
    maul_add_test(path)
    maul_add_test(path_rules)
    maul_add_dense(path_rules WHITEBOX)
    maul_add_test(raycast)
    maul_add_test(query_filter)
    maul_add_test(offmesh)
    # A link slot at its last generation, which the API reaches only after
    # 2^32 links (test_link_slots.c).
    maul_add_test(link_slots WHITEBOX)
    maul_add_test(path_links)
    maul_add_test(shortest)
    # The nodes the exact search makes, which no public call reports
    # (test_shortest_work.c).
    maul_add_test(shortest_work WHITEBOX)
    maul_add_test(flight WHITEBOX)
    maul_add_test(flight_volume)
    # The octree the tiles hold, the walk and the payload's structure,
    # which no public call shows (test_flight_octree.c).
    maul_add_test(flight_octree WHITEBOX)
    maul_add_test(flight_query)
    maul_add_dense(flight_query)
    maul_add_test(sliced)
    maul_add_test(surface)
    # The node table a move leaves clean, which no result shows
    # (test_query_table.c).
    maul_add_test(query_table WHITEBOX)
    maul_add_test(walls)
    maul_add_test(corridor)
    maul_add_test(tiers)
    maul_add_test(resnap)
    maul_add_test(bake2d)
    maul_add_test(grid)
    maul_add_test(flow)
    maul_add_test(hierarchy)
    maul_add_dense(hierarchy)
    maul_add_test(spatial)
    # The circle clip running out of room, which no public call reaches at
    # the room the random point gives it.
    maul_add_test(clip WHITEBOX)
    maul_add_test(steer)
    maul_add_test(linkgen)
    maul_add_test(busy)
    maul_add_test(busy_owners)
    maul_add_test(misuse)
    # Generation stopping at its limits, run small here.
    maul_add_test(linkgen_limits WHITEBOX)
    maul_add_test(debug)
    maul_add_test(guide)
    maul_add_test(navflow)
    maul_add_test(alloc_failures)
    maul_add_dense(alloc_failures)
    maul_add_test(rings)
    maul_add_test(refusals)
    maul_add_test(version)
    maul_add_test(stored)
    # Bakes and queries on several threads; Emscripten builds have none.
    if(NOT EMSCRIPTEN)
        maul_add_test(workers POSIX)
        maul_add_test(shared_reads POSIX)
        maul_add_test(busy_threads POSIX)
    endif()
    maul_add_test(edge_links)
    maul_add_test(scenarios)
    maul_add_dense(scenarios)
    # The 1,000-agent doorway crowd, a large scene: nightly, never in the
    # pull request check.
    add_test(NAME doorway COMMAND test_scenarios --doorway)
    set_tests_properties(doorway PROPERTIES LABELS "scenarios;nightly")
    maul_add_test(terrain)
    maul_add_test(funnel WHITEBOX)
    maul_add_test(lz WHITEBOX)
    # The family's suite of the floating-point environment's module
    # (synced from .github with src/fpenv.c): it calls the module's entries
    # under every host setting, which no public call exposes on its own.
    maul_add_test(fpenv WHITEBOX)
    maul_add_test(tile_cache)
    maul_add_dense(tile_cache)
    maul_add_test(tile_index)
    maul_add_test(volume)
    maul_add_test(avoidance)
    maul_add_test(avoidance3)
    maul_add_test(crowd WHITEBOX)
    maul_add_test(spheres WHITEBOX)
    maul_add_test(orca WHITEBOX)
    maul_add_test(orca3 WHITEBOX)
    maul_add_test(obstacle WHITEBOX)
    maul_add_test(polymesh)
    maul_add_test(region)
    maul_add_test(scalar)
    maul_add_test(tile WHITEBOX)
    maul_add_header_tests()
    maul_add_exports_test()
    # Installs the build into the binary tree and builds and runs
    # samples/minimal against it, as a consumer of the package would.
    if(MAUL_NAV_TEST_PACKAGE AND MAUL_NAV_INSTALL)
        add_test(NAME package
                 COMMAND ${CMAKE_COMMAND} -DBINARY_DIR=${PROJECT_BINARY_DIR}
                         -DSOURCE_DIR=${PROJECT_SOURCE_DIR} -DGENERATOR=${CMAKE_GENERATOR}
                         -DC_COMPILER=${CMAKE_C_COMPILER} -DCONFIG=$<CONFIG>
                         -P ${PROJECT_SOURCE_DIR}/cmake/PackageTest.cmake)
        set_tests_properties(package PROPERTIES LABELS package)
    endif()
endif()

# The fuzz targets: test/fuzz_<area>.c, run as fuzz_<area> <corpus dir>,
# over the bytes programs load and hand the library. test/corpus holds a
# minimized seed corpus for each (fuzz_<area>.tar.gz); test_tile --seed
# <file> writes fuzz_tile's first seed.
if(MAUL_NAV_FUZZ)
    # The library is instrumented too: its coverage guides libFuzzer, and
    # the sanitizers watch its code, not only the targets'. Undefined
    # behaviour stops the run, as a failed check does.
    target_compile_options(maul-nav PRIVATE -fsanitize=fuzzer-no-link,address,undefined
                                            -fno-sanitize-recover=all)
    target_link_options(maul-nav PUBLIC -fsanitize=address,undefined)
    set(fuzz_targets "")
    foreach(area tile bake query avoid graph stream flow shortest flight lz)
        list(APPEND fuzz_targets fuzz_${area})
        add_executable(fuzz_${area} test/fuzz_${area}.c)
        target_link_libraries(fuzz_${area} PRIVATE maul-nav)
        # The targets reach the library's own functions.
        target_include_directories(fuzz_${area} PRIVATE ${PROJECT_SOURCE_DIR}/src)
        maul_apply_flags(fuzz_${area})
        target_compile_options(fuzz_${area} PRIVATE -fsanitize=fuzzer,address,undefined
                                                     -fno-sanitize-recover=all)
        target_link_options(fuzz_${area} PRIVATE -fsanitize=fuzzer,address,undefined)
    endforeach()
    # The targets, one a line, for tools/fuzz.sh and the night's matrix.
    list(JOIN fuzz_targets "\n" fuzz_lines)
    file(WRITE ${PROJECT_BINARY_DIR}/fuzz-targets.txt "${fuzz_lines}\n")
endif()

if(MAUL_NAV_DIFFERENTIAL)
    add_subdirectory(test/differential)
endif()

if(MAUL_NAV_BUILD_BENCH AND EXISTS ${PROJECT_SOURCE_DIR}/bench/bench_main.c)
    add_subdirectory(bench)
endif()

if(MAUL_NAV_BUILD_SAMPLES)
    add_subdirectory(samples)
endif()

if(MAUL_NAV_INSTALL)
    if(NOT WIN32 AND NOT APPLE)
        set(MAUL_PKG_LIBS_PRIVATE "-lm")
    endif()
    include(cmake/Install.cmake)
endif()
