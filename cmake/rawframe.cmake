# Build policy shared by every module: language, warnings, determinism,
# configurations, sanitizers, and the module helper.

# The three configurations (ADR-0006). Each fixes the assertion level that
# rawframe/base/assert.h reads: full in debug and development, contract-only in
# shipping, where RAWFRAME_ASSERT is removed and RAWFRAME_CHECK stays.
set(RAWFRAME_CONFIGURATION "development" CACHE STRING "debug, development, or shipping")
set_property(CACHE RAWFRAME_CONFIGURATION PROPERTY STRINGS debug development shipping)
if(RAWFRAME_CONFIGURATION STREQUAL "shipping")
    set(RAWFRAME_ASSERTION_LEVEL 1)
    set(RAWFRAME_SHIPPING 1)
elseif(RAWFRAME_CONFIGURATION STREQUAL "debug" OR RAWFRAME_CONFIGURATION STREQUAL "development")
    set(RAWFRAME_ASSERTION_LEVEL 2)
    set(RAWFRAME_SHIPPING 0)
else()
    message(FATAL_ERROR "RAWFRAME_CONFIGURATION must be debug, development, or shipping, not '${RAWFRAME_CONFIGURATION}'")
endif()

# The C math library, a library of its own on POSIX and part of the C
# runtime on Windows (D237).
if(WIN32)
    set(RAWFRAME_MATH_LIBRARY "")
else()
    set(RAWFRAME_MATH_LIBRARY m)
endif()

# What runs a test written as a shell script: the script itself where the
# system reads its first line, Git's bash on Windows (D237).
if(WIN32)
    find_program(RAWFRAME_BASH NAMES bash REQUIRED)
    set(RAWFRAME_SHELL "${RAWFRAME_BASH}")
else()
    set(RAWFRAME_SHELL "")
endif()

set(RAWFRAME_SANITIZE "" CACHE STRING "Sanitizers: empty, `address` (with undefined behaviour), or `thread`")

# The allowed dependency table. One line per module: `name: dep dep ...`.
file(STRINGS "${PROJECT_SOURCE_DIR}/tools/modules.txt" rawframe_module_lines REGEX "^[a-z0-9_]+:")
foreach(line IN LISTS rawframe_module_lines)
    string(REGEX MATCH "^([a-z0-9_]+):(.*)$" _ "${line}")
    string(STRIP "${CMAKE_MATCH_2}" deps)
    separate_arguments(deps)
    set(RAWFRAME_ALLOWED_DEPS_${CMAKE_MATCH_1} "${deps}")
    set(RAWFRAME_KNOWN_MODULE_${CMAKE_MATCH_1} TRUE)
endforeach()

add_library(rawframe_policy INTERFACE)
target_compile_features(rawframe_policy INTERFACE cxx_std_23)
target_compile_definitions(rawframe_policy INTERFACE
    RAWFRAME_ASSERTIONS=${RAWFRAME_ASSERTION_LEVEL}
    # 1 in shipping, where development-only surfaces refuse to compile.
    RAWFRAME_SHIPPING=${RAWFRAME_SHIPPING}
    RAWFRAME_CONFIGURATION_NAME="${RAWFRAME_CONFIGURATION}")
# On Windows (D237): windows.h without its min and max macros, which break
# std::min and std::max, and without the rarely used half of its headers; the
# standard C library's functions without MSVC's deprecation of them in favour
# of its own _s variants; and MSVC's library without its vectorized find,
# which asserts on 16-byte values such as Bits128 when Clang compiles it.
if(WIN32)
    target_compile_definitions(rawframe_policy INTERFACE
        NOMINMAX WIN32_LEAN_AND_MEAN _CRT_SECURE_NO_WARNINGS _USE_STD_VECTOR_ALGORITHMS=0)
endif()

