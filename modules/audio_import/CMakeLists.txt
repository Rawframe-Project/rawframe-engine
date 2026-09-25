# Sound import (ADR-0058, SPEC-0036): WAVE, Ogg Vorbis, and MP3 sources
# decoded and cooked into the runtime's form. Import tooling only; no client
# or server closure may depend on it, and the source decoders it holds are
# trusted only with an author's own files.
rawframe_module(NAME audio_import
    SOURCES src/import.cpp
    DEPS audio result base
)
# miniaudio's MP3 decoder and stb_vorbis, in C, with miniaudio internal to
# the one file that holds them.
add_library(rawframe_audio_import_decoders STATIC src/decoders.c)
set_target_properties(rawframe_audio_import_decoders PROPERTIES C_STANDARD 11 POSITION_INDEPENDENT_CODE ON)
target_link_libraries(rawframe_audio_import_decoders PRIVATE rawframe_miniaudio_source)
target_link_libraries(rawframe_audio_import PRIVATE rawframe_audio_import_decoders)

rawframe_module_tests(NAME audio_import SOURCES tests/import_test.cpp)
if(TARGET rawframe_audio_import_tests)
    target_compile_definitions(rawframe_audio_import_tests PRIVATE
        RAWFRAME_AUDIO_IMPORT_DATA="${CMAKE_CURRENT_SOURCE_DIR}/tests/data/")
endif()
