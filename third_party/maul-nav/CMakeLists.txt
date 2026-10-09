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
    src/filter.c
    src/fingerprint.c
    src/flight.c
    src/flight_baker.c
    src/flight_def.c
    src/flight_frame.c
    src/flight_probe.c
    src/flight_search.c
    src/flight_space.c
    src/flight_tile.c
    src/flight_volume.c
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
    src/spatial.c
    src/surface.c
    src/terrain.c
    src/tile_index.c
    src/tile.c
    src/volume.c
    src/triangulate.c)
maul_configure_library(maul-nav)
# floor, ceil, fabs and sqrt, the exactly rounded functions the library
# takes from libm (mnav-0001).
if(NOT WIN32 AND NOT APPLE)
    target_link_libraries(maul-nav PUBLIC m)
endif()

if(MAUL_NAV_BUILD_TESTS)
    enable_testing()
    maul_add_test(base)
    maul_add_test(bake)
    maul_add_test(baker)
    maul_add_test(border_vertices WHITEBOX)
    maul_add_test(compact WHITEBOX)
    maul_add_test(contour WHITEBOX)
    maul_add_test(delaunay WHITEBOX)
    maul_add_test(detail WHITEBOX)
    maul_add_test(filter WHITEBOX)
    maul_add_test(height_patch WHITEBOX)
    maul_add_test(heightfield WHITEBOX)
    maul_add_test(holes WHITEBOX)
    maul_add_test(navmesh WHITEBOX)
    maul_add_test(nearest)
    maul_add_test(nearest_rules WHITEBOX)
    maul_add_test(path WHITEBOX)
    maul_add_test(path_rules WHITEBOX)
    maul_add_test(raycast WHITEBOX)
    maul_add_test(query_filter WHITEBOX)
    maul_add_test(offmesh WHITEBOX)
    maul_add_test(path_links WHITEBOX)
    maul_add_test(shortest WHITEBOX)
    maul_add_test(flight WHITEBOX)
    maul_add_test(flight_volume WHITEBOX)
    maul_add_test(flight_query)
    maul_add_test(sliced WHITEBOX)
    maul_add_test(surface WHITEBOX)
    maul_add_test(corridor WHITEBOX)
    maul_add_test(tiers WHITEBOX)
    maul_add_test(resnap WHITEBOX)
    maul_add_test(bake2d)
    maul_add_test(grid)
    maul_add_test(flow)
    maul_add_test(hierarchy)
    maul_add_test(spatial)
    maul_add_test(linkgen)
    maul_add_test(debug)
    maul_add_test(guide)
    maul_add_test(navflow)
    maul_add_test(alloc_failures)
    maul_add_test(rings)
    maul_add_test(refusals)
    # Bakes and queries on several threads; Emscripten builds have none.
    if(NOT EMSCRIPTEN)
        maul_add_test(workers POSIX)
        maul_add_test(shared_reads POSIX)
    endif()
    maul_add_test(edge_links)
    maul_add_test(scenarios)
    maul_add_test(terrain)
    maul_add_test(tile_index)
    maul_add_test(volume WHITEBOX)
    maul_add_test(avoidance)
    maul_add_test(avoidance3)
    maul_add_test(crowd WHITEBOX)
    maul_add_test(orca WHITEBOX)
    maul_add_test(orca3 WHITEBOX)
    maul_add_test(obstacle WHITEBOX)
    maul_add_test(polymesh WHITEBOX)
    maul_add_test(region WHITEBOX)
    maul_add_test(scalar WHITEBOX)
    maul_add_test(tile WHITEBOX)
    maul_add_header_tests()
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
    foreach(area tile bake query avoid graph stream flow shortest flight)
        add_executable(fuzz_${area} test/fuzz_${area}.c)
        target_link_libraries(fuzz_${area} PRIVATE maul-nav)
        # The targets reach the library's own functions.
        target_include_directories(fuzz_${area} PRIVATE ${PROJECT_SOURCE_DIR}/src)
        maul_apply_flags(fuzz_${area})
        target_compile_options(fuzz_${area} PRIVATE -fsanitize=fuzzer,address,undefined
                                                     -fno-sanitize-recover=all)
        target_link_options(fuzz_${area} PRIVATE -fsanitize=fuzzer,address,undefined)
    endforeach()
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