if(MSVC AND CMAKE_CXX_COMPILER_ID STREQUAL "Clang")
    # clang-cl (D237): MSVC's library and linker, Clang's warnings as on
    # Linux, and contraction turned off the way Clang spells it.
    target_compile_options(rawframe_policy INTERFACE
        /W4 /WX /Zc:__cplusplus /utf-8
        /GR-            # no RTTI (ADR-0008)
        /EHs-c-         # no exceptions (ADR-0008)
        -Wpedantic -Wshadow -Wno-missing-field-initializers
        # Precise floating point is its default; /fp:precise would be
        # overridden by the contraction option.
        /clang:-ffp-contract=off  # deterministic simulation
    )
    target_compile_definitions(rawframe_policy INTERFACE _HAS_EXCEPTIONS=0)
    # Clang's own runtime routines, such as 128-bit division, which MSVC's
    # libraries do not carry.
    execute_process(
        COMMAND "${CMAKE_CXX_COMPILER}" /clang:--rtlib=compiler-rt /clang:-print-libgcc-file-name
        OUTPUT_VARIABLE rawframe_clang_builtins OUTPUT_STRIP_TRAILING_WHITESPACE)
    if(NOT EXISTS "${rawframe_clang_builtins}")
        # Installers that keep the older layout, one directory per system.
        execute_process(COMMAND "${CMAKE_CXX_COMPILER}" /clang:-print-resource-dir
                        OUTPUT_VARIABLE rawframe_clang_resources OUTPUT_STRIP_TRAILING_WHITESPACE)
        file(TO_CMAKE_PATH "${rawframe_clang_resources}" rawframe_clang_resources)
        set(rawframe_clang_builtins "${rawframe_clang_resources}/lib/windows/clang_rt.builtins-x86_64.lib")
    endif()
    if(NOT EXISTS "${rawframe_clang_builtins}")
        message(FATAL_ERROR "clang-cl names no builtins library ('${rawframe_clang_builtins}')")
    endif()
    target_link_libraries(rawframe_policy INTERFACE "${rawframe_clang_builtins}")
elseif(MSVC)
    target_compile_options(rawframe_policy INTERFACE
        /W4 /WX /permissive- /Zc:__cplusplus /utf-8
        /GR-            # no RTTI (ADR-0008)
        /EHs-c-         # no exceptions (ADR-0008)
        /fp:precise /fp:contract-  # no floating-point contraction: deterministic simulation
    )
    target_compile_definitions(rawframe_policy INTERFACE _HAS_EXCEPTIONS=0)
else()
    target_compile_options(rawframe_policy INTERFACE
        -Wall -Wextra -Wpedantic -Wshadow -Werror
        # Omitting a member that has a default from an initializer, designated or
        # not, is the intended idiom for records and settings.
        -Wno-missing-field-initializers
        -fno-exceptions -fno-rtti
        -ffp-contract=off  # no fused multiply-add behind our back: deterministic simulation
        # Source locations in diagnostics name repository paths, never the
        # build machine's directories.
        -fmacro-prefix-map=${PROJECT_SOURCE_DIR}/=
    )
    if(RAWFRAME_SANITIZE STREQUAL "address")
        target_compile_options(rawframe_policy INTERFACE -fsanitize=address,undefined -fno-sanitize-recover=all -fno-omit-frame-pointer)
        target_link_options(rawframe_policy INTERFACE -fsanitize=address,undefined)
    elseif(RAWFRAME_SANITIZE STREQUAL "thread")
        target_compile_options(rawframe_policy INTERFACE -fsanitize=thread -fno-omit-frame-pointer)
        target_link_options(rawframe_policy INTERFACE -fsanitize=thread)
    elseif(RAWFRAME_SANITIZE)
        # Any false value (empty, OFF) means no sanitizer.
        message(FATAL_ERROR "RAWFRAME_SANITIZE must be empty, address, or thread")
    endif()
endif()

