# Materials (ADR-0027, ADR-0031, SPEC-0026): the `surface` family of graph
# documents, checked against SPEC-0026's Surface contract and compiled to a
# material's typed data blob. No renderer in it, so a cook reads materials
# without a device.
rawframe_module(NAME material
    SOURCES src/common.cpp src/material.cpp src/post_process.cpp
    DEPS graph document result base
)

rawframe_module_tests(NAME material SOURCES tests/material_test.cpp tests/post_process_test.cpp)
rawframe_module_fuzz(NAME material TARGET documents RUNS 10000 SEEDS tests/seeds SOURCES tests/fuzz_material.cpp)
rawframe_module_fuzz(NAME material TARGET post_processes RUNS 10000 SEEDS tests/post_seeds SOURCES tests/fuzz_post_process.cpp)
