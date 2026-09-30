# Materials (ADR-0027, ADR-0031, SPEC-0026): the `surface` family of graph
# documents, checked against SPEC-0026's Surface contract and compiled to a
# material's typed data blob. No renderer in it, so a cook reads materials
# without a device.
rawframe_module(NAME material
    SOURCES src/material.cpp
    DEPS graph document result base
)

rawframe_module_tests(NAME material SOURCES tests/material_test.cpp)
rawframe_module_fuzz(NAME material TARGET documents RUNS 10000 SEEDS tests/seeds SOURCES tests/fuzz_material.cpp)
