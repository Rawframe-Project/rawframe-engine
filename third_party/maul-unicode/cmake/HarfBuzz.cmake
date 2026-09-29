# maul-unicode-harfbuzz: Unicode functions for HarfBuzz, in a static
# library of its own so that only those who shape with HarfBuzz depend
# on it. Installed as a package of its own,
# find_package(maul-unicode-harfbuzz) with the target
# maul-unicode::maul-unicode-harfbuzz, and a pkg-config file.

# HarfBuzz as harfbuzz::harfbuzz: its own CMake package where it has one,
# pkg-config elsewhere (Debian and Ubuntu ship only the latter).
find_package(harfbuzz CONFIG QUIET)
if(NOT TARGET harfbuzz::harfbuzz)
    find_package(PkgConfig REQUIRED)
    pkg_check_modules(MUNI_HARFBUZZ REQUIRED IMPORTED_TARGET GLOBAL harfbuzz)
    add_library(harfbuzz::harfbuzz ALIAS PkgConfig::MUNI_HARFBUZZ)
endif()

add_library(maul-unicode-harfbuzz STATIC src/harfbuzz.c)
add_library(maul-unicode::maul-unicode-harfbuzz ALIAS maul-unicode-harfbuzz)
# Installed, the dependency is named as the package config recreates it,
# whichever way this build found HarfBuzz.
target_link_libraries(maul-unicode-harfbuzz PUBLIC maul-unicode
    $<BUILD_INTERFACE:harfbuzz::harfbuzz> $<INSTALL_INTERFACE:harfbuzz::harfbuzz>)
maul_apply_flags(maul-unicode-harfbuzz)

if(MAUL_UNICODE_INSTALL)
    include(CMakePackageConfigHelpers)
    set(muni_harfbuzz_dir ${CMAKE_INSTALL_LIBDIR}/cmake/maul-unicode-harfbuzz)
    install(TARGETS maul-unicode-harfbuzz EXPORT maul-unicode-harfbuzzTargets
        ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR})
    install(EXPORT maul-unicode-harfbuzzTargets
        NAMESPACE maul-unicode::
        DESTINATION ${muni_harfbuzz_dir})
    configure_package_config_file(${PROJECT_SOURCE_DIR}/cmake/HarfBuzzConfig.cmake.in
        ${PROJECT_BINARY_DIR}/maul-unicode-harfbuzzConfig.cmake
        INSTALL_DESTINATION ${muni_harfbuzz_dir})
    write_basic_package_version_file(${PROJECT_BINARY_DIR}/maul-unicode-harfbuzzConfigVersion.cmake
        VERSION ${PROJECT_VERSION}
        COMPATIBILITY ExactVersion)
    install(FILES
        ${PROJECT_BINARY_DIR}/maul-unicode-harfbuzzConfig.cmake
        ${PROJECT_BINARY_DIR}/maul-unicode-harfbuzzConfigVersion.cmake
        DESTINATION ${muni_harfbuzz_dir})
    configure_file(${PROJECT_SOURCE_DIR}/cmake/harfbuzz.pc.in
        ${PROJECT_BINARY_DIR}/maul-unicode-harfbuzz.pc @ONLY)
    install(FILES ${PROJECT_BINARY_DIR}/maul-unicode-harfbuzz.pc
        DESTINATION ${CMAKE_INSTALL_LIBDIR}/pkgconfig)
endif()
