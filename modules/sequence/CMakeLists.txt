# Sequences (SPEC-0051's sequence row, D869): a timeline of keys that
# start the game's animators on the entities cast in its tracks and fire
# its events, as text. Motion between keys belongs to the animators'
# clips (ADR-0039's one play pathway), never to the sequence.
rawframe_module(NAME sequence
    SOURCES src/sequence.cpp
    DEPS result base
)

rawframe_module_tests(NAME sequence SOURCES tests/sequence_test.cpp)
