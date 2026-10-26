# Splines: a smooth path through points,
# measured along its length. The points are a scene's own entities, a
# spline's fields (rawframe.spline); this is the curve they make, which a
# game's followers move along and Studio draws.
rawframe_module(NAME spline
    SOURCES src/spline.cpp
    DEPS base
)

rawframe_module_tests(NAME spline SOURCES tests/spline_test.cpp)
