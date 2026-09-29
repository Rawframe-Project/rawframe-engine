# The cooked texture (ADR-0058, ADR-0047, D253): levels in a form the GPU
# takes as they are, in a KTX 2.0 container, written by import tooling and
# read by the runtime. No renderer in it.
rawframe_module(NAME texture
    SOURCES src/texture.cpp
    DEPS result base
)

rawframe_module_tests(NAME texture SOURCES tests/texture_test.cpp)
rawframe_module_fuzz(NAME texture TARGET texture RUNS 25000 MAX_LEN 16384 SEEDS tests/seeds SOURCES tests/fuzz_texture.cpp)
