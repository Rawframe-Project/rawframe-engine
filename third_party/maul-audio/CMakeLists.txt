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
option(MAUL_AUDIO_BUILD_SAMPLES "Build the samples" ${PROJECT_IS_TOP_LEVEL})
option(MAUL_AUDIO_BUILD_SHARED "Build a shared library instead of a static one" OFF)
option(MAUL_AUDIO_INSTALL "Generate install rules" ${PROJECT_IS_TOP_LEVEL})
option(MAUL_AUDIO_TEST_PACKAGE "Test the installed package by building samples/minimal against it" OFF)
option(MAUL_AUDIO_WERROR "Treat compiler warnings as errors" OFF)
option(MAUL_AUDIO_SANITIZE "Instrument with AddressSanitizer and UBSan" OFF)
option(MAUL_AUDIO_TSAN "Instrument with ThreadSanitizer" OFF)
option(MAUL_AUDIO_COVERAGE "Instrument for gcov coverage" OFF)
if(CMAKE_SYSTEM_NAME STREQUAL "Linux")
    set(MAUL_AUDIO_PIPEWIRE_DEFAULT ON)
else()
    set(MAUL_AUDIO_PIPEWIRE_DEFAULT OFF)
endif()
option(MAUL_AUDIO_FUZZ "Build the libFuzzer targets (Clang)" OFF)
# The library's two parts, each usable without the other: Device
# (streams, devices, capture, voice processing) and Spatial (HRTF sets,
# binaural rendering, the ambisonic bed, speaker panning). Neither's
# sources use the other's; a build with one alone links only if so.
option(MAUL_AUDIO_DEVICE "Build the Device part" ON)
option(MAUL_AUDIO_SPATIAL "Build the Spatial part" ON)
if(NOT MAUL_AUDIO_DEVICE AND NOT MAUL_AUDIO_SPATIAL)
    message(FATAL_ERROR "MAUL_AUDIO_DEVICE and MAUL_AUDIO_SPATIAL are both off: nothing to build")
endif()
option(MAUL_AUDIO_PIPEWIRE "Build the PipeWire backend, loaded at run time"
    ${MAUL_AUDIO_PIPEWIRE_DEFAULT})
option(MAUL_AUDIO_PULSE "Build the PulseAudio backend, loaded at run time"
    ${MAUL_AUDIO_PIPEWIRE_DEFAULT})
option(MAUL_AUDIO_ALSA "Build the ALSA backend, loaded at run time"
    ${MAUL_AUDIO_PIPEWIRE_DEFAULT})
option(MAUL_AUDIO_WASAPI "Build the WASAPI backend" ${WIN32})
# CoreAudio's HAL backend is macOS's; iOS has its own (no HAL devices).
if(CMAKE_SYSTEM_NAME STREQUAL "Darwin")
    set(MAUL_AUDIO_COREAUDIO_DEFAULT ON)
else()
    set(MAUL_AUDIO_COREAUDIO_DEFAULT OFF)
endif()
option(MAUL_AUDIO_COREAUDIO "Build the CoreAudio backend" ${MAUL_AUDIO_COREAUDIO_DEFAULT})
option(MAUL_AUDIO_AAUDIO "Build the AAudio backend" ${ANDROID})
if(CMAKE_SYSTEM_NAME STREQUAL "iOS")
    set(MAUL_AUDIO_IOS_DEFAULT ON)
else()
    set(MAUL_AUDIO_IOS_DEFAULT OFF)
endif()
option(MAUL_AUDIO_IOS "Build the iOS backend" ${MAUL_AUDIO_IOS_DEFAULT})
# A backend kept outside the library, as a console's is: a directory
# whose backend.cmake adds its sources to maul-audio
# (docs/user/private-backends.md).
set(MAUL_AUDIO_PRIVATE_BACKEND "" CACHE PATH
    "A directory outside the library holding a private backend's backend.cmake")
# Backends belong to the Device part.
if(NOT MAUL_AUDIO_DEVICE)
    foreach(backend PIPEWIRE PULSE ALSA WASAPI COREAUDIO AAUDIO IOS)
        set(MAUL_AUDIO_${backend} OFF)
    endforeach()
endif()

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
# What both parts use: results, allocators, buffers, channel layouts.
set(MAUL_AUDIO_BASE_SOURCES
    src/allocator.c
    src/base.c
    src/buffer.c
    src/fft.c
    src/fpenv.c
    src/layout.c
    src/real_fft.c)
set(MAUL_AUDIO_DEVICE_SOURCES
    src/backend_offline.c
    src/clock.c
    src/context.c
    src/device.c
    src/duplex.c
    src/focus.c
    src/follow.c
    src/echo_canceller.c
    src/echo_filter.c
    src/echo_suppressor.c
    src/form.c
    src/gain_control.c
    src/lifecycle.c
    src/noise_suppressor.c
    src/notify.c
    src/offline.c
    src/period.c
    src/sample_convert.c
    src/stream.c
    src/stream_open.c
    src/thread.c
    src/vad_core.c
    src/voice.c
    src/voice_detector.c
    src/voice_dsp.c)
set(MAUL_AUDIO_SPATIAL_SOURCES
    src/air_absorption.c
    src/ambisonic_rotation.c
    src/ambisonics.c
    src/bake_file.c
    src/band_eq.c
    src/bvh.c
    src/binaural.c
    src/binaural_decoder.c
    src/binaural_dsp.c
    src/crc32.c
    src/direct_effect.c
    src/direct_step.c
    src/file_fields.c
    src/host_tasks.c
    src/hrtf.c
    src/hrtf_file.c
    src/hrtf_lookup.c
    src/hrtf_resample.c
    src/least_squares.c
    src/magls.c
    src/near_field.c
    src/near_field_table.c
    src/occlusion.c
    src/octave_eq.c
    src/reverb.c
    src/partitioned.c
    src/path_refine.c
    src/path_search.c
    src/pathing.c
    src/portable_math.c
    src/probe_bake.c
    src/probe_graph.c
    src/probe_sets.c
    src/reflection_response.c
    src/reflections.c
    src/reverb_estimate.c
    src/scene.c
    src/scene_instances.c
    src/spatializer.c
    src/spatializer_paths.c
    src/speaker_decoder.c
    src/speaker_panner.c
    src/spherical_harmonics.c
    src/staged.c
    src/transmission.c
    src/watertight.c)
add_library(maul-audio ${MAUL_LIBRARY_KIND} ${MAUL_AUDIO_BASE_SOURCES})
if(MAUL_AUDIO_DEVICE)
    target_sources(maul-audio PRIVATE ${MAUL_AUDIO_DEVICE_SOURCES})
