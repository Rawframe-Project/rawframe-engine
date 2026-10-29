cmake_minimum_required(VERSION 3.25)

# The library's identity. The family's CMake modules and tools read these.
set(MAUL_LIBRARY maul-rhi)
set(MAUL_API_PREFIX mrhi)
set(MAUL_MACRO_PREFIX MRHI)
set(MAUL_TITLE "Maul RHI")
set(MAUL_PREFIX MAUL_RHI)

include(cmake/ReadVersion.cmake)
maul_read_version(${CMAKE_CURRENT_SOURCE_DIR}/include/maul-rhi/base.h MRHI MAUL_VERSION)
project(maul-rhi
    VERSION ${MAUL_VERSION}
    DESCRIPTION "The GPU device layer: devices, resources, a frame graph and presentation"
    LANGUAGES C)

option(MAUL_RHI_BUILD_TESTS "Build the test suites" ${PROJECT_IS_TOP_LEVEL})
option(MAUL_RHI_BUILD_BENCH "Build the benchmarks" ${PROJECT_IS_TOP_LEVEL})
option(MAUL_RHI_BUILD_SAMPLES "Build the samples" ${PROJECT_IS_TOP_LEVEL})
option(MAUL_RHI_BUILD_SHARED "Build a shared library instead of a static one" OFF)
option(MAUL_RHI_INSTALL "Generate install rules" ${PROJECT_IS_TOP_LEVEL})
set(MAUL_RHI_CONFORMANCE_DRIVER "" CACHE STRING
    "A target defining mrhiConformanceDriver, to run the conformance suite on")
option(MAUL_RHI_WERROR "Treat compiler warnings as errors" OFF)
option(MAUL_RHI_SANITIZE "Instrument with AddressSanitizer and UBSan" OFF)
option(MAUL_RHI_TSAN "Instrument with ThreadSanitizer" OFF)
option(MAUL_RHI_VALIDATION "Build the validation layer around the driver SPI" OFF)
option(MAUL_RHI_LABELS "Hand debug labels to drivers and record label commands" ON)
if(NOT MAUL_RHI_LABELS)
    # Labels are still checked, then dropped: every target here sees it.
    add_compile_definitions(MAUL_RHI_LABELS=0)
endif()
option(MAUL_RHI_COVERAGE "Instrument for gcov coverage" OFF)
option(MAUL_RHI_FUZZ "Build the libFuzzer targets (Clang)" OFF)
option(MAUL_RHI_SEAM "Build the check against Maul Window, fetched for it (test only)" OFF)
# The tests run on the test driver, and so do fuzz_cache and fuzz_pipeline.
if(MAUL_RHI_BUILD_TESTS OR MAUL_RHI_FUZZ)
    set(maul_rhi_test_driver ON)
else()
    set(maul_rhi_test_driver OFF)
endif()
option(MAUL_RHI_TEST_DRIVER "Build the test driver (mrhiTestDriverDef)" ${maul_rhi_test_driver})
# The web: Emscripten's, or a plain WebAssembly toolchain's.
if(EMSCRIPTEN OR CMAKE_SYSTEM_NAME STREQUAL "WASI")
    set(maul_rhi_web ON)
else()
    set(maul_rhi_web OFF)
endif()
# The native driver, one per build: Metal on Apple systems, D3D12 on
# Windows, Vulkan on the others; the web has none. A Windows build may
# take Vulkan instead of D3D12.
if(maul_rhi_web OR APPLE OR WIN32)
    set(maul_rhi_vulkan_default OFF)
else()
    set(maul_rhi_vulkan_default ON)
endif()
option(MAUL_RHI_VULKAN_DRIVER "Build the Vulkan driver" ${maul_rhi_vulkan_default})
# Each pass class's Vulkan work as submissions of its own on the one
# queue, waiting on each other through timeline semaphores: a check of
# the driver's batches, never a release build's.
option(MAUL_RHI_VULKAN_SPLIT_LANES "Submit each pass class's Vulkan work apart (test only)" OFF)
# The Metal driver, in Objective-C.
option(MAUL_RHI_METAL_DRIVER "Build the Metal driver" ${APPLE})
if(MAUL_RHI_METAL_DRIVER AND NOT APPLE)
    message(FATAL_ERROR "The Metal driver builds on Apple systems only")
