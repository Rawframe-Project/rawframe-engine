# The Kest library every game compiles against (D86): Kest's `std` and the
# engine's own Kest modules, embedded in the engine when it is built.
set(embedded "${CMAKE_CURRENT_BINARY_DIR}/embedded.cpp")
file(GLOB_RECURSE kest_library_sources CONFIGURE_DEPENDS
    "${RAWFRAME_KEST_LIBRARY}std/*.kest" "${CMAKE_CURRENT_SOURCE_DIR}/kest/*.kest")
add_custom_command(
    OUTPUT "${embedded}"
    COMMAND "${CMAKE_COMMAND}" -DOUTPUT=${embedded}
        "-DROOTS=${RAWFRAME_KEST_LIBRARY}=engine/lib/|${CMAKE_CURRENT_SOURCE_DIR}/kest=engine/modules/"
        -P "${CMAKE_CURRENT_SOURCE_DIR}/embed.cmake"
    DEPENDS ${kest_library_sources} "${CMAKE_CURRENT_SOURCE_DIR}/embed.cmake"
    COMMENT "Embedding the Kest library"
    VERBATIM)

rawframe_module(NAME kest_library
    SOURCES src/library.cpp src/sources.cpp "${embedded}"
    DEPS kest document result base
)
target_include_directories(rawframe_kest_library PRIVATE src)

rawframe_module_tests(NAME kest_library SOURCES tests/library_test.cpp tests/sources_test.cpp)
rawframe_module_fuzz(NAME kest_library TARGET sources RUNS 20000 MAX_LEN 16384 SEEDS tests/seeds SOURCES tests/fuzz_sources.cpp)
if(TARGET rawframe_kest_library_tests)
    target_compile_definitions(rawframe_kest_library_tests PRIVATE
        RAWFRAME_KEST_LIBRARY="${RAWFRAME_KEST_LIBRARY}"
        RAWFRAME_ENGINE_MODULES="${CMAKE_CURRENT_SOURCE_DIR}/kest/"
        RAWFRAME_SAMPLE_GAMES="${PROJECT_SOURCE_DIR}/games/")
endif()