endif()
if(MAUL_AUDIO_SPATIAL)
    target_sources(maul-audio PRIVATE ${MAUL_AUDIO_SPATIAL_SOURCES})
endif()
maul_configure_library(maul-audio)
# With Emscripten's defaults a failed allocation aborts the module before
# the library sees it. Every program that links the library through CMake,
# in this tree or through the installed package, links so that malloc may
# fail and maud_errorCapacity reaches it; docs/user/platforms.md asks other
# builds to do the same.
if(EMSCRIPTEN)
    target_link_options(maul-audio INTERFACE -sABORTING_MALLOC=0)
endif()
# The voice processors use the C math library, a library of its own on
# POSIX systems other than Darwin; static consumers need it too.
if(UNIX AND NOT APPLE)
    target_link_libraries(maul-audio PUBLIC m)
endif()
# clock_gettime under a strict C23 build, on POSIX systems other than
# Darwin, which shows it (and clock_gettime_nsec_np) without the request.
if(MAUL_AUDIO_DEVICE AND UNIX AND NOT APPLE)
    set_source_files_properties(src/clock.c PROPERTIES COMPILE_DEFINITIONS
        _POSIX_C_SOURCE=200809L)
endif()

# PipeWire's headers at build time only: the library opens libpipewire
# at run time and links nothing of it.
if(MAUL_AUDIO_PIPEWIRE)
    find_package(PkgConfig REQUIRED)
    pkg_check_modules(MAUL_AUDIO_PW REQUIRED libpipewire-0.3)
    target_sources(maul-audio PRIVATE src/backend_pipewire.c src/pipewire_api.c
        src/pipewire_card.c src/pipewire_parse.c src/pipewire_stream.c)
    target_include_directories(maul-audio SYSTEM PRIVATE ${MAUL_AUDIO_PW_INCLUDE_DIRS})
    target_compile_definitions(maul-audio PRIVATE MAUD_HAVE_PIPEWIRE=1)
    # PipeWire's headers use POSIX and GNU declarations (ssize_t, locale_t)
    # that a strict C23 build hides.
    set_source_files_properties(src/backend_pipewire.c src/pipewire_api.c src/pipewire_card.c
        src/pipewire_parse.c src/pipewire_stream.c PROPERTIES COMPILE_DEFINITIONS _GNU_SOURCE)
    # PipeWire's method macros compare a uint32_t version with an int
    # inside the code they expand to; the comparison is theirs.
    set_source_files_properties(src/backend_pipewire.c src/pipewire_card.c src/pipewire_stream.c
        PROPERTIES COMPILE_OPTIONS -Wno-sign-compare)
    target_link_libraries(maul-audio PRIVATE ${CMAKE_DL_LIBS})
endif()

# WASAPI, on Windows 10 and later, through the system's COM libraries.
if(MAUL_AUDIO_WASAPI)
    target_sources(maul-audio PRIVATE src/backend_wasapi.c src/wasapi_format.c
        src/wasapi_exclusive.c src/wasapi_notify.c src/wasapi_apartment.c src/wasapi_objects.c src/wasapi_stream.c
        src/wasapi_voice.c
        src/worker.c)
    target_compile_definitions(maul-audio PRIVATE MAUD_HAVE_WASAPI=1 _WIN32_WINNT=0x0A00)
    # COM for the devices and clients, avrt for MMCSS on stream threads.
    target_link_libraries(maul-audio PRIVATE ole32 avrt)
endif()

# CoreAudio, on macOS, through the system's frameworks.
if(MAUL_AUDIO_COREAUDIO)
    target_sources(maul-audio PRIVATE src/apple_objects.c src/backend_coreaudio.c
        src/coreaudio_form.c src/coreaudio_hog.c src/coreaudio_listen.c src/coreaudio_stream.c)
    target_compile_definitions(maul-audio PRIVATE MAUD_HAVE_COREAUDIO=1)
    target_link_libraries(maul-audio PRIVATE "-framework CoreAudio" "-framework AudioToolbox"
        "-framework CoreFoundation")
endif()

if(MAUL_AUDIO_IOS)
    include(cmake/IOS.cmake)
endif()

# AAudio, on Android 11 (API 30) and later, through the NDK's library.
if(MAUL_AUDIO_AAUDIO)
    target_sources(maul-audio PRIVATE src/backend_aaudio.c src/aaudio_java.c src/aaudio_stream.c)
    target_compile_definitions(maul-audio PRIVATE MAUD_HAVE_AAUDIO=1)
    target_link_libraries(maul-audio PRIVATE aaudio ${CMAKE_DL_LIBS})
endif()

# Web Audio, in Emscripten builds: the backend's JavaScript is in its
# source, through EM_JS.
if(MAUL_AUDIO_DEVICE AND EMSCRIPTEN)
    target_sources(maul-audio PRIVATE src/backend_web.c src/web_capture.c src/web_clock.c
                                       src/web_devices.c)
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

# The private backend, built into the library from outside it.
if(MAUL_AUDIO_DEVICE AND NOT MAUL_AUDIO_PRIVATE_BACKEND STREQUAL "")
    if(NOT EXISTS "${MAUL_AUDIO_PRIVATE_BACKEND}/backend.cmake")
        message(FATAL_ERROR "MAUL_AUDIO_PRIVATE_BACKEND: no backend.cmake in "
                            "${MAUL_AUDIO_PRIVATE_BACKEND}")
    endif()
    include("${MAUL_AUDIO_PRIVATE_BACKEND}/backend.cmake")
    target_compile_definitions(maul-audio PRIVATE MAUD_HAVE_PRIVATE_BACKEND=1)
endif()

# Each running PulseAudio or ALSA stream has a library thread.
if(MAUL_AUDIO_PULSE OR MAUL_AUDIO_ALSA)
    target_sources(maul-audio PRIVATE src/worker.c)
    set_source_files_properties(src/worker.c PROPERTIES COMPILE_DEFINITIONS _GNU_SOURCE)
    find_package(Threads REQUIRED)
    target_link_libraries(maul-audio PRIVATE Threads::Threads)
endif()

# The data a test reads (data/): the source tree's, or on Android a copy
# on the device, which the android_data fixture pushes first.
if(ANDROID)
    set(MAUL_AUDIO_TEST_DATA /data/local/tmp/maul-audio/data)
else()
    set(MAUL_AUDIO_TEST_DATA ${PROJECT_SOURCE_DIR}/data)
endif()
function(maul_audio_test_data name)
    if(TARGET test_${name})
        target_compile_definitions(test_${name} PRIVATE MAUD_DATA_DIR="${MAUL_AUDIO_TEST_DATA}")
        if(ANDROID)
            set_tests_properties(${name} PROPERTIES FIXTURES_REQUIRED android-data)
        endif()
    endif()
