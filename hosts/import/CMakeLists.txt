# The sound import tool (ADR-0058): import tooling's process entry, over
# rawframe.audio_import. No runtime closure contains it.
rawframe_host(NAME import OUTPUT rawframe-import
    SOURCES src/main.cpp
    DEPS audio_import audio result base
)

if(RAWFRAME_BUILD_TESTS)
    # A Vorbis source is cooked into a WAVE the runtime reads.
    add_test(NAME import_vorbis
             COMMAND rawframe_host_import ${PROJECT_SOURCE_DIR}/modules/audio_import/tests/data/tone.ogg
                     ${CMAKE_CURRENT_BINARY_DIR}/tone.wav)
    set_tests_properties(import_vorbis PROPERTIES
                         PASS_REGULAR_EXPRESSION "imported vorbis: 2 channels at 48000 Hz, [0-9]+ frames")
    # A bad argument is a usage error, not a crash.
    add_test(NAME import_usage COMMAND rawframe_host_import)
    set_tests_properties(import_usage PROPERTIES WILL_FAIL TRUE)
endif()
