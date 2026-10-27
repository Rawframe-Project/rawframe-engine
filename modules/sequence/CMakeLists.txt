# Sequences: a timeline of keys that
# start the game's animators on the entities cast in its tracks and fire
# its events, as text. Motion between keys belongs to the animators'
# clips, never to the sequence.
rawframe_module(NAME sequence
    SOURCES src/sequence.cpp
    DEPS result base
)

rawframe_module_tests(NAME sequence SOURCES tests/sequence_test.cpp)
