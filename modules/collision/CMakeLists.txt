# SPEC-0037's collision document, shared by 2D and 3D physics: collision
# classes, the rules between them, and the Maul category and mask bits that
# carry them.
rawframe_module(NAME collision
    SOURCES src/filters.cpp
    DEPS result base
)

rawframe_module_tests(NAME collision SOURCES tests/filters_test.cpp)