endif()
if(MAUL_RHI_METAL_DRIVER AND MAUL_RHI_VULKAN_DRIVER)
    message(FATAL_ERROR "A build has one native driver: turn MAUL_RHI_VULKAN_DRIVER off "
                        "with MAUL_RHI_METAL_DRIVER on")
endif()
# The D3D12 driver, the native driver of Windows builds.
option(MAUL_RHI_D3D12_DRIVER "Build the D3D12 driver" ${WIN32})
if(MAUL_RHI_D3D12_DRIVER AND NOT WIN32)
    message(FATAL_ERROR "The D3D12 driver builds on Windows only")
endif()
if(MAUL_RHI_D3D12_DRIVER AND MAUL_RHI_VULKAN_DRIVER)
    message(FATAL_ERROR "A build has one native driver: turn MAUL_RHI_VULKAN_DRIVER off "
                        "with MAUL_RHI_D3D12_DRIVER on")
endif()
# The WebGPU driver: every web build.
option(MAUL_RHI_WEBGPU_DRIVER "Build the WebGPU driver" ${maul_rhi_web})

# Every executable (tests, benchmarks, tools) lands in the top of the
# build tree, so scripts and CI find them in one place.
if(PROJECT_IS_TOP_LEVEL)
    set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${PROJECT_BINARY_DIR})
endif()

include(GNUInstallDirs)
include(cmake/CompilerFlags.cmake)
include(cmake/Library.cmake)
include(cmake/Testing.cmake)

if(MAUL_RHI_BUILD_SHARED)
    set(MAUL_LIBRARY_KIND SHARED)
else()
    set(MAUL_LIBRARY_KIND STATIC)
endif()
add_library(maul-rhi ${MAUL_LIBRARY_KIND}
    src/adapter.c
    src/allocator.c
    src/base.c
    src/cache.c
    src/chain.c
    src/vulkan_external.c
    src/d3d12_external.c
    src/metal_external.c
    src/compile.c
    src/container.c
    src/container_table.c
    src/container_interface.c
    src/spirv_caps.c
    src/device.c
    src/diagnostics.c
    src/binding.c
    src/copy.c
    src/upload.c
    src/heap.c
    src/query.c
    src/readback.c
    src/submit.c
    src/draw.c
    src/encoder.c
    src/frame.c
    src/format_caps.c
    src/frame_resource.c
    src/driver.c
    src/generated/capabilities.c
    src/generated/defaults.c
    src/generated/diagnostic_texts.c
    src/generated/result_names.c
    src/graphics.c
    src/instance.c
    src/label.c
    src/memory.c
    src/notify.c
    src/pass.c
    src/pipeline.c
    src/plan.c
    src/pool.c
    src/reflection.c
    src/resources.c
    src/sha256.c
    src/stage_limits.c
    src/shader.c
    src/surface.c
    src/swapchain.c
    src/texture.c
    src/tlsf.c
    src/view.c)
maul_configure_library(maul-rhi)
if(MAUL_RHI_TEST_DRIVER)
    target_sources(maul-rhi PRIVATE src/driver_test.c src/pipeline_walk.c)
    target_compile_definitions(maul-rhi PRIVATE MAUL_RHI_TEST_DRIVER)
endif()
if(MAUL_RHI_VALIDATION)
    target_sources(maul-rhi PRIVATE src/validation.c)
    target_compile_definitions(maul-rhi PRIVATE MAUL_RHI_VALIDATION)
endif()
# The frame walk: the test driver traps on its faults, the validation
# layer counts them.
if(MAUL_RHI_TEST_DRIVER OR MAUL_RHI_VALIDATION)
    target_sources(maul-rhi PRIVATE src/frame_walk.c)
endif()
if(MAUL_RHI_VULKAN_DRIVER)
    target_sources(maul-rhi PRIVATE src/driver_vulkan.c src/vulkan_adapter.c src/vulkan_api.c
                                   src/vulkan_device.c src/vulkan_facts.c src/vulkan_frame.c src/vulkan_graphics.c
                                   src/vulkan_heap.c src/vulkan_label.c src/vulkan_lanes.c src/vulkan_memory.c
                                   src/vulkan_object.c src/vulkan_pass.c src/vulkan_pipeline.c
                                   src/vulkan_query.c src/vulkan_recipe.c src/vulkan_record.c
                                   src/vulkan_resource.c
                                   src/vulkan_surface.c src/vulkan_swapchain.c)
    target_compile_definitions(maul-rhi PRIVATE MAUL_RHI_VULKAN_DRIVER)
    if(MAUL_RHI_VULKAN_SPLIT_LANES)
        target_compile_definitions(maul-rhi PRIVATE MAUL_RHI_VULKAN_SPLIT_LANES)
    endif()
    # The Khronos headers, kept as published (khronos/README.md).
    target_include_directories(maul-rhi SYSTEM PRIVATE ${PROJECT_SOURCE_DIR}/khronos)
    target_link_libraries(maul-rhi PRIVATE ${CMAKE_DL_LIBS})
