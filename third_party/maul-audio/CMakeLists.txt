cmake_minimum_required(VERSION 3.25)

# The library's identity. The family's CMake modules and tools read these.
set(MAUL_LIBRARY maul-audio)
set(MAUL_API_PREFIX maud)
set(MAUL_MACRO_PREFIX MAUD)
set(MAUL_TITLE "Maul Audio")
set(MAUL_PREFIX MAUL_AUDIO)

include(cmake/ReadVersion.cmake)
maul_read_version(${CMAKE_CURRENT_SOURCE_DIR}/include/maul-audio/base.h MAUD MAUL_VERSION)
project(maul-audio
    VERSION ${MAUL_VERSION}
    DESCRIPTION "Audio device I/O and spatial audio"
    LANGUAGES C)

option(MAUL_AUDIO_BUILD_TESTS "Build the test suites" ${PROJECT_IS_TOP_LEVEL})
option(MAUL_AUDIO_BUILD_BENCH "Build the benchmarks" ${PROJECT_IS_TOP_LEVEL})
option(MAUL_AUDIO_BUILD_SHARED "Build a shared library instead of a static one" OFF)
option(MAUL_AUDIO_INSTALL "Generate install rules" ${PROJECT_IS_TOP_LEVEL})
option(MAUL_AUDIO_WERROR "Treat compiler warnings as errors" OFF)
option(MAUL_AUDIO_SANITIZE "Instrument with AddressSanitizer and UBSan" OFF)
option(MAUL_AUDIO_TSAN "Instrument with ThreadSanitizer" OFF)
option(MAUL_AUDIO_COVERAGE "Instrument for gcov coverage" OFF)
if(CMAKE_SYSTEM_NAME STREQUAL "Linux")
    set(MAUL_AUDIO_PIPEWIRE_DEFAULT ON)
else()
    set(MAUL_AUDIO_PIPEWIRE_DEFAULT OFF)
endif()
option(MAUL_AUDIO_PIPEWIRE "Build the PipeWire backend, loaded at run time"
    ${MAUL_AUDIO_PIPEWIRE_DEFAULT})
option(MAUL_AUDIO_PULSE "Build the PulseAudio backend, loaded at run time"
    ${MAUL_AUDIO_PIPEWIRE_DEFAULT})
option(MAUL_AUDIO_ALSA "Build the ALSA backend, loaded at run time"
    ${MAUL_AUDIO_PIPEWIRE_DEFAULT})
option(MAUL_AUDIO_WASAPI "Build the WASAPI backend" ${WIN32})
option(MAUL_AUDIO_COREAUDIO "Build the CoreAudio backend" ${APPLE})

# Every executable (tests, benchmarks, tools) lands in the top of the
# build tree, so scripts and CI find them in one place.
if(PROJECT_IS_TOP_LEVEL)
    set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${PROJECT_BINARY_DIR})
endif()

include(GNUInstallDirs)
include(cmake/CompilerFlags.cmake)
include(cmake/Library.cmake)
include(cmake/Testing.cmake)

if(MAUL_AUDIO_BUILD_SHARED)
    set(MAUL_LIBRARY_KIND SHARED)
else()
    set(MAUL_LIBRARY_KIND STATIC)
endif()
add_library(maul-audio ${MAUL_LIBRARY_KIND}
    src/allocator.c
    src/backend_offline.c
    src/base.c
    src/buffer.c
    src/context.c
    src/device.c
    src/follow.c
    src/layout.c
    src/notify.c
    src/offline.c
    src/period.c
    src/stream.c
    src/stream_open.c
    src/sample_convert.c
    src/lifecycle.c
    src/form.c
    src/duplex.c
    src/voice.c
    src/voice_dsp.c
    src/vad_core.c
    src/voice_detector.c
    src/gain_control.c
    src/clock.c
    src/thread.c)
maul_configure_library(maul-audio)
# The voice processors use the C math library, a library of its own on
# POSIX systems other than Darwin; static consumers need it too.
if(UNIX AND NOT APPLE)
    target_link_libraries(maul-audio PUBLIC m)
