# Audio (ADR-0038, SPEC-0036): the mixer layout, the mixer rendering into
# buffers, and the output rendering it to a device. Client only: the dedicated
# server links none of it. miniaudio and libopus are linked privately, so no
# header above this module can include them.
# The web has no device here until Maul Audio brings one (D174): its output
# refuses to open, and a client runs on without sound.
if(CMAKE_SYSTEM_NAME STREQUAL "WASI")
    set(rawframe_audio_output src/output_none.cpp)
else()
    set(rawframe_audio_output src/output.cpp)
endif()
rawframe_module(NAME audio
    SOURCES src/decode.cpp src/effects.cpp src/layout.cpp src/mixer.cpp src/opus.cpp ${rawframe_audio_output} src/sound.cpp src/sounds.cpp src/stream.cpp
    DEPS world document result base
)
target_link_libraries(rawframe_audio PRIVATE rawframe_opus_vendor)
if(NOT CMAKE_SYSTEM_NAME STREQUAL "WASI")
    target_link_libraries(rawframe_audio PRIVATE rawframe_miniaudio_vendor)
endif()

# The device's tests need a device, which the web has none of yet.
set(rawframe_audio_output_tests "")
if(NOT CMAKE_SYSTEM_NAME STREQUAL "WASI")
    set(rawframe_audio_output_tests tests/output_test.cpp)
endif()
rawframe_module_tests(NAME audio SOURCES tests/decode_test.cpp tests/effects_test.cpp tests/layout_test.cpp tests/mixer_test.cpp
    ${rawframe_audio_output_tests} tests/sound_test.cpp tests/sounds_test.cpp tests/stream_test.cpp)
if(TARGET rawframe_audio_tests)
    target_compile_definitions(rawframe_audio_tests PRIVATE RAWFRAME_AUDIO_DATA="${CMAKE_CURRENT_SOURCE_DIR}/tests/data/")
endif()
