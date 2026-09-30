# The WASI platform for cmake/wasm32-wasi.cmake where CMake has none of
# its own: static libraries, and programs that are .wasm files.
set(CMAKE_EXECUTABLE_SUFFIX ".wasm")
set(CMAKE_STATIC_LIBRARY_PREFIX "lib")
set(CMAKE_STATIC_LIBRARY_SUFFIX ".a")
