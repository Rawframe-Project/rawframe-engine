# Blockout documents: a level's rough
# shapes as text, a solid a line, made into one mesh by solid geometry,
# cuts taken out by a BSP tree. Import tooling: the cook's
# `rawframe.blockout` importer reads it; no runtime links it.
rawframe_module(NAME blockout
    SOURCES src/parse.cpp src/build.cpp
    DEPS mesh result base
)

rawframe_module_tests(NAME blockout SOURCES tests/blockout_test.cpp)