endif()
# clock_gettime under a strict C23 build, on POSIX systems other than
# Darwin, which shows it (and clock_gettime_nsec_np) without the request.
if(UNIX AND NOT APPLE)
    set_source_files_properties(src/clock.c PROPERTIES COMPILE_DEFINITIONS
        _POSIX_C_SOURCE=200809L)
endif()

# PipeWire's headers at build time only: the library opens libpipewire
# at run time and links nothing of it.
if(MAUL_AUDIO_PIPEWIRE)
    find_package(PkgConfig REQUIRED)
    pkg_check_modules(MAUL_AUDIO_PW REQUIRED libpipewire-0.3)
    target_sources(maul-audio PRIVATE src/backend_pipewire.c src/pipewire_api.c
        src/pipewire_card.c src/pipewire_stream.c)
    target_include_directories(maul-audio SYSTEM PRIVATE ${MAUL_AUDIO_PW_INCLUDE_DIRS})
    target_compile_definitions(maul-audio PRIVATE MAUD_HAVE_PIPEWIRE=1)
    # PipeWire's headers use POSIX and GNU declarations (ssize_t, locale_t)
    # that a strict C23 build hides.
    set_source_files_properties(src/backend_pipewire.c src/pipewire_api.c src/pipewire_card.c
        src/pipewire_stream.c PROPERTIES COMPILE_DEFINITIONS _GNU_SOURCE)
    # PipeWire's method macros compare a uint32_t version with an int
    # inside the code they expand to; the comparison is theirs.
    set_source_files_properties(src/backend_pipewire.c src/pipewire_card.c src/pipewire_stream.c
        PROPERTIES COMPILE_OPTIONS -Wno-sign-compare)
    target_link_libraries(maul-audio PRIVATE ${CMAKE_DL_LIBS})
endif()

# WASAPI, on Windows 10 and later, through the system's COM libraries.
if(MAUL_AUDIO_WASAPI)
    target_sources(maul-audio PRIVATE src/backend_wasapi.c src/wasapi_format.c
        src/wasapi_exclusive.c src/wasapi_notify.c src/wasapi_stream.c src/wasapi_voice.c
        src/worker.c)
    target_compile_definitions(maul-audio PRIVATE MAUD_HAVE_WASAPI=1 _WIN32_WINNT=0x0A00)
    # COM for the devices and clients, avrt for MMCSS on stream threads.
    target_link_libraries(maul-audio PRIVATE ole32 avrt)
endif()

# CoreAudio, on macOS, through the system's frameworks.
if(MAUL_AUDIO_COREAUDIO)
    target_sources(maul-audio PRIVATE src/backend_coreaudio.c src/coreaudio_form.c
        src/coreaudio_hog.c
        src/coreaudio_stream.c)
    target_compile_definitions(maul-audio PRIVATE MAUD_HAVE_COREAUDIO=1)
    target_link_libraries(maul-audio PRIVATE "-framework CoreAudio" "-framework AudioToolbox"
        "-framework CoreFoundation")
endif()

# Web Audio, in Emscripten builds: the backend's JavaScript is in its
# source, through EM_JS.
if(EMSCRIPTEN)
    target_sources(maul-audio PRIVATE src/backend_web.c src/web_capture.c src/web_devices.c)
endif()

# Likewise ALSA's headers only.
if(MAUL_AUDIO_ALSA)
    find_package(PkgConfig REQUIRED)
    pkg_check_modules(MAUL_AUDIO_ASOUND REQUIRED alsa)
    target_sources(maul-audio PRIVATE src/alsa_api.c src/alsa_chmap.c src/alsa_scan.c
        src/alsa_stream.c src/alsa_watch.c src/backend_alsa.c)
    target_include_directories(maul-audio SYSTEM PRIVATE ${MAUL_AUDIO_ASOUND_INCLUDE_DIRS})
    target_compile_definitions(maul-audio PRIVATE MAUD_HAVE_ALSA=1)
    set_source_files_properties(src/alsa_api.c src/alsa_chmap.c src/alsa_scan.c src/alsa_stream.c
        src/alsa_watch.c src/backend_alsa.c PROPERTIES COMPILE_DEFINITIONS _GNU_SOURCE)
    target_link_libraries(maul-audio PRIVATE ${CMAKE_DL_LIBS})
