# The package test (MAUL_AUDIO_TEST_PACKAGE): installs the build into
# BINARY_DIR/package-test/prefix, then builds samples/minimal against it
# with nothing else on its search path and runs it, as a consumer of the
# package would; on Linux and macOS also through pkg-config, statically
# when the library is static. Run by CTest as the test "package"; the
# build must be complete.
#
#   cmake -DBINARY_DIR=... -DSOURCE_DIR=... -DGENERATOR=... -DC_COMPILER=...
#         -DCONFIG=... -DSHARED=ON|OFF -P PackageTest.cmake

set(root ${BINARY_DIR}/package-test)
file(REMOVE_RECURSE ${root})

function(run)
    execute_process(COMMAND ${ARGN} RESULT_VARIABLE result)
    if(NOT result EQUAL 0)
        list(JOIN ARGN " " command)
        message(FATAL_ERROR "failed (${result}): ${command}")
    endif()
endfunction()

set(prefix ${root}/prefix)
set(config_args "")
if(CONFIG)
    set(config_args --config ${CONFIG})
endif()
run(${CMAKE_COMMAND} --install ${BINARY_DIR} --prefix ${prefix} ${config_args})

set(compiler_args "")
if(NOT GENERATOR MATCHES "Visual Studio")
    set(compiler_args -DCMAKE_C_COMPILER=${C_COMPILER})
endif()
run(${CMAKE_COMMAND} -S ${SOURCE_DIR}/samples/minimal -B ${root}/build -G ${GENERATOR}
    ${compiler_args} -DCMAKE_PREFIX_PATH=${prefix})
run(${CMAKE_COMMAND} --build ${root}/build ${config_args})
set(minimal ${root}/build/minimal)
if(CONFIG AND EXISTS ${root}/build/${CONFIG}/minimal.exe)
    set(minimal ${root}/build/${CONFIG}/minimal.exe)
endif()
# The installed library is found by the loader's path, never by the build's
# rpath; on Windows a DLL is found on PATH.
set(ENV{LD_LIBRARY_PATH} "${prefix}/lib:${prefix}/lib64")
set(ENV{DYLD_LIBRARY_PATH} "${prefix}/lib")
if(CMAKE_HOST_WIN32)
    set(ENV{PATH} "${prefix}/bin;$ENV{PATH}")
endif()
run(${minimal})

# pkg-config, where the platform has it.
find_program(pkg_config pkg-config)
if(pkg_config AND NOT WIN32)
    set(static "")
    if(NOT SHARED)
        set(static --static)
    endif()
    set(ENV{PKG_CONFIG_PATH} "${prefix}/lib/pkgconfig:${prefix}/lib64/pkgconfig")
    execute_process(COMMAND ${pkg_config} ${static} --cflags --libs maul-audio
                    OUTPUT_VARIABLE flags OUTPUT_STRIP_TRAILING_WHITESPACE RESULT_VARIABLE result)
    if(NOT result EQUAL 0)
        message(FATAL_ERROR "pkg-config does not find maul-audio in ${prefix}")
    endif()
    separate_arguments(flags UNIX_COMMAND "${flags}")
    run(${C_COMPILER} -std=c17 ${SOURCE_DIR}/samples/minimal/main.c -o ${root}/minimal-pc ${flags})
    run(${root}/minimal-pc)
endif()
