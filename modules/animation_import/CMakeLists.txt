# Animation import (ADR-0039, SPEC-0035): a glTF 2.0 skin and its animations
# made into skeleton and clip documents, the authority the cook reads.
# Import tooling only; no client or server closure may depend on it, and the
# parser it holds is trusted only with an author's own files.
rawframe_module(NAME animation_import
    SOURCES src/import.cpp
    DEPS animation result base
)
target_link_libraries(rawframe_animation_import PRIVATE rawframe_cgltf)

rawframe_module_tests(NAME animation_import SOURCES tests/import_test.cpp)