endif()
if(MAUL_RHI_METAL_DRIVER)
    enable_language(OBJC)
    target_sources(maul-rhi PRIVATE src/driver_metal.m src/metal_device.m src/metal_names.m
                                   src/metal_copy.m src/metal_encode.m src/metal_frame.m
                                   src/metal_graphics.m src/metal_lanes.m src/metal_pipeline.m
                                   src/metal_resource.m src/metal_surface.m)
    target_compile_definitions(maul-rhi PRIVATE MAUL_RHI_METAL_DRIVER)
    # The family's C23 for Objective-C, with manual retain and release so
    # that Metal objects sit in plain C structs.
    set_target_properties(maul-rhi PROPERTIES
        OBJC_STANDARD 23
        OBJC_STANDARD_REQUIRED ON
        OBJC_EXTENSIONS OFF
        OBJC_VISIBILITY_PRESET hidden)
    target_compile_options(maul-rhi PRIVATE
        $<$<COMPILE_LANGUAGE:OBJC>:-fno-objc-arc -Wmissing-prototypes>)
    target_link_libraries(maul-rhi PRIVATE "-framework CoreGraphics" "-framework Foundation"
                                           "-framework Metal" "-framework QuartzCore")
endif()
if(MAUL_RHI_D3D12_DRIVER)
    target_sources(maul-rhi PRIVATE src/driver_d3d12.c src/d3d12_api.c src/d3d12_device.c
                                   src/d3d12_names.c src/d3d12_resource.c src/d3d12_slots.c
                                   src/d3d12_view.c src/d3d12_pipeline.c src/d3d12_root.c
                                   src/d3d12_graphics.c src/d3d12_frame.c src/d3d12_lanes.c
                                   src/d3d12_record.c src/d3d12_barrier.c src/d3d12_copy.c
                                   src/d3d12_bind.c src/d3d12_draw.c src/d3d12_heap.c
                                   src/d3d12_surface.c src/d3d12_swapchain.c)
    target_compile_definitions(maul-rhi PRIVATE MAUL_RHI_D3D12_DRIVER)
    # The DirectX headers, kept as published (directx/README.md).
    target_include_directories(maul-rhi SYSTEM PRIVATE ${PROJECT_SOURCE_DIR}/directx)
endif()
if(MAUL_RHI_WEBGPU_DRIVER)
    include(cmake/Web.cmake)
endif()

