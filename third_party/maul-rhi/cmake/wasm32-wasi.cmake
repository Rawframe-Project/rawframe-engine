# The web without Emscripten (mrhi-0016): Clang's wasm32-wasi and
# wasi-libc, as a program built with a plain WebAssembly toolchain has.
#
#   cmake -B build -DCMAKE_TOOLCHAIN_FILE=cmake/wasm32-wasi.cmake [-DCMAKE_C_COMPILER=clang-20]
#
# Tests run under Node's WASI (test/wasi_run.mjs); the web tests run in a
# browser through test/web_runner.cjs.
set(CMAKE_SYSTEM_NAME WASI)
set(CMAKE_SYSTEM_PROCESSOR wasm32)
# CMake before 3.31 has no WASI platform: Platform/WASI.cmake beside this.
list(APPEND CMAKE_MODULE_PATH ${CMAKE_CURRENT_LIST_DIR})
if(NOT CMAKE_C_COMPILER)
    set(CMAKE_C_COMPILER clang)
endif()
set(CMAKE_C_COMPILER_TARGET wasm32-wasi)
set(CMAKE_CROSSCOMPILING_EMULATOR node;--no-warnings;${CMAKE_CURRENT_LIST_DIR}/../test/wasi_run.mjs)
set(CMAKE_FIND_ROOT_PATH_MODE_PROGRAM NEVER)
