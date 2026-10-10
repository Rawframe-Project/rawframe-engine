# The reference renderer (record mui-0005), maul-ui-rhi: a static
# library beside maul-ui, drawing its lists with Maul RHI, found installed
# or fetched at its release tag as Maul Unicode is. The core and the text
# component never depend on it.

set(MAUL_UI_RHI_VERSION 0.7.0)
find_package(maul-rhi ${MAUL_UI_RHI_VERSION} QUIET)
if(NOT maul-rhi_FOUND)
    set(MAUL_RHI_BUILD_TESTS OFF)
    set(MAUL_RHI_BUILD_BENCH OFF)
    set(MAUL_RHI_BUILD_SAMPLES OFF)
    set(MAUL_RHI_INSTALL OFF)
    # The renderer's tests run on its test driver.
    set(MAUL_RHI_TEST_DRIVER ${MAUL_UI_BUILD_TESTS})
    include(FetchContent)
    FetchContent_Declare(maul-rhi
        GIT_REPOSITORY https://github.com/Rawframe-Project/maul-rhi.git
        GIT_TAG v${MAUL_UI_RHI_VERSION}
        GIT_SHALLOW TRUE)
    FetchContent_MakeAvailable(maul-rhi)
endif()

add_library(maul-ui-rhi STATIC rhi/src/allocator.c rhi/src/cull.c rhi/src/glyphs.c
    rhi/src/images.c rhi/src/pack.c rhi/src/plan.c rhi/src/renderer.c rhi/src/streams.c)
add_library(maul-ui-rhi::maul-ui-rhi ALIAS maul-ui-rhi)
target_include_directories(maul-ui-rhi PUBLIC
    $<BUILD_INTERFACE:${PROJECT_SOURCE_DIR}/rhi/include>
    $<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}>)
target_link_libraries(maul-ui-rhi PUBLIC maul-ui maul-rhi::maul-rhi)
# Linked with maul-ui, which holds HarfBuzz's C++ objects, it would link
# as C++, and Visual Studio's project would drop its C standard.
set_target_properties(maul-ui-rhi PROPERTIES LINKER_LANGUAGE C)
maul_apply_flags(maul-ui-rhi)
# Glyph runs are drawn with Maul UI's glyph atlas, which its text
# component has.
target_compile_definitions(maul-ui-rhi PRIVATE MUI_RHI_TEXT=$<BOOL:${MAUL_UI_TEXT}>)