if(MAUL_RHI_BUILD_TESTS)
    enable_testing()
    maul_add_test(base)
    maul_add_test(chain WHITEBOX)
    maul_add_test(layout WHITEBOX)
    maul_add_test(notify WHITEBOX)
    maul_add_test(internals WHITEBOX)
    maul_add_test(pool WHITEBOX)
    maul_add_test(label WHITEBOX)
    maul_add_test(sha256 WHITEBOX)
    maul_add_test(tlsf WHITEBOX)
    maul_add_test(stage_limits WHITEBOX)
    if(MAUL_RHI_VULKAN_DRIVER)
        maul_add_test(vulkan_label WHITEBOX)
        maul_add_test(vulkan_limits WHITEBOX)
        maul_add_test(vulkan_facts WHITEBOX)
        maul_add_test(vulkan_surface WHITEBOX)
        maul_add_test(vulkan_swapchain WHITEBOX)
        maul_add_test(vulkan_external WHITEBOX)
        maul_add_test(vulkan_frame WHITEBOX)
        maul_add_test(vulkan_zero)
        maul_add_test(vulkan_cache WHITEBOX)
        foreach(test vulkan_label vulkan_limits vulkan_facts vulkan_surface vulkan_swapchain
                     vulkan_external vulkan_frame vulkan_zero vulkan_cache)
            if(TARGET test_${test})
                target_include_directories(test_${test} SYSTEM PRIVATE
                                           ${PROJECT_SOURCE_DIR}/khronos)
            endif()
        endforeach()
    endif()
    if(MAUL_RHI_METAL_DRIVER)
        # A native pass on Metal, and the queues a frame's passes run on,
        # seen through the command buffers' methods the suite swaps; in
        # Objective-C as the driver is, without ARC.
        foreach(test metal_native metal_queues)
            add_executable(test_${test} test/test_${test}.m)
            target_link_libraries(test_${test} PRIVATE maul-rhi "-framework Metal"
                                  "-framework Foundation")
            set_target_properties(test_${test} PROPERTIES OBJC_STANDARD 23
                                                          OBJC_STANDARD_REQUIRED ON)
            target_compile_options(test_${test} PRIVATE -fno-objc-arc)
            maul_apply_flags(test_${test})
            add_test(NAME ${test} COMMAND test_${test})
        endforeach()
    endif()
    if(MAUL_RHI_D3D12_DRIVER)
        # A native pass on D3D12, and the queues a frame's passes run on,
        # seen through the runtime's tables the suite wraps: under the
        # debug layer and the Agility SDK each exe exports.
        foreach(test d3d12_native d3d12_queues)
            maul_add_test(${test})
            target_sources(test_${test} PRIVATE test/d3d12_debug.c)
            target_include_directories(test_${test} SYSTEM PRIVATE ${PROJECT_SOURCE_DIR}/directx)
            target_link_libraries(test_${test} PRIVATE d3d12 dxgi)
        endforeach()
        # A failed frame's drain that never returns is a hang: the suite
        # runs in about a second on CI.
        set_tests_properties(d3d12_queues PROPERTIES TIMEOUT 10)
        # GPU-based validation's false state errors (#7) reproduced without
        # library code: built with the tests so that it keeps compiling,
        # run by hand as docs/operations/debugging.md says.
        add_executable(d3d12_uav_check test/d3d12_uav_check.c test/d3d12_debug.c)
        target_include_directories(d3d12_uav_check SYSTEM PRIVATE ${PROJECT_SOURCE_DIR}/directx)
        target_link_libraries(d3d12_uav_check PRIVATE d3d12 dxgi)
        maul_apply_flags(d3d12_uav_check)
        # The suites export the Agility SDK they run on from D3D12/ beside
        # them (test/d3d12_debug.c), where this copies the redistributable
        # DLLs of its NuGet package, the version the directx/ headers are.
        set(MAUL_RHI_D3D12_AGILITY "" CACHE PATH
            "The Agility SDK's bin/x64 folder, copied into D3D12/ beside the tests")
        if(MAUL_RHI_D3D12_AGILITY)
            file(GLOB maul_rhi_agility "${MAUL_RHI_D3D12_AGILITY}/*.dll")
            if(NOT maul_rhi_agility)
                message(FATAL_ERROR "MAUL_RHI_D3D12_AGILITY holds no DLL: ${MAUL_RHI_D3D12_AGILITY}")
            endif()
            add_custom_target(maul_rhi_agility ALL
                COMMAND ${CMAKE_COMMAND} -E make_directory $<TARGET_FILE_DIR:test_d3d12_native>/D3D12
                COMMAND ${CMAKE_COMMAND} -E copy_if_different ${maul_rhi_agility}
                        $<TARGET_FILE_DIR:test_d3d12_native>/D3D12
                VERBATIM)
        endif()
    endif()
    maul_add_test(instance)
    if(MAUL_RHI_TEST_DRIVER)
        maul_add_test(adapter)
        maul_add_test(device)
        maul_add_test(diagnostics)
        if(NOT maul_rhi_web AND NOT WIN32)
            find_package(Threads REQUIRED)
            target_link_libraries(test_diagnostics PRIVATE Threads::Threads)
        endif()
        maul_add_test(format)
        maul_add_test(sampler)
        maul_add_test(buffer)
        maul_add_test(zero)
        maul_add_test(zero_texture)
        maul_add_test(texture)
        maul_add_test(view)
        maul_add_test(surface)
        maul_add_test(swapchain)
        maul_add_test(frame)
        maul_add_test(frame_usage)
        maul_add_test(frame_resource)
        maul_add_test(pass)
        maul_add_test(plan)
        maul_add_test(lanes WHITEBOX)
        maul_add_test(memory)
        maul_add_test(shader WHITEBOX)
        maul_add_test(container_d3d12 WHITEBOX)
        maul_add_test(pipeline WHITEBOX)
        maul_add_test(pipeline_walk WHITEBOX)
        maul_add_test(graphics WHITEBOX)
        maul_add_test(cache WHITEBOX)
        maul_add_test(encoder WHITEBOX)
        maul_add_test(binding WHITEBOX)
        maul_add_test(draw WHITEBOX)
        maul_add_test(cts)
        maul_add_test(copy WHITEBOX)
        maul_add_test(upload WHITEBOX)
        maul_add_test(query WHITEBOX)
        maul_add_test(heap WHITEBOX)
        maul_add_test(seal)
        maul_add_test(busy)
        maul_add_test(alloc_failure)
        maul_add_test(readback WHITEBOX)
        maul_add_test(submit WHITEBOX)
        maul_add_test(acquire WHITEBOX)
        maul_add_test(loss WHITEBOX)
        maul_add_test(external WHITEBOX)
        maul_add_test(outputs)
        maul_add_test(stored_bytes)
        maul_add_test(out_bytes)
        if(MAUL_RHI_VALIDATION)
            maul_add_test(validation WHITEBOX)
        endif()
        # The container writer against the reader, where Python runs.
        find_package(Python3 COMPONENTS Interpreter)
        if(TARGET test_shader AND Python3_Interpreter_FOUND AND NOT CMAKE_CROSSCOMPILING)
            add_test(NAME container_writer
                     COMMAND Python3::Interpreter ${PROJECT_SOURCE_DIR}/test/test_container_writer.py
                             $<TARGET_FILE:test_shader>)
            set_tests_properties(container_writer PROPERTIES LABELS container_writer)
        endif()
        # The D3D12 tool's refusal of a loop variable read after its step.
        if(Python3_Interpreter_FOUND AND NOT CMAKE_CROSSCOMPILING)
            add_test(NAME dxil_order COMMAND Python3::Interpreter ${PROJECT_SOURCE_DIR}/test/test_dxil_order.py)
            set_tests_properties(dxil_order PROPERTIES LABELS dxil_order)
        endif()
    endif()
    # Every driver the build has, through the public API. Where the XCB
    # client library is installed, the suite also makes a window to
    # check surfaces on; only the test links it. On the web it runs in
    # headless Chrome (test/web_runner.cjs), sleeping through JSPI while
    # the browser settles its promises, with Emscripten or without.
    if(maul_rhi_web)
        maul_add_test(conformance MANUAL)
        if(EMSCRIPTEN)
            target_link_options(test_conformance PRIVATE -sJSPI -sEXIT_RUNTIME
                                -sEXPORTED_RUNTIME_METHODS=ENV)
        else()
            # A reactor whose main the runner calls through JSPI, with the
            # driver's imports from maul-rhi.mjs.
            target_link_options(test_conformance PRIVATE -mexec-model=reactor -Wl,--export-table
                                -Wl,--export=main)
        endif()
        add_test(NAME conformance
                 COMMAND node ${PROJECT_SOURCE_DIR}/test/web_runner.cjs
                         $<TARGET_FILE:test_conformance>)
        # Again in a dedicated worker, on the canvases'
        # OffscreenCanvases.
        add_test(NAME conformance_worker
                 COMMAND node ${PROJECT_SOURCE_DIR}/test/web_runner.cjs --worker
                         $<TARGET_FILE:test_conformance>)
        # hygiene: no adapter is exit 77, and CI fails any skipped test
        set_tests_properties(conformance conformance_worker PROPERTIES SKIP_RETURN_CODE 77)
    else()
        maul_add_test(conformance)
        if(MAUL_RHI_METAL_DRIVER)
            # A CAMetalLayer outside any window to present to.
            target_sources(test_conformance PRIVATE test/metal_layer.m)
            set_target_properties(test_conformance PROPERTIES OBJC_STANDARD 23
                                                              OBJC_STANDARD_REQUIRED ON)
            target_compile_options(test_conformance PRIVATE
                                   $<$<COMPILE_LANGUAGE:OBJC>:-fno-objc-arc>)
            target_compile_definitions(test_conformance PRIVATE MAUL_RHI_METAL_DRIVER)
            target_link_libraries(test_conformance PRIVATE "-framework QuartzCore")
        endif()
        if(MAUL_RHI_D3D12_DRIVER)
            # The debug layer, and the Agility SDK the exe exports.
            target_sources(test_conformance PRIVATE test/d3d12_debug.c)
            target_include_directories(test_conformance SYSTEM PRIVATE
                                       ${PROJECT_SOURCE_DIR}/directx)
            target_compile_definitions(test_conformance PRIVATE MAUL_RHI_D3D12_DRIVER)
            target_link_libraries(test_conformance PRIVATE d3d12 dxgi)
        endif()
    endif()
    # The suite's cases are the mustpass list of the SPI version,
    # conformance/mustpass/spi-<N>.txt, which must match what the
    # suite lists.
    file(STRINGS src/driver.h maul_rhi_spi REGEX "^#define MRHI_SPI_VERSION ")
    string(REGEX REPLACE ".* ([0-9]+)$" "\\1" maul_rhi_spi "${maul_rhi_spi}")
    target_compile_definitions(test_conformance PRIVATE MRHI_TEST_SPI_VERSION=${maul_rhi_spi})
    # threads.recording records on a second thread, except on the web.
    if(NOT maul_rhi_web)
        find_package(Threads REQUIRED)
        target_link_libraries(test_conformance PRIVATE Threads::Threads)
    endif()
    if(NOT maul_rhi_web AND NOT CMAKE_CROSSCOMPILING)
        add_test(NAME mustpass
                 COMMAND ${CMAKE_COMMAND} -DSUITE=$<TARGET_FILE:test_conformance>
                         -DLIST=${PROJECT_SOURCE_DIR}/conformance/mustpass/spi-${maul_rhi_spi}.txt
                         -P ${PROJECT_SOURCE_DIR}/test/mustpass.cmake)
        set_tests_properties(mustpass PROPERTIES LABELS mustpass)
    endif()
    find_package(PkgConfig QUIET)
    if(PkgConfig_FOUND AND MAUL_RHI_VULKAN_DRIVER AND NOT CMAKE_CROSSCOMPILING)
        pkg_check_modules(MAUL_RHI_XCB QUIET IMPORTED_TARGET xcb)
        if(MAUL_RHI_XCB_FOUND)
            target_link_libraries(test_conformance PRIVATE PkgConfig::MAUL_RHI_XCB)
            target_compile_definitions(test_conformance PRIVATE MRHI_TEST_XCB)
        endif()
    endif()
    # On Android the suite also runs as an application on the system's
    # NativeActivity, whose window its surface checks present to
    # (test/android_activity.c).
    if(ANDROID AND MAUL_RHI_VULKAN_DRIVER)
        set(package org.maul.rhi.tests.conformance)
        add_library(test_conformance_app SHARED test/test_conformance.c test/android_activity.c)
        target_link_libraries(test_conformance_app PRIVATE maul-rhi android)
        target_include_directories(test_conformance_app PRIVATE ${PROJECT_SOURCE_DIR}/test)
        target_compile_definitions(test_conformance_app PRIVATE MRHI_TEST_ANDROID
                                                                MRHI_TEST_SPI_VERSION=${maul_rhi_spi})
        maul_apply_flags(test_conformance_app)
        add_custom_command(OUTPUT ${PROJECT_BINARY_DIR}/test_conformance_app.apk
            COMMAND ${CMAKE_COMMAND} -E env ANDROID_HOME=$ENV{ANDROID_HOME}
                    ${PROJECT_SOURCE_DIR}/tools/build_android_app.sh
                    ${PROJECT_BINARY_DIR}/test_conformance_app.apk
                    $<TARGET_FILE:test_conformance_app> ${package} ${ANDROID_ABI}
            DEPENDS test_conformance_app ${PROJECT_SOURCE_DIR}/test/android/AndroidManifest.xml
                    ${PROJECT_SOURCE_DIR}/tools/build_android_app.sh
            VERBATIM)
        add_custom_target(test_conformance_apk ALL
                          DEPENDS ${PROJECT_BINARY_DIR}/test_conformance_app.apk)
        add_test(NAME conformance_app COMMAND ${PROJECT_SOURCE_DIR}/tools/run_android_app.sh
                 ${PROJECT_BINARY_DIR}/test_conformance_app.apk ${package})
        # The application always has a window, so its surface is required.
        set_tests_properties(conformance_app PROPERTIES TIMEOUT 300
                             ENVIRONMENT MAUL_RHI_REQUIRE_SURFACE=1)
    endif()
    maul_add_header_tests()
    maul_add_exports_test()
    # The suite on a driver from outside the tree: a target defining
    # mrhiConformanceDriver(mrhiExternalDriverDef*), named by
    # MAUL_RHI_CONFORMANCE_DRIVER. Without one, a Linux build hands the
    # suite its own Vulkan driver through the external path.
    set(maul_rhi_outside "${MAUL_RHI_CONFORMANCE_DRIVER}")
    if(NOT maul_rhi_outside AND MAUL_RHI_VULKAN_DRIVER AND CMAKE_SYSTEM_NAME STREQUAL "Linux"
       AND NOT MAUL_RHI_BUILD_SHARED)
        add_library(conformance_vulkan_external STATIC test/conformance_vulkan_external.c)
        target_link_libraries(conformance_vulkan_external PRIVATE maul-rhi)
        target_include_directories(conformance_vulkan_external PRIVATE ${PROJECT_SOURCE_DIR}/src)
        maul_apply_flags(conformance_vulkan_external)
        set(maul_rhi_outside conformance_vulkan_external)
    endif()
    if(maul_rhi_outside AND NOT maul_rhi_web)
        add_executable(test_conformance_external test/test_conformance.c)
        target_link_libraries(test_conformance_external PRIVATE maul-rhi ${maul_rhi_outside}
                                                                Threads::Threads)
        target_compile_definitions(test_conformance_external
                                   PRIVATE MRHI_TEST_EXTERNAL MRHI_TEST_SPI_VERSION=${maul_rhi_spi})
        maul_apply_flags(test_conformance_external)
        if(MAUL_RHI_XCB_FOUND)
            target_link_libraries(test_conformance_external PRIVATE PkgConfig::MAUL_RHI_XCB)
            target_compile_definitions(test_conformance_external PRIVATE MRHI_TEST_XCB)
        endif()
        add_test(NAME conformance_external COMMAND test_conformance_external)
    endif()
    # The Vulkan loader keeps drivers loaded after an instance ends, so
    # that LeakSanitizer reads their memory as held, not lost. Where the
    # validation layer runs, it logs to the test's output, and a
    # validation error there fails the test. ThreadSanitizer leaves
    # lavapipe's own threads alone (test/tsan.supp).
    set(maul_rhi_test_env VK_LOADER_DISABLE_DYNAMIC_LIBRARY_UNLOADING=1)
    if(MAUL_RHI_TSAN)
        list(APPEND maul_rhi_test_env
             TSAN_OPTIONS=suppressions=${PROJECT_SOURCE_DIR}/test/tsan.supp)
    endif()
    get_property(maul_rhi_tests DIRECTORY PROPERTY TESTS)
    set_tests_properties(${maul_rhi_tests} PROPERTIES
                         ENVIRONMENT "${maul_rhi_test_env}"
                         FAIL_REGULAR_EXPRESSION "Validation Error")
    # The tests that drive a real GPU, window or browser: the changed-files
    # check leaves them out, every other check runs them.
    foreach(test conformance conformance_worker conformance_external conformance_app
                 vulkan_external vulkan_frame vulkan_zero vulkan_cache metal_native metal_queues
                 d3d12_native d3d12_queues external_build)
        if(TEST ${test})
            set_property(TEST ${test} APPEND PROPERTY LABELS integration)
        endif()
    endforeach()
