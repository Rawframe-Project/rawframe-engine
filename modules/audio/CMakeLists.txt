# Audio (ADR-0038, SPEC-0036): the mixer layout, the mixer rendering into
# buffers, and the output rendering it to a device. Client only: the dedicated
# server links none of it. miniaudio is linked privately, so no header above
# this module can include it.
rawframe_module(NAME audio
    SOURCES src/decode.cpp src/effects.cpp src/layout.cpp src/mixer.cpp src/output.cpp src/sound.cpp src/sounds.cpp
    DEPS world document result base
)
target_link_libraries(rawframe_audio PRIVATE rawframe_miniaudio_vendor)

rawframe_module_tests(NAME audio SOURCES tests/decode_test.cpp tests/effects_test.cpp tests/layout_test.cpp tests/mixer_test.cpp tests/output_test.cpp tests/sound_test.cpp tests/sounds_test.cpp)
