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
option(MAUL_RHI_WERROR "Treat compiler warnings as errors" OFF)
option(MAUL_RHI_SANITIZE "Instrument with AddressSanitizer and UBSan" OFF)
option(MAUL_RHI_TSAN "Instrument with ThreadSanitizer" OFF)
option(MAUL_RHI_COVERAGE "Instrument for gcov coverage" OFF)
option(MAUL_RHI_FUZZ "Build the libFuzzer targets (Clang)" OFF)
option(MAUL_RHI_TEST_DRIVER "Build the test driver (mrhiTestDriverDef)" ${MAUL_RHI_BUILD_TESTS})
# The Vulkan driver (mrhi-0003): every native build; the web has no Vulkan.
if(EMSCRIPTEN)
    set(maul_rhi_vulkan_default OFF)
else()
    set(maul_rhi_vulkan_default ON)
endif()
option(MAUL_RHI_VULKAN_DRIVER "Build the Vulkan driver" ${maul_rhi_vulkan_default})
# The WebGPU driver (mrhi-0003): every web build.
option(MAUL_RHI_WEBGPU_DRIVER "Build the WebGPU driver" ${EMSCRIPTEN})

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
    src/compile.c
    src/container.c
    src/device.c
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
    src/shader.c
    src/surface.c
    src/swapchain.c
    src/texture.c
    src/tlsf.c
    src/view.c)
maul_configure_library(maul-rhi)
if(MAUL_RHI_TEST_DRIVER)
    target_sources(maul-rhi PRIVATE src/driver_test.c src/driver_test_frame.c)
    target_compile_definitions(maul-rhi PRIVATE MAUL_RHI_TEST_DRIVER)
endif()
if(MAUL_RHI_VULKAN_DRIVER)
    target_sources(maul-rhi PRIVATE src/driver_vulkan.c src/vulkan_adapter.c src/vulkan_api.c
                                   src/vulkan_device.c src/vulkan_frame.c src/vulkan_graphics.c
                                   src/vulkan_heap.c src/vulkan_label.c src/vulkan_memory.c
                                   src/vulkan_object.c src/vulkan_pass.c src/vulkan_pipeline.c
                                   src/vulkan_query.c src/vulkan_record.c src/vulkan_resource.c
                                   src/vulkan_surface.c src/vulkan_swapchain.c)
    target_compile_definitions(maul-rhi PRIVATE MAUL_RHI_VULKAN_DRIVER)
    # The Khronos headers, kept as published (khronos/README.md).
    target_include_directories(maul-rhi SYSTEM PRIVATE ${PROJECT_SOURCE_DIR}/khronos)
    target_link_libraries(maul-rhi PRIVATE ${CMAKE_DL_LIBS})
endif()
if(MAUL_RHI_WEBGPU_DRIVER)
    target_sources(maul-rhi PRIVATE src/driver_webgpu.c src/webgpu_device.c src/webgpu_frame.c
                                   src/webgpu_names.c src/webgpu_pipeline.c)
    target_compile_definitions(maul-rhi PRIVATE MAUL_RHI_WEBGPU_DRIVER)
endif()

