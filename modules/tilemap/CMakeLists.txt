# Tilemap documents (SPEC-0051's tilemap row, D857): a 2D level's tiles on
# a grid from one sheet, some solid, as text; painted, and its solid rows
# joined into bodies' boxes. Read by the cook's `rawframe.tilemap` importer,
# the authoring session, and the canvas and 2D physics that place one.
rawframe_module(NAME tilemap
    SOURCES src/tilemap.cpp
    DEPS result base
)

rawframe_module_tests(NAME tilemap SOURCES tests/tilemap_test.cpp)