endif()

# The fuzz targets: test/fuzz_<area>.c, run as fuzz_<area> <corpus dir>,
# over what programs load from disk or fill from their own data and hand
# the library. tools/container_seed.py writes fuzz_container's seeds;
# fuzz_cache makes its own envelopes, fuzz_vulkan_cache seals its own
# around the Vulkan driver's payload on the first Vulkan adapter, and
# fuzz_pipeline starts each pipeline def from a valid one.
# test/fuzz_coverage_ignore.txt names the sources coverage leaves out.
if(MAUL_RHI_FUZZ)
    # The library is instrumented too: its coverage guides libFuzzer, and
    # the sanitizers watch its code, not only the targets'. Undefined
    # behavior aborts the run, so libFuzzer writes the input that found it
    # instead of printing and going on.
    set(ignored ${PROJECT_SOURCE_DIR}/test/fuzz_coverage_ignore.txt)
    target_compile_options(maul-rhi PRIVATE -fsanitize=fuzzer-no-link,address,undefined
                                            -fno-sanitize-recover=all
                                            -fsanitize-coverage-ignorelist=${ignored})
    target_link_options(maul-rhi PUBLIC -fsanitize=address,undefined)
    set(areas container cache pipeline)
    if(MAUL_RHI_VULKAN_DRIVER)
        list(APPEND areas vulkan_cache)
    endif()
    foreach(area ${areas})
        add_executable(fuzz_${area} test/fuzz_${area}.c)
        target_link_libraries(fuzz_${area} PRIVATE maul-rhi)
        # The targets reach the library's own functions.
        target_include_directories(fuzz_${area} PRIVATE ${PROJECT_SOURCE_DIR}/src)
        maul_apply_flags(fuzz_${area})
        target_compile_options(fuzz_${area} PRIVATE -fsanitize=fuzzer,address,undefined
                                                   -fno-sanitize-recover=all)
        target_link_options(fuzz_${area} PRIVATE -fsanitize=fuzzer,address,undefined
                                                -fno-sanitize-recover=all)
    endforeach()
