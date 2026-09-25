# Mesh import (ADR-0058): glTF 2.0 sources cooked into the runtime's mesh.
# Import tooling only; no client or server closure may depend on it, and
# the parser it holds is trusted only with an author's own files.
rawframe_module(NAME mesh_import
    SOURCES src/import.cpp
    DEPS mesh result base
)
target_link_libraries(rawframe_mesh_import PRIVATE rawframe_cgltf)

rawframe_module_tests(NAME mesh_import SOURCES tests/import_test.cpp)