endif()

# Likewise libpulse's headers only.
if(MAUL_AUDIO_PULSE)
    find_package(PkgConfig REQUIRED)
    pkg_check_modules(MAUL_AUDIO_PA REQUIRED libpulse)
    target_sources(maul-audio PRIVATE src/backend_pulse.c src/pulse_api.c src/pulse_stream.c)
    target_include_directories(maul-audio SYSTEM PRIVATE ${MAUL_AUDIO_PA_INCLUDE_DIRS})
    target_compile_definitions(maul-audio PRIVATE MAUD_HAVE_PULSE=1)
    set_source_files_properties(src/backend_pulse.c src/pulse_api.c src/pulse_stream.c
        PROPERTIES COMPILE_DEFINITIONS _GNU_SOURCE)
    target_link_libraries(maul-audio PRIVATE ${CMAKE_DL_LIBS})
endif()

# Each running PulseAudio or ALSA stream has a library thread.
if(MAUL_AUDIO_PULSE OR MAUL_AUDIO_ALSA)
    target_sources(maul-audio PRIVATE src/worker.c)
    set_source_files_properties(src/worker.c PROPERTIES COMPILE_DEFINITIONS _GNU_SOURCE)
    find_package(Threads REQUIRED)
    target_link_libraries(maul-audio PRIVATE Threads::Threads)
endif()

