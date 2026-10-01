# Particles, trails, and beams on a client (ADR-0053, D357): the component
# triad as C++ reads it, and one view's accounting of them from frame to
# frame, which the scene and the canvas both draw through. Client only: the
# dedicated server carries the components as plain values and links none of
# this.
rawframe_module(NAME particles
    SOURCES src/particles.cpp src/ribbons.cpp
    DEPS world base
)

rawframe_module_tests(NAME particles SOURCES tests/particle_test.cpp tests/ribbon_test.cpp)