if(MAUL_RHI_BUILD_TESTS)
    enable_testing()
    maul_add_test(base)
    maul_add_test(chain WHITEBOX)
    maul_add_test(layout WHITEBOX)
    maul_add_test(notify WHITEBOX)
    maul_add_test(pool WHITEBOX)
    maul_add_test(label WHITEBOX)
    maul_add_test(sha256 WHITEBOX)
    maul_add_test(tlsf WHITEBOX)
    if(MAUL_RHI_VULKAN_DRIVER)
        maul_add_test(vulkan_label WHITEBOX)
        if(TARGET test_vulkan_label)
            target_include_directories(test_vulkan_label SYSTEM PRIVATE
                                       ${PROJECT_SOURCE_DIR}/khronos)
        endif()
    endif()
    maul_add_test(instance)
    if(MAUL_RHI_TEST_DRIVER)
        maul_add_test(adapter)
        maul_add_test(device)
        maul_add_test(format)
        maul_add_test(sampler)
        maul_add_test(buffer)
        maul_add_test(texture)
        maul_add_test(view)
        maul_add_test(surface)
        maul_add_test(swapchain)
        maul_add_test(frame)
        maul_add_test(frame_resource)
        maul_add_test(pass)
        maul_add_test(plan)
        maul_add_test(memory)
        maul_add_test(shader WHITEBOX)
        maul_add_test(pipeline WHITEBOX)
        maul_add_test(graphics WHITEBOX)
        maul_add_test(cache WHITEBOX)
        maul_add_test(encoder WHITEBOX)
        maul_add_test(binding WHITEBOX)
        maul_add_test(draw WHITEBOX)
        maul_add_test(copy WHITEBOX)
        maul_add_test(upload WHITEBOX)
        maul_add_test(query WHITEBOX)
        maul_add_test(heap WHITEBOX)
        maul_add_test(seal)
        maul_add_test(readback WHITEBOX)
        maul_add_test(submit WHITEBOX)
        maul_add_test(acquire WHITEBOX)
        maul_add_test(loss WHITEBOX)
        # The container writer against the reader, where Python runs.
        find_package(Python3 COMPONENTS Interpreter)
        if(TARGET test_shader AND Python3_Interpreter_FOUND AND NOT CMAKE_CROSSCOMPILING)
            add_test(NAME container_writer
                     COMMAND Python3::Interpreter ${PROJECT_SOURCE_DIR}/test/test_container_writer.py
                             $<TARGET_FILE:test_shader>)
        endif()
    endif()
    # Every driver the build has, through the public API (mrhi-0003).
    # Where the XCB client library is installed, the suite also makes a
    # window to check surfaces on; only the test links it. On the web it
    # runs in headless Chrome (test/web_runner.cjs), sleeping through
    # JSPI while the browser settles its promises.
    if(EMSCRIPTEN)
        maul_add_test(conformance MANUAL)
        target_link_options(test_conformance PRIVATE -sJSPI -sEXIT_RUNTIME
                            -sEXPORTED_RUNTIME_METHODS=ENV)
        add_test(NAME conformance
                 COMMAND node ${PROJECT_SOURCE_DIR}/test/web_runner.cjs
                         $<TARGET_FILE:test_conformance>)
        set_tests_properties(conformance PROPERTIES SKIP_RETURN_CODE 77)
    else()
        maul_add_test(conformance)
    endif()
    find_package(PkgConfig QUIET)
    if(PkgConfig_FOUND AND MAUL_RHI_VULKAN_DRIVER)
        pkg_check_modules(MAUL_RHI_XCB QUIET IMPORTED_TARGET xcb)
        if(MAUL_RHI_XCB_FOUND)
            target_link_libraries(test_conformance PRIVATE PkgConfig::MAUL_RHI_XCB)
            target_compile_definitions(test_conformance PRIVATE MRHI_TEST_XCB)
        endif()
    endif()
    maul_add_header_tests()
    # The Vulkan loader keeps drivers loaded after an instance ends, so
    # that LeakSanitizer reads their memory as held, not lost. Where the
    # validation layer runs, it logs to the test's output, and a
    # validation error there fails the test (mrhi-0003). ThreadSanitizer
    # leaves lavapipe's own threads alone (test/tsan.supp).
    set(maul_rhi_test_env VK_LOADER_DISABLE_DYNAMIC_LIBRARY_UNLOADING=1)
    if(MAUL_RHI_TSAN)
        list(APPEND maul_rhi_test_env
             TSAN_OPTIONS=suppressions=${PROJECT_SOURCE_DIR}/test/tsan.supp)
    endif()
    get_property(maul_rhi_tests DIRECTORY PROPERTY TESTS)
    set_tests_properties(${maul_rhi_tests} PROPERTIES
                         ENVIRONMENT "${maul_rhi_test_env}"
                         FAIL_REGULAR_EXPRESSION "Validation Error")
endif()

# The fuzz targets: test/fuzz_<area>.c, run as fuzz_<area> <corpus dir>,
# over the bytes programs load from disk and hand the library.
# tools/container_seed.py writes fuzz_container's seed.
if(MAUL_RHI_FUZZ)
    # The library is instrumented too: its coverage guides libFuzzer, and
    # the sanitizers watch its code, not only the targets'.
    target_compile_options(maul-rhi PRIVATE -fsanitize=fuzzer-no-link,address,undefined)
    target_link_options(maul-rhi PUBLIC -fsanitize=address,undefined)
    foreach(area container)
        add_executable(fuzz_${area} test/fuzz_${area}.c)
        target_link_libraries(fuzz_${area} PRIVATE maul-rhi)
        # The targets reach the library's own functions.
        target_include_directories(fuzz_${area} PRIVATE ${PROJECT_SOURCE_DIR}/src)
        maul_apply_flags(fuzz_${area})
        target_compile_options(fuzz_${area} PRIVATE -fsanitize=fuzzer,address,undefined)
        target_link_options(fuzz_${area} PRIVATE -fsanitize=fuzzer,address,undefined)
    endforeach()
endif()

if(MAUL_RHI_BUILD_BENCH AND EXISTS ${PROJECT_SOURCE_DIR}/bench/bench_main.c)
    add_subdirectory(bench)
endif()

if(MAUL_RHI_BUILD_SAMPLES)
    add_subdirectory(samples)
endif()

if(MAUL_RHI_INSTALL)
    include(cmake/Install.cmake)
endif()