endif()

# The benchmarks run on the build's native driver through the samples'
# harness, and not on the web, whose timers and pacing are the
# browser's; bench/CMakeLists.txt is the family's.
if(MAUL_RHI_BUILD_BENCH AND EXISTS ${PROJECT_SOURCE_DIR}/bench/bench_main.c AND
   NOT (EMSCRIPTEN OR CMAKE_SYSTEM_NAME STREQUAL "WASI"))
    add_subdirectory(bench)
    target_sources(${PROJECT_NAME}_bench PRIVATE ${PROJECT_SOURCE_DIR}/samples/harness.c)
    target_include_directories(${PROJECT_NAME}_bench PRIVATE ${PROJECT_SOURCE_DIR}/samples)
endif()

if(MAUL_RHI_BUILD_SAMPLES)
    add_subdirectory(samples)
endif()

# The guide's C snippets as written, run natively on the samples' harness
# and triangle; the web's waits are a later frame's.
if(MAUL_RHI_BUILD_TESTS AND NOT (EMSCRIPTEN OR CMAKE_SYSTEM_NAME STREQUAL "WASI"))
    add_executable(test_guide test/test_guide.c samples/harness.c)
    target_link_libraries(test_guide PRIVATE maul-rhi)
    target_include_directories(test_guide PRIVATE ${PROJECT_SOURCE_DIR}/samples)
    maul_apply_flags(test_guide)
    add_test(NAME guide COMMAND test_guide)
    # hygiene: no adapter is exit 77, and CI fails any skipped test
    set_tests_properties(guide PROPERTIES SKIP_RETURN_CODE 77 ENVIRONMENT "${maul_rhi_test_env}"
                         FAIL_REGULAR_EXPRESSION "Validation Error" LABELS "guide;integration")
