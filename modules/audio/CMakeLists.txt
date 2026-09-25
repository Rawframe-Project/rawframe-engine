# Audio (ADR-0038, SPEC-0036): the mixer layout, and the mixer rendering
# into buffers. Client only: the dedicated server links none of it.
rawframe_module(NAME audio
    SOURCES src/decode.cpp src/layout.cpp src/mixer.cpp src/sound.cpp
    DEPS document result base
)

rawframe_module_tests(NAME audio SOURCES tests/decode_test.cpp tests/layout_test.cpp tests/mixer_test.cpp tests/sound_test.cpp)