endfunction()

if(MAUL_AUDIO_BUILD_TESTS)
    enable_testing()
    if(ANDROID)
        add_test(NAME android_data COMMAND ${PROJECT_SOURCE_DIR}/tools/run_android.sh
            --push ${PROJECT_SOURCE_DIR}/data /data/local/tmp/maul-audio)
        set_tests_properties(android_data PROPERTIES FIXTURES_SETUP android-data)
    endif()
    maul_add_test(base)
    maul_add_test(allocator WHITEBOX)
    maul_add_test(fpenv WHITEBOX)
    maul_add_exports_test()
    maul_add_test(buffer)
    maul_add_test(layout WHITEBOX)
    if(MAUL_AUDIO_SPATIAL)
        maul_add_test(ambisonics)
        maul_add_test(direct_effect)
        maul_add_test(spatializer)
        maul_add_test(spatializer_refusals)
        maul_add_test(occlusion)
        maul_add_test(scene)
        maul_add_test(scene_refusals)
        maul_add_test(reverb)
        maul_add_test(spatial_silence)
        maul_audio_test_data(spatial_silence)
        maul_add_test(effect_refusals)
        maul_add_test(transmission)
        # The two sides on two threads; Emscripten builds have none.
        if(NOT EMSCRIPTEN)
            maul_add_test(spatializer_threads POSIX)
        endif()
        # The host's float modes, the tasks on threads where there are any.
        if(EMSCRIPTEN)
            maul_add_test(float_env)
        else()
            maul_add_test(float_env THREADS)
        endif()
        maul_add_test(speaker_decoder)
        maul_add_test(speaker_panner)
    endif()
    if(MAUL_AUDIO_SPATIAL AND NOT MAUL_AUDIO_BUILD_SHARED)
        maul_add_test(band_eq WHITEBOX)
        maul_add_test(octave_eq WHITEBOX)
        maul_add_test(partitioned WHITEBOX)
        maul_add_test(staged WHITEBOX)
        maul_add_test(path_refine WHITEBOX)
        maul_add_test(path_search WHITEBOX)
        maul_add_test(bake_file WHITEBOX)
        maul_add_test(portable_math WHITEBOX)
        maul_add_test(probe_graph WHITEBOX)
        maul_add_test(real_fft WHITEBOX)
        maul_add_test(reflection_response WHITEBOX)
        maul_add_test(reverb_estimate WHITEBOX)
        maul_add_test(spatializer_reverb)
        maul_add_test(spatializer_reflections)
        maul_add_test(spatializer_probes)
        maul_add_test(spatializer_pathing)
        maul_add_test(spatializer_bake)
        maul_add_test(spatializer_call_back_in)
        maul_audio_test_data(spatializer_bake)
        maul_add_test(near_field WHITEBOX)
        maul_add_test(bvh WHITEBOX)
        maul_add_test(watertight WHITEBOX)
        maul_add_test(scene_instances)
        # Through the offline backend, so with the Device part too.
        if(MAUL_AUDIO_DEVICE)
            maul_add_test(golden)
        endif()
        if(TARGET test_golden)
            maul_audio_test_data(golden)
            # Node's own file system, so the web reads the references too.
            if(EMSCRIPTEN)
                target_link_options(test_golden PRIVATE -sNODERAWFS=1)
            endif()
        endif()
        foreach(area binaural binaural_decoder hrtf render_refusals)
            maul_add_test(${area} WHITEBOX)
            maul_audio_test_data(${area})
        endforeach()
        # test_hrtf builds a set of 176 MB in memory.
        if(EMSCRIPTEN AND TARGET test_hrtf)
            target_link_options(test_hrtf PRIVATE -sALLOW_MEMORY_GROWTH=1 -sMAXIMUM_MEMORY=4GB)
        endif()
    endif()
endif()

