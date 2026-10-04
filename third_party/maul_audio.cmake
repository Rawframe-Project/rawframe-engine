# Maul Audio, vendored at the exact revision recorded in third_party/README.md
# and built by its own CMake, unchanged: the device layer the audio module's
# output plays through (D401), in place of miniaudio's devices. Its Linux
# backends open PipeWire, PulseAudio, and ALSA at run time and need only
# their headers to build. Native only: in a browser the page plays the sound
# (D259).
set(MAUL_AUDIO_BUILD_TESTS OFF CACHE BOOL "" FORCE)
set(MAUL_AUDIO_BUILD_BENCH OFF CACHE BOOL "" FORCE)
set(MAUL_AUDIO_INSTALL OFF CACHE BOOL "" FORCE)
add_subdirectory("${CMAKE_CURRENT_LIST_DIR}/maul-audio" "${CMAKE_BINARY_DIR}/third_party/maul-audio" EXCLUDE_FROM_ALL)
