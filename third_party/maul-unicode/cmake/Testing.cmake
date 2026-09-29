# maul_add_test(<name> [WHITEBOX] [THREADS] [POSIX] [MANUAL])
#
# Builds test/test_<name>.c into test_<name> and registers it with
# CTest as <name>.
#   WHITEBOX  includes src/ and reaches internal symbols, which a shared
#             build hides, so the suite is skipped in shared builds.
#   THREADS   links the platform thread library.
#   POSIX     uses POSIX threads directly and is skipped on Windows.
#   MANUAL    is built but not registered: soaks and timing runs.

function(maul_add_test name)
    cmake_parse_arguments(ARG "WHITEBOX;THREADS;POSIX;MANUAL" "" "" ${ARGN})
    if(ARG_POSIX AND WIN32)
        return()
    endif()
    if(ARG_WHITEBOX AND ${MAUL_PREFIX}_BUILD_SHARED)
        return()
    endif()
    set(target test_${name})
    add_executable(${target} ${PROJECT_SOURCE_DIR}/test/test_${name}.c)
    target_link_libraries(${target} PRIVATE ${PROJECT_NAME})
    if(ARG_WHITEBOX)
        target_include_directories(${target} PRIVATE ${PROJECT_SOURCE_DIR}/src)
    endif()
    if(ARG_THREADS OR ARG_POSIX)
        find_package(Threads REQUIRED)
        target_link_libraries(${target} PRIVATE Threads::Threads)
    endif()
    maul_apply_flags(${target})
    if(NOT ARG_MANUAL)
        add_test(NAME ${name} COMMAND ${target})
    endif()
endfunction()

# maul_add_header_tests()
#
# Compiles every public header on its own as C17, C23 and C++17, with
# pedantic warnings as errors, so the headers stay in the common subset
# the family requires. A macro, because enable_language must run at
# directory scope. Without a C++ compiler the C++17 check is skipped.
macro(maul_add_header_tests)
    include(CheckLanguage)
    check_language(CXX)
    if(CMAKE_CXX_COMPILER)
        enable_language(CXX)
    endif()
    file(GLOB maul_headers RELATIVE ${PROJECT_SOURCE_DIR}/include
         ${PROJECT_SOURCE_DIR}/include/${PROJECT_NAME}/*.h)
    set(maul_header_dir ${PROJECT_BINARY_DIR}/header_tests)
    set(maul_c17_sources "")
    set(maul_c23_sources "")
    set(maul_cxx_sources "")
    foreach(maul_header ${maul_headers})
        string(MAKE_C_IDENTIFIER ${maul_header} maul_stem)
        file(WRITE ${maul_header_dir}/c17_${maul_stem}.c "#include \"${maul_header}\"\n")
        file(WRITE ${maul_header_dir}/c23_${maul_stem}.c "#include \"${maul_header}\"\n")
        file(WRITE ${maul_header_dir}/cxx_${maul_stem}.cpp "#include \"${maul_header}\"\n")
        list(APPEND maul_c17_sources ${maul_header_dir}/c17_${maul_stem}.c)
        list(APPEND maul_c23_sources ${maul_header_dir}/c23_${maul_stem}.c)
        list(APPEND maul_cxx_sources ${maul_header_dir}/cxx_${maul_stem}.cpp)
    endforeach()
    if(MSVC)
        set(maul_header_flags /W4 /WX)
    else()
        set(maul_header_flags -Wall -Wextra -Wpedantic -Werror)
    endif()
    add_library(header_test_c17 OBJECT ${maul_c17_sources})
    set_target_properties(header_test_c17 PROPERTIES C_STANDARD 17 C_STANDARD_REQUIRED ON C_EXTENSIONS OFF)
    add_library(header_test_c23 OBJECT ${maul_c23_sources})
    set_target_properties(header_test_c23 PROPERTIES C_STANDARD 23 C_STANDARD_REQUIRED ON C_EXTENSIONS OFF)
    set(maul_header_targets header_test_c17 header_test_c23)
    if(CMAKE_CXX_COMPILER)
        add_library(header_test_cxx17 OBJECT ${maul_cxx_sources})
        set_target_properties(header_test_cxx17 PROPERTIES CXX_STANDARD 17 CXX_STANDARD_REQUIRED ON
                                                           CXX_EXTENSIONS OFF)
        list(APPEND maul_header_targets header_test_cxx17)
    endif()
    foreach(maul_target ${maul_header_targets})
        target_include_directories(${maul_target} PRIVATE ${PROJECT_SOURCE_DIR}/include)
        target_compile_options(${maul_target} PRIVATE ${maul_header_flags})
    endforeach()
endmacro()