if(MAUL_AUDIO_BUILD_TESTS)
    enable_testing()
    maul_add_test(base)
    maul_add_test(buffer)
    maul_add_test(layout)
    maul_add_test(context)
    maul_add_test(stream)
    maul_add_test(device)
    maul_add_test(utf8 WHITEBOX)
    maul_add_test(duplex)
    maul_add_test(voice_detector)
    maul_add_test(gain_control)
    maul_add_test(lifecycle)
    if(NOT MAUL_AUDIO_BUILD_SHARED)
        maul_add_test(sample_convert WHITEBOX)
    endif()
    # The stamp races a writer thread; Emscripten builds have none.
    if(NOT EMSCRIPTEN)
        maul_add_test(stamp WHITEBOX POSIX)
    endif()
    if(MAUL_AUDIO_PIPEWIRE)
        # The test's own client links libpipewire; the library does not.
        pkg_check_modules(MAUL_AUDIO_PW_LINK REQUIRED libpipewire-0.3)
        maul_add_test(pipewire THREADS)
        target_include_directories(test_pipewire SYSTEM PRIVATE ${MAUL_AUDIO_PW_INCLUDE_DIRS})
        target_compile_definitions(test_pipewire PRIVATE _GNU_SOURCE)
        target_compile_options(test_pipewire PRIVATE -Wno-sign-compare)
        target_link_libraries(test_pipewire PRIVATE ${MAUL_AUDIO_PW_LINK_LIBRARIES})
        maul_add_test(pipewire_restart THREADS)
        target_compile_definitions(test_pipewire_restart PRIVATE _GNU_SOURCE)
        if(NOT MAUL_AUDIO_BUILD_SHARED)
            maul_add_test(pipewire_route WHITEBOX)
            target_include_directories(test_pipewire_route SYSTEM PRIVATE
                ${MAUL_AUDIO_PW_INCLUDE_DIRS})
            target_compile_definitions(test_pipewire_route PRIVATE _GNU_SOURCE)
        endif()
        # Both use the one test daemon, and the second one restarts it.
        set_tests_properties(pipewire pipewire_restart PROPERTIES SKIP_RETURN_CODE 77
            RESOURCE_LOCK pipewire-daemon)
    endif()
    if(MAUL_AUDIO_ALSA)
        maul_add_test(alsa THREADS)
        target_compile_definitions(test_alsa PRIVATE _GNU_SOURCE)
        # Its streams play through the test daemon's pulse service.
        set_tests_properties(alsa PROPERTIES SKIP_RETURN_CODE 77 RESOURCE_LOCK pipewire-daemon)
        if(MAUL_AUDIO_TSAN)
            set_tests_properties(alsa PROPERTIES ENVIRONMENT
                "TSAN_OPTIONS=suppressions=${PROJECT_SOURCE_DIR}/test/tsan-alsa.supp")
        endif()
        if(NOT MAUL_AUDIO_BUILD_SHARED)
            maul_add_test(alsa_chmap WHITEBOX)
            target_include_directories(test_alsa_chmap SYSTEM PRIVATE
                ${MAUL_AUDIO_ASOUND_INCLUDE_DIRS})
            target_compile_definitions(test_alsa_chmap PRIVATE _GNU_SOURCE)
            maul_add_test(alsa_hotplug WHITEBOX)
            target_include_directories(test_alsa_hotplug SYSTEM PRIVATE
                ${MAUL_AUDIO_ASOUND_INCLUDE_DIRS})
            target_compile_definitions(test_alsa_hotplug PRIVATE _GNU_SOURCE)
            set_tests_properties(alsa_hotplug PROPERTIES SKIP_RETURN_CODE 77)
        endif()
    endif()
    if(MAUL_AUDIO_PULSE)
        maul_add_test(pulse)
        target_compile_definitions(test_pulse PRIVATE _GNU_SOURCE)
        # The test server is the PipeWire daemon's pulse service.
        set_tests_properties(pulse PROPERTIES SKIP_RETURN_CODE 77 RESOURCE_LOCK pipewire-daemon)
        if(MAUL_AUDIO_TSAN)
            set_tests_properties(pulse PROPERTIES ENVIRONMENT
                "TSAN_OPTIONS=suppressions=${PROJECT_SOURCE_DIR}/test/tsan-pulse.supp")
        endif()
    endif()
    if(MAUL_AUDIO_WASAPI AND NOT MAUL_AUDIO_BUILD_SHARED)
        maul_add_test(wasapi WHITEBOX)
        target_compile_definitions(test_wasapi PRIVATE _WIN32_WINNT=0x0A00)
        target_link_libraries(test_wasapi PRIVATE ole32)
        set_tests_properties(wasapi PROPERTIES SKIP_RETURN_CODE 77)
    endif()
    if(MAUL_AUDIO_COREAUDIO)
        maul_add_test(coreaudio THREADS)
        target_link_libraries(test_coreaudio PRIVATE "-framework CoreAudio"
            "-framework CoreFoundation")
        set_tests_properties(coreaudio PROPERTIES SKIP_RETURN_CODE 77)
    endif()
    # Speaker masks are plain integers, tested on every platform.
    if(NOT MAUL_AUDIO_BUILD_SHARED)
        maul_add_test(wasapi_format WHITEBOX)
        if(NOT MAUL_AUDIO_WASAPI)
            target_sources(test_wasapi_format PRIVATE src/wasapi_format.c)
        endif()
    endif()
    # The web backend's page, run in a browser by test/web/run_page.js,
    # not by CTest; its name keeps it out of the test_*.js runs.
    if(EMSCRIPTEN)
        add_executable(page_web test/web/page_web.c)
        target_link_libraries(page_web PRIVATE maul-audio)
        set_target_properties(page_web PROPERTIES SUFFIX ".html")
        maul_apply_flags(page_web)
        target_link_options(page_web PRIVATE "--shell-file=${PROJECT_SOURCE_DIR}/test/web/shell.html")
    endif()
    # The audio-thread trap exists only where invariants are checked.
    if(CMAKE_BUILD_TYPE STREQUAL "Debug" AND NOT MAUL_AUDIO_BUILD_SHARED)
        maul_add_test(trap WHITEBOX POSIX)
    endif()
    maul_add_header_tests()
endif()

if(MAUL_AUDIO_BUILD_BENCH AND EXISTS ${PROJECT_SOURCE_DIR}/bench/bench_main.c)
    add_subdirectory(bench)
endif()

if(MAUL_AUDIO_INSTALL)
    include(cmake/Install.cmake)
endif()
