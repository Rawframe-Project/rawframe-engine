# Graph documents (ADR-0027, SPEC-0028): the one grammar, identity, and
# semantic hash every graph family is read, written, and hashed by. What a
# node means belongs to its family (animation's graphs, the materials').
rawframe_module(NAME graph
    SOURCES src/graph.cpp
    DEPS document result base
)

rawframe_module_tests(NAME graph SOURCES tests/graph_test.cpp)
