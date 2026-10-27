# Terrain documents: a ground's heights on a
# grid and its cells' layers as text, made into one mesh a part a layer,
# and the brushes that raise, smooth, and paint it. Import tooling: the
# cook's `rawframe.terrain` importer and the authoring session read it; no
# runtime links it.
rawframe_module(NAME terrain
    SOURCES src/terrain.cpp
    DEPS mesh result base
)

rawframe_module_tests(NAME terrain SOURCES tests/terrain_test.cpp)