# The Device part's tests.
if(MAUL_AUDIO_BUILD_TESTS AND MAUL_AUDIO_DEVICE)
    maul_add_test(context)
    maul_add_test(focus WHITEBOX)
    maul_add_test(device_sync WHITEBOX)
    maul_add_test(follow WHITEBOX)
    maul_add_test(form WHITEBOX)
    maul_add_test(stream_open WHITEBOX)
    maul_add_test(period WHITEBOX)
    if(NOT MAUL_AUDIO_PRIVATE_BACKEND STREQUAL "")
        maul_add_test(private_backend)
    endif()
    # The worker thread, where a backend builds it.
    if(MAUL_AUDIO_WASAPI OR MAUL_AUDIO_PULSE OR MAUL_AUDIO_ALSA)
        maul_add_test(worker WHITEBOX)
    endif()
    maul_add_test(stream)
    # The backend tests' rate measure, on clock reads of its own.
    maul_add_test(clock)
    maul_add_test(loopback)
    # Outputs on failure across the parts: the device layer's calls, and
    # the spatial part's where it is built.
    if(MAUL_AUDIO_SPATIAL)
        maul_add_test(outputs)
    endif()
    maul_add_test(objects)
    maul_add_test(device)
    maul_add_test(utf8 WHITEBOX)
    maul_add_test(duplex)
    maul_add_test(voice_detector)
    maul_add_test(voice_dsp WHITEBOX)
    maul_add_test(gain_control)
    maul_add_test(noise_suppressor)
    maul_add_test(echo_canceller)
    maul_add_test(voice_silence)
    maul_add_test(voice_refusals)
    maul_add_test(device_refusals)
    # The C snippets of docs/user/, as written there: both parts'.
    if(MAUL_AUDIO_SPATIAL)
        maul_add_test(refusals)
        maul_add_test(versions)
        maul_audio_test_data(refusals)
        maul_add_test(guide)
        maul_audio_test_data(guide)
        # Node's own file system, so the web reads the HRTF set too.
        if(EMSCRIPTEN)
            target_link_options(test_guide PRIVATE -sNODERAWFS=1)
        endif()
    endif()
    # The snippets that open the native backend. Its stream plays on the
    # test daemon, which other tests plug, unplug and restart.
    maul_add_test(guide_device)
    set_tests_properties(guide_device PROPERTIES RESOURCE_LOCK pipewire-daemon)
    # Its stream may open ALSA's default PCM on a machine without sound,
    # where libasound leaks.
    if(MAUL_AUDIO_SANITIZE AND MAUL_AUDIO_ALSA)
        set_tests_properties(guide_device PROPERTIES ENVIRONMENT
            "LSAN_OPTIONS=suppressions=${PROJECT_SOURCE_DIR}/test/lsan-alsa.supp")
    endif()
    # A host stalled inside a step, under a native stream that latches the
    # spatializer's results: both parts', a device's, and not the web's,
    # whose callback runs on the stalled thread itself.
    if(MAUL_AUDIO_SPATIAL AND NOT EMSCRIPTEN)
        maul_add_test(stall)
        # Whether a block waited for the stalled host: quarantined, so it
        # runs at night, until the check has passed fourteen nights.
        add_test(NAME stall_blocks COMMAND test_stall blocks)
        set_tests_properties(stall_blocks PROPERTIES LABELS "stall;quarantine")
        set_tests_properties(stall stall_blocks PROPERTIES RESOURCE_LOCK pipewire-daemon)
        # Its stream may open ALSA's default PCM on a machine without
        # sound, where libasound leaks.
        if(MAUL_AUDIO_SANITIZE AND MAUL_AUDIO_ALSA)
            set_tests_properties(stall stall_blocks PROPERTIES ENVIRONMENT
                "LSAN_OPTIONS=suppressions=${PROJECT_SOURCE_DIR}/test/lsan-alsa.supp")
        endif()
    endif()
    maul_add_test(lifecycle)
    maul_add_test(call_back_in)
    if(NOT MAUL_AUDIO_BUILD_SHARED)
        maul_add_test(sample_convert WHITEBOX)
        maul_add_test(xrun WHITEBOX)
        maul_add_test(retry WHITEBOX)
    endif()
    # The stamp races a writer thread, and render_busy renders from a
    # second one; Emscripten builds have none.
    if(NOT EMSCRIPTEN)
        maul_add_test(stamp WHITEBOX POSIX)
        maul_add_test(render_busy POSIX)
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
            maul_add_test(pipewire_parse WHITEBOX)
            target_include_directories(test_pipewire_parse SYSTEM PRIVATE
                ${MAUL_AUDIO_PW_INCLUDE_DIRS})
            target_compile_definitions(test_pipewire_parse PRIVATE _GNU_SOURCE)
        endif()
        # The loopback's move to a silent sink, which heard the old sink now
        # and then on a loaded runner (#50): quarantined, so it runs at
        # night, until 14 nights in a row pass after the fix.
        add_test(NAME pipewire_move COMMAND test_pipewire move)
        set_tests_properties(pipewire_move PROPERTIES LABELS pipewire)
        maul_quarantine(pipewire_move)
        # The tests that change the default, whose context stopped following
        # it once on a loaded runner (#81): quarantined likewise.
        add_test(NAME pipewire_defaults COMMAND test_pipewire defaults)
        set_tests_properties(pipewire_defaults PROPERTIES LABELS pipewire)
        maul_quarantine(pipewire_defaults)
        # An output stream's rate check with every measure starved: the
        # suite must end inconclusive (RateSuiteResult, test/test_clock.h),
        # never green, and fail nothing.
        add_test(NAME pipewire_starved COMMAND test_pipewire rate)
        set_tests_properties(pipewire_starved PROPERTIES LABELS pipewire
            ENVIRONMENT MAUD_TEST_STARVE_RATE=1 PASS_REGULAR_EXPRESSION "inconclusive: .* with stalls; "
            FAIL_REGULAR_EXPRESSION "FAIL: ")
        # All five use the one test daemon, and pipewire_restart restarts it.
        set_tests_properties(pipewire pipewire_move pipewire_defaults pipewire_restart
            pipewire_starved PROPERTIES RESOURCE_LOCK pipewire-daemon)
    endif()
    if(MAUL_AUDIO_ALSA)
        maul_add_test(alsa THREADS)
        target_compile_definitions(test_alsa PRIVATE _GNU_SOURCE)
        # Its streams play through the test daemon's pulse service.
        set_tests_properties(alsa PROPERTIES RESOURCE_LOCK pipewire-daemon)
        if(MAUL_AUDIO_TSAN)
            set_tests_properties(alsa PROPERTIES ENVIRONMENT
                "TSAN_OPTIONS=suppressions=${PROJECT_SOURCE_DIR}/test/tsan-alsa.supp")
        endif()
        if(NOT MAUL_AUDIO_BUILD_SHARED)
            maul_add_test(alsa_chmap WHITEBOX)
            target_include_directories(test_alsa_chmap SYSTEM PRIVATE
                ${MAUL_AUDIO_ASOUND_INCLUDE_DIRS})
            target_compile_definitions(test_alsa_chmap PRIVATE _GNU_SOURCE)
            maul_add_test(alsa_recover WHITEBOX)
            target_include_directories(test_alsa_recover SYSTEM PRIVATE
                ${MAUL_AUDIO_ASOUND_INCLUDE_DIRS})
            target_compile_definitions(test_alsa_recover PRIVATE _GNU_SOURCE)
            maul_add_test(alsa_hotplug WHITEBOX)
            target_include_directories(test_alsa_hotplug SYSTEM PRIVATE
                ${MAUL_AUDIO_ASOUND_INCLUDE_DIRS})
            target_compile_definitions(test_alsa_hotplug PRIVATE _GNU_SOURCE)
            # hygiene: exit 77 means libasound is absent, never a failure
            set_tests_properties(alsa_hotplug PROPERTIES SKIP_RETURN_CODE 77)
        endif()
    endif()
    if(MAUL_AUDIO_PULSE)
        maul_add_test(pulse)
        target_compile_definitions(test_pulse PRIVATE _GNU_SOURCE)
        # The output stream's rate, which reads past its tolerance now and
        # then on a loaded runner (#73): quarantined, so it runs at night.
        add_test(NAME pulse_rate COMMAND test_pulse rate)
        set_tests_properties(pulse_rate PROPERTIES LABELS pulse)
        maul_quarantine(pulse_rate)
        # The test server is the PipeWire daemon's pulse service.
        set_tests_properties(pulse pulse_rate PROPERTIES RESOURCE_LOCK pipewire-daemon)
        if(MAUL_AUDIO_TSAN)
            set_tests_properties(pulse pulse_rate PROPERTIES ENVIRONMENT
                "TSAN_OPTIONS=suppressions=${PROJECT_SOURCE_DIR}/test/tsan-pulse.supp")
        endif()
    endif()
    # The loaders of the Linux sound libraries, against files by their
    # names that are no libraries and stubs under their sonames.
    if((MAUL_AUDIO_ALSA OR MAUL_AUDIO_PIPEWIRE OR MAUL_AUDIO_PULSE) AND NOT MAUL_AUDIO_BUILD_SHARED)
        maul_add_test(loaders WHITEBOX)
        target_compile_definitions(test_loaders PRIVATE _GNU_SOURCE)
        target_link_libraries(test_loaders PRIVATE ${CMAKE_DL_LIBS})
        set(MAUL_AUDIO_NOT_LIBRARIES ${CMAKE_CURRENT_BINARY_DIR}/not-libraries)
        foreach(entry "ALSA;asound;libasound.so.2;ASOUND" "PIPEWIRE;pipewire;libpipewire-0.3.so.0;PW"
                "PULSE;pulse;libpulse.so.0;PA")
            list(GET entry 0 part)
            list(GET entry 1 stem)
            list(GET entry 2 soname)
            list(GET entry 3 headers)
            if(MAUL_AUDIO_${part})
                file(WRITE ${MAUL_AUDIO_NOT_LIBRARIES}/${soname} "not a library\n")
                add_library(maud_stub_${stem} SHARED test/loader_stub.c)
                set_target_properties(maud_stub_${stem} PROPERTIES NO_SONAME ON)
                target_link_options(maud_stub_${stem} PRIVATE -Wl,-soname,${soname})
                target_include_directories(test_loaders SYSTEM PRIVATE
                    ${MAUL_AUDIO_${headers}_INCLUDE_DIRS})
                target_compile_definitions(test_loaders PRIVATE MAUD_TEST_${part}=1
                    MAUD_STUB_${part}="$<TARGET_FILE:maud_stub_${stem}>")
                add_dependencies(test_loaders maud_stub_${stem})
            endif()
        endforeach()
        set_tests_properties(loaders PROPERTIES ENVIRONMENT
            "LD_LIBRARY_PATH=${MAUL_AUDIO_NOT_LIBRARIES}")
    endif()
    if(MAUL_AUDIO_WASAPI AND NOT MAUL_AUDIO_BUILD_SHARED)
        maul_add_test(wasapi WHITEBOX)
        target_compile_definitions(test_wasapi PRIVATE _WIN32_WINNT=0x0A00)
        target_link_libraries(test_wasapi PRIVATE ole32)
        # Both runs move streams between the system's endpoints and make
        # them the default, so they never run at once.
        set_tests_properties(wasapi PROPERTIES RESOURCE_LOCK wasapi-endpoints)
        # The same with the host's thread in a single-threaded apartment.
        add_test(NAME wasapi_sta COMMAND test_wasapi)
        set_tests_properties(wasapi_sta PROPERTIES ENVIRONMENT MAUD_WASAPI_STA=1
            RESOURCE_LOCK wasapi-endpoints)
        if(NOT MAUL_AUDIO_BUILD_SHARED)
            maul_add_test(wasapi_voice WHITEBOX)
            target_link_libraries(test_wasapi_voice PRIVATE ole32)
        endif()
    endif()
    # The iOS backend in the simulator, in Objective-C to read the
    # session's category.
    if(MAUL_AUDIO_IOS)
        add_executable(test_ios test/test_ios.m)
        # White box: it also reads the session's port forms.
        target_include_directories(test_ios PRIVATE ${PROJECT_SOURCE_DIR}/src)
        target_link_libraries(test_ios PRIVATE maul-audio "-framework AVFAudio"
            "-framework Foundation")
        maul_apply_flags(test_ios)
        set_target_properties(test_ios PROPERTIES OBJC_STANDARD 23 OBJC_STANDARD_REQUIRED ON
            OBJC_EXTENSIONS OFF)
        target_compile_options(test_ios PRIVATE $<$<COMPILE_LANGUAGE:OBJC>:-fno-objc-arc>)
        add_test(NAME ios COMMAND test_ios)
        # Capture, as an application the runner grants the microphone
        # (tools/run_ios_app.sh).
        add_executable(test_ios_capture MACOSX_BUNDLE test/test_ios_capture.m)
        target_link_libraries(test_ios_capture PRIVATE maul-audio "-framework AudioToolbox"
            "-framework AVFAudio" "-framework Foundation" "-framework UIKit")
        maul_apply_flags(test_ios_capture)
        set_target_properties(test_ios_capture PROPERTIES OBJC_STANDARD 23
            OBJC_STANDARD_REQUIRED ON OBJC_EXTENSIONS OFF
            MACOSX_BUNDLE_INFO_PLIST ${PROJECT_SOURCE_DIR}/test/ios/Info.plist.in
            MACOSX_BUNDLE_GUI_IDENTIFIER org.maul.audio.test.ios-capture
            MACOSX_BUNDLE_BUNDLE_NAME test_ios_capture
            MACOSX_BUNDLE_BUNDLE_VERSION ${PROJECT_VERSION}
            MACOSX_BUNDLE_SHORT_VERSION_STRING ${PROJECT_VERSION})
        target_compile_options(test_ios_capture PRIVATE -fno-objc-arc)
        add_test(NAME ios_capture COMMAND ${PROJECT_SOURCE_DIR}/tools/run_ios_app.sh
                 $<TARGET_BUNDLE_DIR:test_ios_capture>)
        # The runner bounds the install and launch (300 s) and the run (120
        # s) apart, each failing with what it waited for; CTest's timeout
        # only backs them up.
        set_tests_properties(ios_capture PROPERTIES TIMEOUT 480 RESOURCE_LOCK simulator)
        set_tests_properties(ios PROPERTIES RESOURCE_LOCK simulator)
    endif()
    if(MAUL_AUDIO_AAUDIO)
        maul_add_test(aaudio THREADS)
    endif()
    # The Java half, as an application in the emulator, built by
    # tools/build_android_app.sh without Gradle and run by
    # tools/run_android_app.sh.
    if(MAUL_AUDIO_AAUDIO AND DEFINED ENV{ANDROID_HOME})
        set(package maul.audio.tests.android_java)
        add_library(test_android_java SHARED test/test_android_java.c)
        target_link_libraries(test_android_java PRIVATE maul-audio android)
        target_compile_definitions(test_android_java PRIVATE MAUD_TEST_PACKAGE="${package}")
        maul_apply_flags(test_android_java)
        file(GLOB_RECURSE MAUL_AUDIO_JAVA_SOURCES ${PROJECT_SOURCE_DIR}/java/*.java)
        add_custom_command(OUTPUT ${PROJECT_BINARY_DIR}/test_android_java.apk
            COMMAND ${CMAKE_COMMAND} -E env ANDROID_HOME=$ENV{ANDROID_HOME}
                    ${PROJECT_SOURCE_DIR}/tools/build_android_app.sh
                    ${PROJECT_BINARY_DIR}/test_android_java.apk
                    $<TARGET_FILE:test_android_java> ${package} ${ANDROID_ABI}
            DEPENDS test_android_java ${MAUL_AUDIO_JAVA_SOURCES}
                    ${PROJECT_SOURCE_DIR}/java/proguard-rules.pro
                    ${PROJECT_SOURCE_DIR}/test/android/AndroidManifest.xml
                    ${PROJECT_SOURCE_DIR}/tools/build_android_app.sh
            VERBATIM)
        add_custom_target(test_android_java_apk ALL
            DEPENDS ${PROJECT_BINARY_DIR}/test_android_java.apk)
        add_test(NAME android_java COMMAND ${PROJECT_SOURCE_DIR}/tools/run_android_app.sh
                 ${PROJECT_BINARY_DIR}/test_android_java.apk ${package})
        set_tests_properties(android_java PROPERTIES TIMEOUT 240)
    endif()
    if(MAUL_AUDIO_COREAUDIO)
        maul_add_test(coreaudio THREADS)
        target_link_libraries(test_coreaudio PRIVATE "-framework CoreAudio"
            "-framework CoreFoundation")
        # Forms and data source listeners, against a fake HAL.
        maul_add_test(coreaudio_form WHITEBOX)
        target_link_libraries(test_coreaudio_form PRIVATE "-framework CoreAudio"
            "-framework CoreFoundation")
        # The system listeners and a restart of the audio server, against
        # a fake HAL.
        maul_add_test(coreaudio_listen WHITEBOX)
        target_link_libraries(test_coreaudio_listen PRIVATE "-framework CoreAudio"
            "-framework CoreFoundation")
    endif()
    # Speaker masks are plain integers, tested on every platform.
    if(NOT MAUL_AUDIO_BUILD_SHARED)
        maul_add_test(wasapi_format WHITEBOX)
        if(NOT MAUL_AUDIO_WASAPI)
            target_sources(test_wasapi_format PRIVATE src/wasapi_format.c)
        endif()
    endif()
    # The web backend's transport and capture on a simulated Web Audio,
    # under Node.
    if(EMSCRIPTEN)
        maul_add_test(web_transport)
        maul_add_test(web_capture)
    endif()
    # The web backend's page, run in a browser by test/web/run_page.js,
    # not by CTest; its name keeps it out of the test_*.js runs.
    if(EMSCRIPTEN)
        add_executable(page_web test/web/page_web.c)
        target_link_libraries(page_web PRIVATE maul-audio)
        set_target_properties(page_web PROPERTIES SUFFIX ".html")
        maul_apply_flags(page_web)
        target_link_options(page_web PRIVATE "--shell-file=${PROJECT_SOURCE_DIR}/test/web/shell.html")
        add_executable(page_display test/web/page_display.c)
        target_link_libraries(page_display PRIVATE maul-audio)
        set_target_properties(page_display PROPERTIES SUFFIX ".html")
        maul_apply_flags(page_display)
        target_link_options(page_display PRIVATE "--shell-file=${PROJECT_SOURCE_DIR}/test/web/shell.html")
        add_executable(page_sink test/web/page_sink.c)
        target_link_libraries(page_sink PRIVATE maul-audio)
        set_target_properties(page_sink PROPERTIES SUFFIX ".html")
        maul_apply_flags(page_sink)
        target_link_options(page_sink PRIVATE "--shell-file=${PROJECT_SOURCE_DIR}/test/web/shell.html")
        add_executable(page_ended test/web/page_ended.c)
        target_link_libraries(page_ended PRIVATE maul-audio)
        set_target_properties(page_ended PROPERTIES SUFFIX ".html")
        maul_apply_flags(page_ended)
        target_link_options(page_ended PRIVATE "--shell-file=${PROJECT_SOURCE_DIR}/test/web/shell.html")
        add_executable(page_default test/web/page_default.c)
        target_link_libraries(page_default PRIVATE maul-audio)
        set_target_properties(page_default PROPERTIES SUFFIX ".html")
        maul_apply_flags(page_default)
        target_link_options(page_default PRIVATE "--shell-file=${PROJECT_SOURCE_DIR}/test/web/shell.html")
        add_executable(page_clock test/web/page_clock.c)
        target_link_libraries(page_clock PRIVATE maul-audio)
        set_target_properties(page_clock PROPERTIES SUFFIX ".html")
        maul_apply_flags(page_clock)
        target_link_options(page_clock PRIVATE "--shell-file=${PROJECT_SOURCE_DIR}/test/web/shell.html")
        add_executable(page_loopback test/web/page_loopback.c)
        target_link_libraries(page_loopback PRIVATE maul-audio)
        set_target_properties(page_loopback PROPERTIES SUFFIX ".html")
        maul_apply_flags(page_loopback)
        target_link_options(page_loopback PRIVATE
                            "--shell-file=${PROJECT_SOURCE_DIR}/test/web/shell.html")
    endif()
    # The audio-thread trap exists only where invariants are checked. In
    # WebAssembly a trap ends the module, with no signal to catch.
    if(CMAKE_BUILD_TYPE STREQUAL "Debug" AND NOT MAUL_AUDIO_BUILD_SHARED AND NOT EMSCRIPTEN)
        maul_add_test(trap WHITEBOX POSIX)
    endif()
endif()

# The browser pages, in headless Chrome through puppeteer (MAUD_PUPPETEER
# names its module; test/web/package.json pins it): each on a cross-origin
# isolated page and on a plain one. Three are quarantined, run every night
# instead of in the pull request check, and leave quarantine after 14
# nightly runs in a row pass: page_clock (#3) and page_loopback (#4),
# which measure timing and capture in a real browser, and page_web_plain
# (#5), whose xrun counts follow how busy the main thread is.
if(MAUL_AUDIO_BUILD_TESTS AND MAUL_AUDIO_DEVICE AND EMSCRIPTEN)
    find_program(MAUL_AUDIO_NODE node REQUIRED)
    function(maul_audio_page name page)
        add_test(NAME ${name} COMMAND ${MAUL_AUDIO_NODE} ${PROJECT_SOURCE_DIR}/test/web/run_page.js
                 ${PROJECT_BINARY_DIR} ${page}.html ${ARGN})
        # A page measures timing and xruns in a real browser, so it runs
        # alone, never beside the other suites.
        # hygiene: exit 77 means the page has no browser or sound server here
        set_tests_properties(${name} PROPERTIES LABELS "integration;${page}" SKIP_RETURN_CODE 77
                             TIMEOUT 120 RUN_SERIAL TRUE)
    endfunction()
    maul_audio_page(page_web_isolated page_web 1)
    maul_audio_page(page_web_plain page_web 0)
    maul_quarantine(page_web_plain)
    maul_audio_page(page_web_denied page_web 0 deny)
    foreach(isolated 1 0)
        if(isolated)
            set(kind isolated)
        else()
            set(kind plain)
        endif()
        foreach(page page_ended page_default page_sink page_display)
            maul_audio_page(${page}_${kind} ${page} ${isolated})
        endforeach()
        maul_audio_page(page_loopback_${kind} page_loopback ${isolated} loopback)
        maul_audio_page(page_clock_${kind} page_clock ${isolated})
        maul_quarantine(page_loopback_${kind} page_clock_${kind})
    endforeach()
endif()

# The suites that drive a real platform: a sound server, the platform's
# own audio service, a simulator, an emulator or a browser. They may read
# the clock, and the changed-files check (tools/check.sh) runs one only
# when a change reaches it by name.
if(MAUL_AUDIO_BUILD_TESTS)
    foreach(name pipewire pipewire_move pipewire_defaults pipewire_restart pipewire_starved pulse
                 pulse_rate alsa stall stall_blocks guide_device coreaudio wasapi wasapi_sta ios
                 ios_capture
                 aaudio android_java)
        if(TEST ${name})
            set_property(TEST ${name} APPEND PROPERTY LABELS integration)
        endif()
    endforeach()
    # The suites that skip where their platform is absent
    # (PlatformAbsent, test/test_wait.h).
    foreach(name pipewire pipewire_move pipewire_defaults pipewire_restart pipewire_starved pulse
                 pulse_rate alsa stall stall_blocks guide_device coreaudio wasapi wasapi_sta
                 aaudio)
        if(TEST ${name})
            # hygiene: exit 77 means the suite's platform is absent, never a failure
            set_tests_properties(${name} PROPERTIES SKIP_RETURN_CODE 77)
        endif()
    endforeach()
    # The suites whose rate measures can be starved (RateSuiteResult,
    # test/test_clock.h): CI fails the skip as a test that did not run.
    foreach(name pipewire pipewire_move pipewire_defaults pulse pulse_rate alsa)
        if(TEST ${name})
            # hygiene: an inconclusive line means the rate measures judged nothing
            set_property(TEST ${name} PROPERTY SKIP_REGULAR_EXPRESSION "inconclusive: ")
        endif()
    endforeach()
endif()

# On Linux no test reaches the session's own sound server. The suites that
# need a server run with the runtime directory where the sound-server
# fixture starts a private PipeWire daemon (tools/pipewire_daemon.sh);
# every other test runs with an empty one of its own, so no client touches
# the daemon's directory while it starts. Where the daemon's programs are installed
# the fixture starts it and those suites must reach it; where they are
# not, the environment is absent and those suites skip. The browser pages
# of a web build on a Linux host need it too: Chrome plays into its null
# sink, a device that is always there, so a page never meets a runner's
# missing or changing output.
if(MAUL_AUDIO_BUILD_TESTS AND CMAKE_HOST_SYSTEM_NAME STREQUAL "Linux"
   AND (EMSCRIPTEN OR (CMAKE_SYSTEM_NAME STREQUAL "Linux" AND NOT ANDROID)))
    set(maul_audio_server_dir ${PROJECT_BINARY_DIR}/sound-server)
    get_property(maul_audio_tests DIRECTORY PROPERTY TESTS)
    set(maul_audio_server_tests "")
    foreach(name pipewire pipewire_move pipewire_defaults pipewire_restart pipewire_starved pulse
                 pulse_rate alsa stall stall_blocks guide_device)
        if(TEST ${name})
            list(APPEND maul_audio_server_tests ${name})
        endif()
    endforeach()
    set(maul_audio_pages ${maul_audio_tests})
    list(FILTER maul_audio_pages INCLUDE REGEX "^page_")
    foreach(name ${maul_audio_tests})
        if(name IN_LIST maul_audio_server_tests OR name IN_LIST maul_audio_pages)
            set(runtime ${maul_audio_server_dir}/run)
        else()
            set(runtime ${maul_audio_server_dir}/none)
        endif()
        set_property(TEST ${name} APPEND PROPERTY ENVIRONMENT XDG_RUNTIME_DIR=${runtime}
                     PULSE_SERVER=unix:${runtime}/pulse/native)
    endforeach()
    set(maul_audio_server_found TRUE)
    foreach(program pipewire pipewire-pulse wireplumber pw-cli pw-metadata pactl dbus-daemon)
        string(MAKE_C_IDENTIFIER ${program} variable)
        find_program(MAUL_AUDIO_HAVE_${variable} ${program})
        if(NOT MAUL_AUDIO_HAVE_${variable})
            set(maul_audio_server_found FALSE)
        endif()
    endforeach()
    file(MAKE_DIRECTORY ${maul_audio_server_dir}/run ${maul_audio_server_dir}/none)
    if(maul_audio_pages AND NOT maul_audio_server_found)
        set_property(TEST ${maul_audio_pages} APPEND PROPERTY ENVIRONMENT MAUD_NO_SOUND_SERVER=1)
    endif()
    if(maul_audio_server_found AND (maul_audio_server_tests OR maul_audio_pages))
        set(daemon ${PROJECT_SOURCE_DIR}/tools/pipewire_daemon.sh)
        add_test(NAME sound_server_start COMMAND ${daemon} start ${maul_audio_server_dir})
        add_test(NAME sound_server_stop COMMAND ${daemon} stop ${maul_audio_server_dir})
        set_tests_properties(sound_server_start PROPERTIES FIXTURES_SETUP sound-server
                             LABELS integration RESOURCE_LOCK pipewire-daemon)
        set_tests_properties(sound_server_stop PROPERTIES FIXTURES_CLEANUP sound-server
                             LABELS integration RESOURCE_LOCK pipewire-daemon)
        set_tests_properties(${maul_audio_server_tests} ${maul_audio_pages} PROPERTIES
                             FIXTURES_REQUIRED sound-server)
        # ALSA's default device reaches the server through ALSA's
        # PulseAudio plugin; without the plugin there is no ALSA server. CI
        # (GitHub sets CI) runs in an image that has it, so there the suite
        # requires it and fails if the plugin goes missing.
        set(require_alsa "")
        file(GLOB maul_audio_alsa_pulse /usr/share/alsa/alsa.conf.d/*pulse*.conf)
        if(maul_audio_alsa_pulse OR "$ENV{CI}" STREQUAL "true")
            set(require_alsa MAUD_REQUIRE_ALSA=1)
        endif()
        # The suites that stop, start or signal the daemon run its script,
        # compiled in (test/test_daemon.h), never a command from the
        # environment.
        foreach(name ${maul_audio_server_tests})
            set_property(TEST ${name} APPEND PROPERTY ENVIRONMENT
                         MAUD_REQUIRE_PIPEWIRE=1 MAUD_REQUIRE_PULSE=1 ${require_alsa})
            if(TARGET test_${name})
                target_compile_definitions(test_${name} PRIVATE MAUD_TEST_DAEMON="${daemon}"
                                           MAUD_TEST_SERVER_DIR="${maul_audio_server_dir}")
            endif()
        endforeach()
    endif()
endif()

# The installed package, consumed by samples/minimal through CMake and
# pkg-config (cmake/PackageTest.cmake).
if(MAUL_AUDIO_BUILD_TESTS AND MAUL_AUDIO_TEST_PACKAGE AND MAUL_AUDIO_INSTALL)
    add_test(NAME package
             COMMAND ${CMAKE_COMMAND} -DBINARY_DIR=${PROJECT_BINARY_DIR} -DSOURCE_DIR=${PROJECT_SOURCE_DIR}
                     -DGENERATOR=${CMAKE_GENERATOR} -DC_COMPILER=${CMAKE_C_COMPILER} -DCONFIG=$<CONFIG>
                     -DSHARED=${MAUL_AUDIO_BUILD_SHARED} -P ${PROJECT_SOURCE_DIR}/cmake/PackageTest.cmake)
    set_tests_properties(package PROPERTIES LABELS package)
endif()

# Every public header compiles alone, whichever parts are built.
if(MAUL_AUDIO_BUILD_TESTS)
    maul_add_header_tests()
endif()

# The fuzz targets: test/fuzz_<area>.c, run as fuzz_<area> <corpus dir>,
# over the bytes programs load and hand the library. Their corpora are in
# test/corpus/fuzz_<area>/; the shipped HRTF file in data/hrtf/, behind a
# 0x01 byte, seeds fuzz_hrtf too, and the shipped bakes in data/bake/,
# behind a 0x03 byte (fields of order 1), fuzz_bake.
if(MAUL_AUDIO_FUZZ AND MAUL_AUDIO_SPATIAL)
    # The library is instrumented too: its coverage guides libFuzzer, and
    # the sanitizers watch its code, not only the targets'. Undefined
    # behavior stops the run, as a crash does: by default UBSan only
    # prints and libFuzzer goes on, leaving no input behind.
    target_compile_options(maul-audio PRIVATE -fsanitize=fuzzer-no-link,address,undefined
                           -fno-sanitize-recover=all)
    target_link_options(maul-audio PUBLIC -fsanitize=address,undefined)
    foreach(area hrtf bake)
        add_executable(fuzz_${area} test/fuzz_${area}.c)
        target_link_libraries(fuzz_${area} PRIVATE maul-audio)
        # The targets reach the library's own functions.
        target_include_directories(fuzz_${area} PRIVATE ${PROJECT_SOURCE_DIR}/src)
        maul_apply_flags(fuzz_${area})
        target_compile_options(fuzz_${area} PRIVATE -fsanitize=fuzzer,address,undefined
                               -fno-sanitize-recover=all)
        target_link_options(fuzz_${area} PRIVATE -fsanitize=fuzzer,address,undefined)
    endforeach()
endif()

if(MAUL_AUDIO_BUILD_BENCH AND EXISTS ${PROJECT_SOURCE_DIR}/bench/bench_main.c)
    add_subdirectory(bench)
endif()

if(MAUL_AUDIO_BUILD_SAMPLES)
    add_subdirectory(samples)
endif()

if(MAUL_AUDIO_INSTALL)
    # The public headers of the parts configured.
    set(MAUL_PUBLIC_HEADERS base.h buffer.h layout.h)
    if(MAUL_AUDIO_DEVICE)
        list(APPEND MAUL_PUBLIC_HEADERS context.h device.h focus.h notification.h objects.h
             offline.h stream.h voice.h)
    endif()
    if(MAUL_AUDIO_SPATIAL)
        list(APPEND MAUL_PUBLIC_HEADERS ambisonics.h binaural.h direct.h hrtf.h reverb.h scene.h
             spatializer.h speakers.h)
    endif()
    # With both parts every header must be listed, or it would not install.
    if(MAUL_AUDIO_DEVICE AND MAUL_AUDIO_SPATIAL)
        file(GLOB headers RELATIVE ${PROJECT_SOURCE_DIR}/include/maul-audio
             ${PROJECT_SOURCE_DIR}/include/maul-audio/*.h)
        foreach(header IN LISTS headers)
            if(NOT header IN_LIST MAUL_PUBLIC_HEADERS)
                message(FATAL_ERROR "include/maul-audio/${header} is in no part's header list")
            endif()
        endforeach()
    endif()
    # What a consumer of the static library links besides it: the
    # packages the config file finds, and for pkg-config the system
    # libraries, as the backends above link them.
    set(MAUL_PKG_LIBS_PRIVATE "")
    if(UNIX AND NOT APPLE)
        string(APPEND MAUL_PKG_LIBS_PRIVATE " -lm")
    endif()
    if(MAUL_AUDIO_PIPEWIRE OR MAUL_AUDIO_PULSE OR MAUL_AUDIO_ALSA OR MAUL_AUDIO_AAUDIO)
        foreach(library IN LISTS CMAKE_DL_LIBS)
            string(APPEND MAUL_PKG_LIBS_PRIVATE " -l${library}")
        endforeach()
    endif()
    if(MAUL_AUDIO_PULSE OR MAUL_AUDIO_ALSA)
        set(MAUL_PACKAGE_DEPENDENCIES "find_dependency(Threads)")
        string(APPEND MAUL_PKG_LIBS_PRIVATE " ${CMAKE_THREAD_LIBS_INIT}")
    endif()
    if(MAUL_AUDIO_WASAPI)
        string(APPEND MAUL_PKG_LIBS_PRIVATE " -lole32 -lavrt")
    endif()
    if(MAUL_AUDIO_COREAUDIO)
        string(APPEND MAUL_PKG_LIBS_PRIVATE
            " -framework CoreAudio -framework AudioToolbox -framework CoreFoundation")
    endif()
    if(MAUL_AUDIO_IOS)
        string(APPEND MAUL_PKG_LIBS_PRIVATE
            " -framework AudioToolbox -framework AVFAudio -framework Foundation")
    endif()
    if(MAUL_AUDIO_AAUDIO)
        string(APPEND MAUL_PKG_LIBS_PRIVATE " -laaudio")
    endif()
    string(STRIP "${MAUL_PKG_LIBS_PRIVATE}" MAUL_PKG_LIBS_PRIVATE)
    include(cmake/Install.cmake)
endif()
