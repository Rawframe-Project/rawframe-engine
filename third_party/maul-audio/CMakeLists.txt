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
# (docs/private-backends.md).
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
        src/coreaudio_form.c src/coreaudio_hog.c src/coreaudio_stream.c)
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
    maul_add_test(buffer)
    maul_add_test(layout WHITEBOX)
    if(MAUL_AUDIO_SPATIAL)
        maul_add_test(ambisonics)
        maul_add_test(direct_effect)
        maul_add_test(spatializer)
        maul_add_test(occlusion)
        maul_add_test(scene)
        maul_add_test(reverb)
        maul_add_test(transmission)
        # The two sides on two threads; Emscripten builds have none.
        if(NOT EMSCRIPTEN)
            maul_add_test(spatializer_threads POSIX)
        endif()
        maul_add_test(speaker_decoder)
        maul_add_test(speaker_panner)
    endif()
    if(MAUL_AUDIO_SPATIAL AND NOT MAUL_AUDIO_BUILD_SHARED)
        maul_add_test(band_eq WHITEBOX)
        maul_add_test(octave_eq WHITEBOX)
        maul_add_test(partitioned WHITEBOX)
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
        foreach(area binaural binaural_decoder hrtf)
            maul_add_test(${area} WHITEBOX)
            maul_audio_test_data(${area})
        endforeach()
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
    if(NOT MAUL_AUDIO_PRIVATE_BACKEND STREQUAL "")
        maul_add_test(private_backend)
    endif()
    # The worker thread, where a backend builds it.
    if(MAUL_AUDIO_WASAPI OR MAUL_AUDIO_PULSE OR MAUL_AUDIO_ALSA)
        maul_add_test(worker WHITEBOX)
    endif()
    maul_add_test(stream)
    maul_add_test(objects)
    maul_add_test(device)
    maul_add_test(utf8 WHITEBOX)
    maul_add_test(duplex)
    maul_add_test(voice_detector)
    maul_add_test(voice_dsp WHITEBOX)
    maul_add_test(gain_control)
    maul_add_test(noise_suppressor)
    maul_add_test(echo_canceller)
    # The guide's snippets (docs/guide.md), as written there: both parts'.
    if(MAUL_AUDIO_SPATIAL)
        maul_add_test(guide)
        maul_audio_test_data(guide)
        # Its stream plays on the test daemon, which other tests plug,
        # unplug and restart.
        set_tests_properties(guide PROPERTIES RESOURCE_LOCK pipewire-daemon)
        # Node's own file system, so the web reads the HRTF set too.
        if(EMSCRIPTEN)
            target_link_options(test_guide PRIVATE -sNODERAWFS=1)
        endif()
        # Its stream may open ALSA's default PCM on a machine without
        # sound, where libasound leaks.
        if(MAUL_AUDIO_SANITIZE AND MAUL_AUDIO_ALSA)
            set_tests_properties(guide PROPERTIES ENVIRONMENT
                "LSAN_OPTIONS=suppressions=${PROJECT_SOURCE_DIR}/test/lsan-alsa.supp")
        endif()
    endif()
    # A host stalled inside a step, under a native stream that latches the
    # spatializer's results: both parts', a device's, and not the web's,
    # whose callback runs on the stalled thread itself.
    if(MAUL_AUDIO_SPATIAL AND NOT EMSCRIPTEN)
        maul_add_test(stall)
        set_tests_properties(stall PROPERTIES SKIP_RETURN_CODE 77 RESOURCE_LOCK pipewire-daemon)
        # Its stream may open ALSA's default PCM on a machine without
        # sound, where libasound leaks.
        if(MAUL_AUDIO_SANITIZE AND MAUL_AUDIO_ALSA)
            set_tests_properties(stall PROPERTIES ENVIRONMENT
                "LSAN_OPTIONS=suppressions=${PROJECT_SOURCE_DIR}/test/lsan-alsa.supp")
        endif()
    endif()
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
        # The same with the host's thread in a single-threaded apartment.
        add_test(NAME wasapi_sta COMMAND test_wasapi)
        set_tests_properties(wasapi_sta PROPERTIES SKIP_RETURN_CODE 77
            ENVIRONMENT MAUD_WASAPI_STA=1)
    endif()
    # The iOS backend in the simulator, in Objective-C to read the
    # session's category.
    if(MAUL_AUDIO_IOS)
        add_executable(test_ios test/test_ios.m)
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
        set_tests_properties(ios_capture PROPERTIES TIMEOUT 240 RESOURCE_LOCK simulator)
        set_tests_properties(ios PROPERTIES RESOURCE_LOCK simulator)
    endif()
    if(MAUL_AUDIO_AAUDIO)
        maul_add_test(aaudio THREADS)
        set_tests_properties(aaudio PROPERTIES SKIP_RETURN_CODE 77)
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
endif()

# Every public header compiles alone, whichever parts are built.
if(MAUL_AUDIO_BUILD_TESTS)
    maul_add_header_tests()
endif()

# The fuzz targets: test/fuzz_<area>.c, run as fuzz_<area> <corpus dir>,
# over the bytes programs load and hand the library; the shipped HRTF
# file in data/hrtf/, behind a 0x01 byte, is fuzz_hrtf's seed; the shipped
# bake in data/bake/, behind a 0x03 byte (fields of order 1), fuzz_bake's.
if(MAUL_AUDIO_FUZZ AND MAUL_AUDIO_SPATIAL)
    # The library is instrumented too: its coverage guides libFuzzer, and
    # the sanitizers watch its code, not only the targets'.
    target_compile_options(maul-audio PRIVATE -fsanitize=fuzzer-no-link,address,undefined)
    target_link_options(maul-audio PUBLIC -fsanitize=address,undefined)
    foreach(area hrtf bake)
        add_executable(fuzz_${area} test/fuzz_${area}.c)
        target_link_libraries(fuzz_${area} PRIVATE maul-audio)
        # The targets reach the library's own functions.
        target_include_directories(fuzz_${area} PRIVATE ${PROJECT_SOURCE_DIR}/src)
        maul_apply_flags(fuzz_${area})
        target_compile_options(fuzz_${area} PRIVATE -fsanitize=fuzzer,address,undefined)
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
