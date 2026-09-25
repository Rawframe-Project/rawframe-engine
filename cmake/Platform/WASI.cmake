# The WASI platform for cmake/wasm32-wasi.cmake: a static, single-module
# target whose programs are .wasm files.
set(CMAKE_EXECUTABLE_SUFFIX ".wasm")
set(CMAKE_STATIC_LIBRARY_PREFIX "lib")
set(CMAKE_STATIC_LIBRARY_SUFFIX ".a")
