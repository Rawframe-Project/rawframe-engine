# The Maul Window glue (record mui-0007), maul-ui-window: a static library
# beside maul-ui that feeds a window's records to a context, Maul Window
# found installed or fetched at its release tag as Maul RHI is. The core
# and the text component never depend on it.

set(MAUL_UI_WINDOW_VERSION 0.10.0)
find_package(maul-window ${MAUL_UI_WINDOW_VERSION} QUIET)
if(NOT maul-window_FOUND)
    set(MAUL_WINDOW_BUILD_TESTS OFF)
    set(MAUL_WINDOW_BUILD_BENCH OFF)
    set(MAUL_WINDOW_BUILD_SAMPLES OFF)
    set(MAUL_WINDOW_INSTALL OFF)
    # The glue's tests and the samples run on Maul Window's headless test
    # backend.
    if(MAUL_UI_BUILD_TESTS OR MAUL_UI_BUILD_SAMPLES)
        set(MAUL_WINDOW_TEST_BACKEND ON)
    else()
        set(MAUL_WINDOW_TEST_BACKEND OFF)
    endif()
    include(FetchContent)
    FetchContent_Declare(maul-window
        GIT_REPOSITORY https://github.com/Rawframe-Project/maul-window.git
        GIT_TAG v${MAUL_UI_WINDOW_VERSION}
        GIT_SHALLOW TRUE)
    FetchContent_MakeAvailable(maul-window)
endif()

add_library(maul-ui-window STATIC window/src/allocator.c window/src/gamepads.c window/src/glue.c)
# Compositions and text carets, with the text component.
if(MAUL_UI_TEXT)
    target_sources(maul-ui-window PRIVATE window/src/clipboard.c window/src/composition.c)
endif()
# Accessibility, with the tree's consumer: the adapters Maul UI was built
# with, named for window/src/access.c.
if(MAUL_UI_ACCESS_TREE)
    target_sources(maul-ui-window PRIVATE window/src/access.c)
    target_compile_definitions(maul-ui-window PRIVATE
        MUI_WINDOW_UIA=$<BOOL:${MAUL_UI_UIA}>
        MUI_WINDOW_NS=$<BOOL:${MAUL_UI_NSACCESSIBILITY}>
        MUI_WINDOW_UIKIT=$<BOOL:${MAUL_UI_UIACCESSIBILITY}>
        MUI_WINDOW_ANDROID=$<BOOL:${MAUL_UI_ANDROID_ACCESSIBILITY}>
        MUI_WINDOW_ARIA=$<BOOL:${MAUL_UI_ARIA}>
        MUI_WINDOW_ATSPI=$<BOOL:${MAUL_UI_ATSPI}>)
endif()
add_library(maul-ui-window::maul-ui-window ALIAS maul-ui-window)
target_include_directories(maul-ui-window PUBLIC
    $<BUILD_INTERFACE:${PROJECT_SOURCE_DIR}/window/include>
    $<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}>)
target_link_libraries(maul-ui-window PUBLIC maul-ui maul-window::maul-window)
maul_apply_flags(maul-ui-window)
# Under Visual Studio's ClangCL toolset this target, made after Maul
# Window is fetched, was compiled below C23 though its C_STANDARD is 23,
# while the core, made before, was not. The family's maul_apply_flags
# asks C23 through $<COMPILE_LANGUAGE:C>, which did not reach this
# target there (CI of maul-ui d6ee815); the plain option does.
if(MSVC)
    target_compile_options(maul-ui-window PRIVATE /clang:-std=c23)
endif()