endif()

# The seam with Maul Window, test only.
if(MAUL_RHI_SEAM AND MAUL_RHI_BUILD_TESTS)
    add_subdirectory(test/seam)
endif()

if(MAUL_RHI_INSTALL)
    include(cmake/Install.cmake)
    # The driver SPI, for drivers built outside the tree.
    install(FILES src/driver.h src/command.h src/reflection.h src/container.h
                  src/container_interface.h
            DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}/${PROJECT_NAME}/spi)
    # Such a driver, built against an install of this build alone; not
    # under the sanitizers, whose flags the package does not carry.
    if(MAUL_RHI_BUILD_TESTS AND NOT CMAKE_CROSSCOMPILING AND NOT WIN32 AND NOT MAUL_RHI_SANITIZE
       AND NOT MAUL_RHI_TSAN)
        add_test(NAME external_build
                 COMMAND ${CMAKE_COMMAND} -DBUILD=${PROJECT_BINARY_DIR}
                         -DSOURCE=${PROJECT_SOURCE_DIR}/test/external -DGENERATOR=${CMAKE_GENERATOR}
                         -DCC=${CMAKE_C_COMPILER} -DCONFIG=$<CONFIG> -P
                         ${PROJECT_SOURCE_DIR}/test/external/run.cmake)
        # Added after the loop above labels the integration tests.
        set_tests_properties(external_build PROPERTIES LABELS "external_build;integration")
    endif()
endif()
