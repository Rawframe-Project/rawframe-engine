# Build policy shared by every module: language, warnings, determinism,
# configurations, sanitizers, and the module helper.

# The three configurations (ADR-0006). Each fixes the assertion level that
# rawframe/base/assert.h reads: full in debug and development, contract-only in
# shipping, where RAWFRAME_ASSERT is removed and RAWFRAME_CHECK stays.
set(RAWFRAME_CONFIGURATION "development" CACHE STRING "debug, development, or shipping")
set_property(CACHE RAWFRAME_CONFIGURATION PROPERTY STRINGS debug development shipping)
if(RAWFRAME_CONFIGURATION STREQUAL "shipping")
    set(RAWFRAME_ASSERTION_LEVEL 1)
elseif(RAWFRAME_CONFIGURATION STREQUAL "debug" OR RAWFRAME_CONFIGURATION STREQUAL "development")
    set(RAWFRAME_ASSERTION_LEVEL 2)
else()
    message(FATAL_ERROR "RAWFRAME_CONFIGURATION must be debug, development, or shipping, not '${RAWFRAME_CONFIGURATION}'")
endif()

set(RAWFRAME_SANITIZE "" CACHE STRING "Sanitizers: empty, `address` (with undefined behaviour), or `thread`")

# The allowed dependency table. One line per module: `name: dep dep ...`.
file(STRINGS "${PROJECT_SOURCE_DIR}/tools/modules.txt" rawframe_module_lines REGEX "^[a-z_]+:")
foreach(line IN LISTS rawframe_module_lines)
    string(REGEX MATCH "^([a-z_]+):(.*)$" _ "${line}")
    string(STRIP "${CMAKE_MATCH_2}" deps)
    separate_arguments(deps)
    set(RAWFRAME_ALLOWED_DEPS_${CMAKE_MATCH_1} "${deps}")
    set(RAWFRAME_KNOWN_MODULE_${CMAKE_MATCH_1} TRUE)
endforeach()

add_library(rawframe_policy INTERFACE)
target_compile_features(rawframe_policy INTERFACE cxx_std_23)
target_compile_definitions(rawframe_policy INTERFACE RAWFRAME_ASSERTIONS=${RAWFRAME_ASSERTION_LEVEL})

if(MSVC)
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
