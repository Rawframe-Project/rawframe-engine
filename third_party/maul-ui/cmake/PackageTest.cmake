# The package test (MAUL_UI_TEST_PACKAGE): installs the build into
# BINARY_DIR/package-test/prefix, then builds and runs samples/minimal
# against it twice, through the CMake package and through pkg-config
# (--static for a static library), with nothing else on its search path.
# Run by CTest as the test "package"; the build must be complete.
#
#   cmake -DBINARY_DIR=... -DSOURCE_DIR=... -DGENERATOR=... -DC_COMPILER=...
#         -DSHARED=ON|OFF -P PackageTest.cmake

set(root ${BINARY_DIR}/package-test)
file(REMOVE_RECURSE ${root})
set(prefix ${root}/prefix)
set(library_path "LD_LIBRARY_PATH=${prefix}/lib:${prefix}/lib64")

function(run)
    execute_process(COMMAND ${ARGN} RESULT_VARIABLE result)
    if(NOT result EQUAL 0)
        list(JOIN ARGN " " command)
        message(FATAL_ERROR "failed (${result}): ${command}")
    endif()
endfunction()

run(${CMAKE_COMMAND} --install ${BINARY_DIR} --prefix ${prefix})

run(${CMAKE_COMMAND} -S ${SOURCE_DIR}/samples/minimal -B ${root}/build -G ${GENERATOR}
    -DCMAKE_C_COMPILER=${C_COMPILER} -DCMAKE_PREFIX_PATH=${prefix})
run(${CMAKE_COMMAND} --build ${root}/build)
run(${CMAKE_COMMAND} -E env ${library_path} ${root}/build/minimal)

find_program(pkg_config pkg-config REQUIRED)
set(static --static)
if(SHARED)
    set(static)
endif()
execute_process(
    COMMAND ${CMAKE_COMMAND} -E env "PKG_CONFIG_PATH=${prefix}/lib/pkgconfig:${prefix}/lib64/pkgconfig"
            ${pkg_config} ${static} --cflags --libs maul-ui
    OUTPUT_VARIABLE flags OUTPUT_STRIP_TRAILING_WHITESPACE RESULT_VARIABLE result)
if(NOT result EQUAL 0)
    message(FATAL_ERROR "pkg-config does not know maul-ui")
endif()
separate_arguments(flags UNIX_COMMAND "${flags}")
run(${C_COMPILER} -std=c17 ${SOURCE_DIR}/samples/minimal/main.c -o ${root}/minimal-pc ${flags})
run(${CMAKE_COMMAND} -E env ${library_path} ${root}/minimal-pc)
