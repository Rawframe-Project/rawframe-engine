# WebAssembly without threads (ADR-0084's web readiness): wasm32-wasi from
# Clang, wasi-libc, and libc++ for wasm32. What builds here builds for a
# browser's main thread too; the browser toolchain itself arrives with the
# web client. Tests run under Node's WASI through tools/wasi_run.mjs.
set(CMAKE_SYSTEM_NAME WASI)
set(CMAKE_SYSTEM_PROCESSOR wasm32)
set(CMAKE_C_COMPILER clang-20)
set(CMAKE_CXX_COMPILER clang++-20)
set(CMAKE_C_COMPILER_TARGET wasm32-wasi)
set(CMAKE_CXX_COMPILER_TARGET wasm32-wasi)
set(CMAKE_EXECUTABLE_SUFFIX ".wasm")
set(CMAKE_CROSSCOMPILING_EMULATOR node;--no-warnings;${CMAKE_CURRENT_LIST_DIR}/../tools/wasi_run.mjs)
set(CMAKE_FIND_ROOT_PATH_MODE_PROGRAM NEVER)
