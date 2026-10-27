# Scatter documents: many copies of one
# prefab placed over an area, apart from each other, on a terrain's ground
# and its painted layer where one is named, as text. Placed alike wherever
# a game is loaded, so a server and its clients hold the same.
rawframe_module(NAME scatter
    SOURCES src/scatter.cpp
    DEPS terrain mesh result base
)

rawframe_module_tests(NAME scatter SOURCES tests/scatter_test.cpp)
