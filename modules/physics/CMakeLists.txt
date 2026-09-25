# What 2D and 3D physics share (SPEC-0037 §14's integration grammar): the
# collision document and the Maul category and mask bits that carry it, and
# the moment a query is cast back to.
rawframe_module(NAME physics
    SOURCES src/filters.cpp
    DEPS world schema execution diagnostics result base
)

rawframe_module_tests(NAME physics SOURCES tests/filters_test.cpp)
