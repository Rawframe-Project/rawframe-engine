# The cooked mesh (ADR-0058, ADR-0046): geometry in a form Rawframe owns,
# written by import tooling and read by the runtime. No renderer in it, so
# a server may read meshes for collision.
rawframe_module(NAME mesh
    SOURCES src/mesh.cpp
    DEPS result base
)

rawframe_module_tests(NAME mesh SOURCES tests/mesh_test.cpp)