# Gives a target options spelled as GCC and Clang spell them, the way its
# compiler reads them: as they are, or through /clang: under clang-cl, whose
# own -Wall means every warning (D237). MSVC's own compiler reads neither
# and gets none.
#
#   rawframe_gnu_options(target PRIVATE -ffp-contract=off -Wall)
if(MSVC AND NOT CMAKE_C_COMPILER_ID STREQUAL "Clang")
    set(RAWFRAME_GNU_OPTIONS FALSE)
else()
    set(RAWFRAME_GNU_OPTIONS TRUE)
endif()
function(rawframe_gnu_options target scope)
    if(NOT RAWFRAME_GNU_OPTIONS)
        return()
    endif()
    if(MSVC)
        list(TRANSFORM ARGN PREPEND "/clang:")
    endif()
    target_compile_options(${target} ${scope} ${ARGN})
endfunction()

# Declares one module. Checks its dependencies against tools/modules.txt so a
# boundary cannot be crossed by editing a CMakeLists.txt alone.
#
#   rawframe_module(NAME base SOURCES src/a.cpp DEPS other)
function(rawframe_module)
    cmake_parse_arguments(arg "" "NAME" "SOURCES;DEPS" ${ARGN})
    if(NOT RAWFRAME_KNOWN_MODULE_${arg_NAME})
        message(FATAL_ERROR "module '${arg_NAME}' is not listed in tools/modules.txt")
    endif()
    foreach(dep IN LISTS arg_DEPS)
        if(NOT dep IN_LIST RAWFRAME_ALLOWED_DEPS_${arg_NAME})
            message(FATAL_ERROR "module '${arg_NAME}' may not depend on '${dep}' (tools/modules.txt)")
        endif()
    endforeach()

    set(target rawframe_${arg_NAME})
    add_library(${target} STATIC ${arg_SOURCES})
    add_library(rawframe::${arg_NAME} ALIAS ${target})
    target_include_directories(${target} PUBLIC include)
    target_link_libraries(${target} PUBLIC rawframe_policy)
    foreach(dep IN LISTS arg_DEPS)
        target_link_libraries(${target} PUBLIC rawframe::${dep})
    endforeach()
endfunction()

# Declares a host: a process entry under hosts/<name>, checked against
# tools/modules.txt like a module. Nothing may depend on a host.
#
#   rawframe_host(NAME dedicated_server OUTPUT rawframe-server SOURCES src/main.cpp DEPS host)
function(rawframe_host)
    cmake_parse_arguments(arg "" "NAME;OUTPUT" "SOURCES;DEPS" ${ARGN})
    if(NOT RAWFRAME_KNOWN_MODULE_${arg_NAME})
        message(FATAL_ERROR "host '${arg_NAME}' is not listed in tools/modules.txt")
    endif()
    foreach(dep IN LISTS arg_DEPS)
        if(NOT dep IN_LIST RAWFRAME_ALLOWED_DEPS_${arg_NAME})
            message(FATAL_ERROR "host '${arg_NAME}' may not depend on '${dep}' (tools/modules.txt)")
        endif()
    endforeach()
    set(target rawframe_host_${arg_NAME})
    add_executable(${target} ${arg_SOURCES})
    set_target_properties(${target} PROPERTIES OUTPUT_NAME ${arg_OUTPUT})
    target_link_libraries(${target} PRIVATE rawframe_policy)
    foreach(dep IN LISTS arg_DEPS)
        target_link_libraries(${target} PRIVATE rawframe::${dep})
    endforeach()
endfunction()

# Declares a module's test executable and registers it with CTest.
#
#   rawframe_module_tests(NAME base SOURCES tests/a_test.cpp)
function(rawframe_module_tests)
    cmake_parse_arguments(arg "" "NAME" "SOURCES" ${ARGN})
    if(NOT RAWFRAME_BUILD_TESTS)
        return()
    endif()
    set(target rawframe_${arg_NAME}_tests)
    add_executable(${target} ${arg_SOURCES})
    target_link_libraries(${target} PRIVATE rawframe::${arg_NAME} rawframe::test)
    add_test(NAME ${arg_NAME} COMMAND ${target})
endfunction()
