# Sound import: WAVE, Ogg Vorbis, and MP3 sources
# decoded and cooked into the runtime's form. Import tooling only; no client
# or server closure may depend on it, and the source decoders it holds are
# trusted only with an author's own files.
rawframe_module(NAME audio_import
    SOURCES src/import.cpp src/resample.cpp
    DEPS audio result base
)
# miniaudio's MP3 decoder and stb_vorbis, in C, with miniaudio internal to
# the one file that holds them.
add_library(rawframe_audio_import_decoders STATIC src/decoders.c)
set_target_properties(rawframe_audio_import_decoders PROPERTIES C_STANDARD 11 POSITION_INDEPENDENT_CODE ON)
target_link_libraries(rawframe_audio_import_decoders PRIVATE rawframe_miniaudio_source)
rawframe_sanitize_vendored(rawframe_audio_import_decoders)
# stb_vorbis's get32_packet shifts the end-of-packet marker, -1, left as it
# assembles a word, which C leaves undefined and GCC and Clang define as two's
# complement; it relies on that value, and the vendored file is upstream's.
# Every other check stays on.
if(RAWFRAME_SANITIZER_OPTIONS)
    target_compile_options(rawframe_audio_import_decoders PRIVATE -fno-sanitize=shift-base)
endif()
target_link_libraries(rawframe_audio_import PRIVATE rawframe_audio_import_decoders rawframe_opus_vendor)

rawframe_module_tests(NAME audio_import SOURCES tests/import_test.cpp)
if(TARGET rawframe_audio_import_tests)
    target_compile_definitions(rawframe_audio_import_tests PRIVATE
        RAWFRAME_AUDIO_IMPORT_DATA="${RAWFRAME_TEST_FILES}/modules/audio_import/tests/data/")
    # The whole suite takes under four seconds in the sanitizer tree. A
    # Vorbis stream of many comments once took minutes to refuse, and the
    # suite's test of it fails here rather than at the check's own limit.
    set_tests_properties(audio_import PROPERTIES TIMEOUT 30)
endif()
rawframe_module_fuzz(NAME audio_import TARGET import RUNS 10000 MAX_LEN 16384 SEEDS tests/seeds SOURCES tests/fuzz_import.cpp)
