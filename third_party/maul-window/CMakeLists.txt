cmake_minimum_required(VERSION 3.25)

# The library's identity. The family's CMake modules and tools read these.
set(MAUL_LIBRARY maul-window)
set(MAUL_API_PREFIX mwin)
set(MAUL_MACRO_PREFIX MWIN)
set(MAUL_TITLE "Maul Window")
set(MAUL_PREFIX MAUL_WINDOW)

# The oldest macOS the backend runs on (mwin-0024), unless the build or
# the project including the library asks for another; the compiler then
# refuses a later API used without a check. A toolchain file says its
# own (the iOS simulator's, cmake/ios-simulator.cmake, iOS 15).
if(CMAKE_SOURCE_DIR STREQUAL CMAKE_CURRENT_SOURCE_DIR AND NOT DEFINED CMAKE_OSX_DEPLOYMENT_TARGET
   AND NOT CMAKE_TOOLCHAIN_FILE)
    if(CMAKE_SYSTEM_NAME STREQUAL "iOS")
        set(CMAKE_OSX_DEPLOYMENT_TARGET "15.0" CACHE STRING "The oldest iOS to run on")
    else()
        set(CMAKE_OSX_DEPLOYMENT_TARGET "11.0" CACHE STRING "The oldest macOS to run on")
    endif()
endif()

include(cmake/ReadVersion.cmake)
maul_read_version(${CMAKE_CURRENT_SOURCE_DIR}/include/maul-window/base.h MWIN MAUL_VERSION)
project(maul-window
    VERSION ${MAUL_VERSION}
    DESCRIPTION "Windows, input, gamepads and platform services"
    LANGUAGES C)

option(MAUL_WINDOW_BUILD_TESTS "Build the test suites" ${PROJECT_IS_TOP_LEVEL})
option(MAUL_WINDOW_BUILD_BENCH "Build the benchmarks" ${PROJECT_IS_TOP_LEVEL})
option(MAUL_WINDOW_BUILD_SAMPLES "Build the samples" ${PROJECT_IS_TOP_LEVEL})
option(MAUL_WINDOW_BUILD_SHARED "Build a shared library instead of a static one" OFF)
option(MAUL_WINDOW_INSTALL "Generate install rules" ${PROJECT_IS_TOP_LEVEL})
option(MAUL_WINDOW_WERROR "Treat compiler warnings as errors" OFF)
option(MAUL_WINDOW_SANITIZE "Instrument with AddressSanitizer and UBSan" OFF)
option(MAUL_WINDOW_TSAN "Instrument with ThreadSanitizer" OFF)
option(MAUL_WINDOW_COVERAGE "Instrument for gcov coverage" OFF)
option(MAUL_WINDOW_FUZZ "Build the libFuzzer targets (Clang, with the test backend)" OFF)
option(MAUL_WINDOW_TEST_BACKEND "Build the headless test backend (mwin_backendTest)" ${MAUL_WINDOW_BUILD_TESTS})
if(CMAKE_SYSTEM_NAME STREQUAL "Linux")
    set(MAUL_WINDOW_LINUX ON)
else()
    set(MAUL_WINDOW_LINUX OFF)
endif()
option(MAUL_WINDOW_WAYLAND "Build the Wayland backend" ${MAUL_WINDOW_LINUX})
option(MAUL_WINDOW_X11 "Build the X11 backend" ${MAUL_WINDOW_LINUX})
option(MAUL_WINDOW_WIN32 "Build the Win32 backend" ${WIN32})
if(CMAKE_SYSTEM_NAME STREQUAL "Darwin")
    set(MAUL_WINDOW_MACOS_DEFAULT ON)
else()
    set(MAUL_WINDOW_MACOS_DEFAULT OFF)
endif()
option(MAUL_WINDOW_MACOS "Build the macOS backend" ${MAUL_WINDOW_MACOS_DEFAULT})
if(CMAKE_SYSTEM_NAME STREQUAL "iOS")
    set(MAUL_WINDOW_IOS_DEFAULT ON)
else()
    set(MAUL_WINDOW_IOS_DEFAULT OFF)
endif()
option(MAUL_WINDOW_IOS "Build the iOS backend" ${MAUL_WINDOW_IOS_DEFAULT})
if(CMAKE_SYSTEM_NAME STREQUAL "Android")
    set(MAUL_WINDOW_ANDROID_DEFAULT ON)
else()
    set(MAUL_WINDOW_ANDROID_DEFAULT OFF)
endif()
option(MAUL_WINDOW_ANDROID "Build the Android backend" ${MAUL_WINDOW_ANDROID_DEFAULT})
# The web: Emscripten's, or a plain WebAssembly toolchain's (mwin-0022).
if(EMSCRIPTEN OR CMAKE_SYSTEM_NAME STREQUAL "WASI")
    set(MAUL_WINDOW_WEB_DEFAULT ON)
else()
    set(MAUL_WINDOW_WEB_DEFAULT OFF)
endif()
option(MAUL_WINDOW_WEB "Build the web backend" ${MAUL_WINDOW_WEB_DEFAULT})
option(MAUL_WINDOW_GAMEPAD "Build the gamepad component" ON)

# Maul Unicode validates every string that crosses the library: the
# installed package when there is one, otherwise the pinned release,
# built and installed along. It comes before the family modules below,
# whose copies then serve this project.
set(MAUL_WINDOW_UNICODE_VERSION 0.3.0)
find_package(maul-unicode ${MAUL_WINDOW_UNICODE_VERSION} CONFIG QUIET)
if(NOT maul-unicode_FOUND)
    include(FetchContent)
    set(MAUL_UNICODE_INSTALL ${MAUL_WINDOW_INSTALL})
    if(MAUL_WINDOW_BUILD_SHARED)
        # Its static library goes into ours.
        set(CMAKE_POSITION_INDEPENDENT_CODE ON)
    endif()
    FetchContent_Declare(maul-unicode
        GIT_REPOSITORY https://github.com/Rawframe-Project/maul-unicode.git
        GIT_TAG v${MAUL_WINDOW_UNICODE_VERSION}
        GIT_SHALLOW TRUE)
    FetchContent_MakeAvailable(maul-unicode)
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

if(MAUL_WINDOW_BUILD_SHARED)
    set(MAUL_LIBRARY_KIND SHARED)
else()
    set(MAUL_LIBRARY_KIND STATIC)
endif()
add_library(maul-window ${MAUL_LIBRARY_KIND}
    src/allocator.c
    src/base.c
    src/clipboard.c
    src/clipboard_data.c
    src/context.c
    src/dialog.c
    src/drop.c
    src/edid.c
    src/file_list.c
    src/accessibility.c
    src/chrome.c
    src/icon.c
    src/cursor.c
    src/input.c
    src/key_reach.c
    src/message_box.c
    src/monitor.c
    src/native.c
    src/pad.c
    src/preedit_fit.c
    src/queue.c
    src/selection_reads.c
    src/services.c
    src/system.c
    src/text.c
    src/uri_list.c
    src/utf8.c
    src/window.c)
# Linux's message box runs zenity or kdialog, and its services xdg-open
# and the session bus (libdbus-1, opened at run time); posix_spawn is
# POSIX.
if(MAUL_WINDOW_WAYLAND OR MAUL_WINDOW_X11)
    set(MWIN_POSIX_SOURCES src/dbus_api.c src/linux_bus.c src/linux_dialog.c src/linux_inhibit.c
        src/linux_locale.c src/linux_power.c src/linux_services.c
        src/linux_settings.c src/linux_zenity.c src/posix_message_box.c)
    target_sources(maul-window PRIVATE ${MWIN_POSIX_SOURCES})
    set_source_files_properties(${MWIN_POSIX_SOURCES} PROPERTIES
        COMPILE_DEFINITIONS _POSIX_C_SOURCE=200809L)
endif()
if(MAUL_WINDOW_TEST_BACKEND)
    target_sources(maul-window PRIVATE src/backend_test.c src/backend_test_services.c
        src/backend_test_text.c)
    target_compile_definitions(maul-window PRIVATE MAUL_WINDOW_TEST_BACKEND)
endif()
# The gamepad component: its functions, and the backends' gamepad code.
if(MAUL_WINDOW_GAMEPAD)
    target_sources(maul-window PRIVATE src/gamepad.c)
    target_compile_definitions(maul-window PRIVATE MAUL_WINDOW_GAMEPAD)
    # The pads a runtime lists and reads: Windows.Gaming.Input's and
    # GameController's (macOS and iOS), and a test's stand-in on every
    # platform.
    if(MAUL_WINDOW_WIN32 OR MAUL_WINDOW_MACOS OR MAUL_WINDOW_IOS OR MAUL_WINDOW_TEST_BACKEND)
        target_sources(maul-window PRIVATE src/pad_tracker.c)
    endif()
endif()
maul_configure_library(maul-window)
target_link_libraries(maul-window PRIVATE maul-unicode::maul-unicode)
if(NOT WIN32)
    target_link_libraries(maul-window PRIVATE m)
    set(MAUL_PKG_LIBS_PRIVATE "-lm")
endif()
if(MAUL_WINDOW_WAYLAND OR MAUL_WINDOW_X11)
    include(cmake/Xkb.cmake)
endif()
if(MAUL_WINDOW_WAYLAND)
    include(cmake/Wayland.cmake)
endif()
if(MAUL_WINDOW_X11)
    include(cmake/X11.cmake)
endif()
if(MAUL_WINDOW_WIN32)
    include(cmake/Win32.cmake)
endif()
if(MAUL_WINDOW_MACOS)
    include(cmake/MacOS.cmake)
endif()
if(MAUL_WINDOW_IOS)
    include(cmake/IOS.cmake)
endif()
if(MAUL_WINDOW_ANDROID)
    include(cmake/Android.cmake)
endif()
if(MAUL_WINDOW_WEB)
    include(cmake/Web.cmake)
endif()
set(MAUL_PACKAGE_DEPENDENCIES "find_dependency(maul-unicode ${MAUL_WINDOW_UNICODE_VERSION})")
set(MAUL_PKG_REQUIRES_PRIVATE "maul-unicode >= ${MAUL_WINDOW_UNICODE_VERSION}")

if(MAUL_WINDOW_BUILD_TESTS)
    enable_testing()
    maul_add_test(base)
    # The guide's opening program, built only (family record 0019).
    maul_add_test(guide_hello MANUAL)
    if(MAUL_WINDOW_TEST_BACKEND)
        maul_add_test(window)
        maul_add_test(input)
        maul_add_test(lifecycle)
        maul_add_test(monitor)
        maul_add_test(system)
        maul_add_test(ime)
        maul_add_test(properties)
        maul_add_test(clipboard)
        maul_add_test(clipboard_data)
        maul_add_test(drop)
        maul_add_test(services)
        maul_add_test(dialog)
        maul_add_test(icon WHITEBOX)
        maul_add_test(cursor WHITEBOX)
        maul_add_test(owned)
        maul_add_test(chrome WHITEBOX)
        maul_add_test(accessibility WHITEBOX)
        maul_add_test(key_reach WHITEBOX)
    maul_add_test(edid WHITEBOX)
        maul_add_test(text WHITEBOX)
        maul_add_test(file_list WHITEBOX)
        maul_add_test(selection WHITEBOX)
        # Android's pointers as records, on every platform: elsewhere the test
        # builds the backend's translator itself.
        maul_add_test(android_motion WHITEBOX)
        if(TARGET test_android_motion AND NOT MAUL_WINDOW_ANDROID)
            target_sources(test_android_motion PRIVATE ${PROJECT_SOURCE_DIR}/src/android_motion.c)
        endif()
        # The names of Android's document copies, on every platform.
        maul_add_test(android_name WHITEBOX)
        if(TARGET test_android_name AND NOT MAUL_WINDOW_ANDROID)
            target_sources(test_android_name PRIVATE ${PROJECT_SOURCE_DIR}/src/android_name.c)
            target_link_libraries(test_android_name PRIVATE maul-unicode::maul-unicode)
        endif()
        if(MAUL_WINDOW_GAMEPAD)
            maul_add_test(gamepad)
            # The runtime pads' tracking against a stand-in runtime.
            maul_add_test(pad_tracker WHITEBOX)
            # Android's gamepad layouts as records, on every platform.
            maul_add_test(android_pad_map WHITEBOX)
            if(TARGET test_android_pad_map AND NOT MAUL_WINDOW_ANDROID)
                target_sources(test_android_pad_map PRIVATE
                    ${PROJECT_SOURCE_DIR}/src/android_pad_map.c)
            endif()
        endif()
    endif()
    if(MAUL_WINDOW_WAYLAND)
        # Needs a compositor: skipped without WAYLAND_DISPLAY.
        maul_add_test(wayland POSIX)
        target_compile_definitions(test_wayland PRIVATE _POSIX_C_SOURCE=200809L)
        set_tests_properties(wayland PROPERTIES SKIP_RETURN_CODE 77)
        mwin_add_wayland_server_test(wayland_input)
        mwin_add_wayland_server_test(wayland_clipboard)
        mwin_add_wayland_server_test(wayland_clipboard_data)
        mwin_add_wayland_server_test(wayland_drop)
        mwin_add_wayland_server_test(wayland_services)
        mwin_add_wayland_server_test(wayland_icon)
        mwin_add_wayland_server_test(wayland_owned)
        mwin_add_wayland_server_test(wayland_pen)
        mwin_add_wayland_server_test(wayland_chrome)
        mwin_add_wayland_server_test(wayland_focus)
        mwin_add_wayland_server_test(wayland_monitor)
        mwin_add_wayland_server_test(wayland_monitor_facts)
        mwin_add_wayland_server_test(wayland_states)
        mwin_add_wayland_server_test(wayland_text)
        mwin_add_wayland_server_test(wayland_seat)
        mwin_add_wayland_server_test(wayland_theme)
        # The pipes other clients write into, read against their limits.
        maul_add_test(wayland_pipe WHITEBOX POSIX)
        if(TARGET test_wayland_pipe)
            target_compile_definitions(test_wayland_pipe PRIVATE _POSIX_C_SOURCE=200809L)
        endif()
    endif()
    if(MAUL_WINDOW_X11)
        # Needs an X server: skipped without DISPLAY.
        maul_add_test(x11 POSIX)
        target_compile_definitions(test_x11 PRIVATE _POSIX_C_SOURCE=200809L)
        set_tests_properties(x11 PROPERTIES SKIP_RETURN_CODE 77)
        # Drives the X server through XTEST, which the test links.
        pkg_check_modules(MWIN_TEST_XCB REQUIRED IMPORTED_TARGET xcb xcb-xtest xcb-xfixes)
        maul_add_test(x11_input POSIX)
        target_compile_definitions(test_x11_input PRIVATE _POSIX_C_SOURCE=200809L)
        target_link_libraries(test_x11_input PRIVATE PkgConfig::MWIN_TEST_XCB)
        set_tests_properties(x11_input PROPERTIES SKIP_RETURN_CODE 77)
        # Another client of the test's own connection shares CLIPBOARD.
        maul_add_test(x11_clipboard POSIX)
        target_compile_definitions(test_x11_clipboard PRIVATE _POSIX_C_SOURCE=200809L)
        target_link_libraries(test_x11_clipboard PRIVATE PkgConfig::MWIN_TEST_XCB)
        set_tests_properties(x11_clipboard PROPERTIES SKIP_RETURN_CODE 77)
        # Its data and PRIMARY likewise.
        maul_add_test(x11_clipboard_data POSIX)
        target_compile_definitions(test_x11_clipboard_data PRIVATE _POSIX_C_SOURCE=200809L)
        target_link_libraries(test_x11_clipboard_data PRIVATE PkgConfig::MWIN_TEST_XCB)
        # Window icons read back by the test's own connection.
        maul_add_test(x11_icon POSIX)
        target_compile_definitions(test_x11_icon PRIVATE _POSIX_C_SOURCE=200809L)
        target_link_libraries(test_x11_icon PRIVATE PkgConfig::MWIN_TEST_XCB)
        set_tests_properties(x11_icon PROPERTIES SKIP_RETURN_CODE 77)
        # A drag source of the test's own connection, by XDND.
        maul_add_test(x11_drop POSIX)
        target_compile_definitions(test_x11_drop PRIVATE _POSIX_C_SOURCE=200809L)
        target_link_libraries(test_x11_drop PRIVATE PkgConfig::MWIN_TEST_XCB)
        set_tests_properties(x11_drop PROPERTIES SKIP_RETURN_CODE 77)
        # Reads at the edges of the clipboard's limit.
        maul_add_test(x11_clipboard_limits POSIX)
        target_compile_definitions(test_x11_clipboard_limits PRIVATE _POSIX_C_SOURCE=200809L)
        target_link_libraries(test_x11_clipboard_limits PRIVATE PkgConfig::MWIN_TEST_XCB)
        # Readers of large texts, more than are served at once.
        maul_add_test(x11_clipboard_readers POSIX)
        target_compile_definitions(test_x11_clipboard_readers PRIVATE _POSIX_C_SOURCE=200809L)
        target_link_libraries(test_x11_clipboard_readers PRIVATE PkgConfig::MWIN_TEST_XCB)
        # Window manager hints and states read back and set.
        maul_add_test(x11_hints POSIX)
        target_compile_definitions(test_x11_hints PRIVATE _POSIX_C_SOURCE=200809L)
        target_link_libraries(test_x11_hints PRIVATE PkgConfig::MWIN_TEST_XCB)
        # Owned windows and popups read back by the test's own connection.
        maul_add_test(x11_owned POSIX)
        target_compile_definitions(test_x11_owned PRIVATE _POSIX_C_SOURCE=200809L)
        target_link_libraries(test_x11_owned PRIVATE PkgConfig::MWIN_TEST_XCB)
        # Custom chrome driven through XTEST, read back from the root window.
        maul_add_test(x11_chrome POSIX)
        target_compile_definitions(test_x11_chrome PRIVATE _POSIX_C_SOURCE=200809L)
        target_link_libraries(test_x11_chrome PRIVATE PkgConfig::MWIN_TEST_XCB)
        # Monitors set and deleted through RandR by the test's own connection.
        pkg_check_modules(MWIN_TEST_RANDR REQUIRED IMPORTED_TARGET xcb xcb-randr)
        maul_add_test(x11_monitor POSIX)
        target_compile_definitions(test_x11_monitor PRIVATE _POSIX_C_SOURCE=200809L)
        target_link_libraries(test_x11_monitor PRIVATE PkgConfig::MWIN_TEST_RANDR)
        # The facts an output's properties give.
        maul_add_test(x11_monitor_facts POSIX)
        target_compile_definitions(test_x11_monitor_facts PRIVATE _POSIX_C_SOURCE=200809L)
        target_link_libraries(test_x11_monitor_facts PRIVATE PkgConfig::MWIN_TEST_RANDR m)
        # The wheel movement of scroll valuators.
        maul_add_test(x11_scroll WHITEBOX)
        # Which devices are pens, and the records their events make.
        maul_add_test(x11_pen WHITEBOX)
        # Which devices are touch screens, and the records their touches
        # make.
        maul_add_test(x11_touch WHITEBOX)
        # The scale of Xft.dpi.
        maul_add_test(x11_resources WHITEBOX)
        # The tests that move the X server's focus or pointer, or own its
        # selections, take turns.
        set_tests_properties(x11 x11_input x11_clipboard x11_clipboard_data
                             x11_clipboard_limits x11_clipboard_readers x11_hints x11_icon
                             x11_drop x11_owned x11_chrome x11_monitor x11_monitor_facts PROPERTIES
                             SKIP_RETURN_CODE 77
                             RESOURCE_LOCK x11-focus)
    endif()
    if(MAUL_WINDOW_WAYLAND OR MAUL_WINDOW_X11)
        # Stand-ins for zenity and kdialog on PATH.
        maul_add_test(message_box POSIX)
        target_compile_definitions(test_message_box PRIVATE _POSIX_C_SOURCE=200809L)
        # Linux input event codes as key codes.
        maul_add_test(evdev WHITEBOX)
        # A gamepad's battery from a sysfs tree of the test's own.
        if(MAUL_WINDOW_GAMEPAD)
            maul_add_test(linux_battery WHITEBOX)
            if(TARGET test_linux_battery)
                target_compile_definitions(test_linux_battery PRIVATE _POSIX_C_SOURCE=200809L)
            endif()
        endif()
        # The preferred locales the environment names.
        maul_add_test(linux_locale WHITEBOX)
        if(TARGET test_linux_locale)
            target_compile_definitions(test_linux_locale PRIVATE _POSIX_C_SOURCE=200809L)
        endif()
    endif()
    if(MAUL_WINDOW_X11)
        # xdg-open on PATH and a session bus of the test's own; X11, and
        # Wayland where there is a compositor too.
        maul_add_test(linux_services POSIX)
        target_compile_definitions(test_linux_services PRIVATE _POSIX_C_SOURCE=200809L)
        target_link_libraries(test_linux_services PRIVATE ${CMAKE_DL_LIBS})
        # Keeping awake over a session bus of the test's own.
        maul_add_test(linux_awake POSIX)
        target_compile_definitions(test_linux_awake PRIVATE _POSIX_C_SOURCE=200809L)
        target_link_libraries(test_linux_awake PRIVATE ${CMAKE_DL_LIBS})
        # File dialogs through a portal of the test's own, and zenity.
        maul_add_test(linux_dialog POSIX)
        target_compile_definitions(test_linux_dialog PRIVATE _POSIX_C_SOURCE=200809L)
        target_link_libraries(test_linux_dialog PRIVATE ${CMAKE_DL_LIBS})
        # The desktop's look and motion through a portal of the test's own.
        maul_add_test(linux_settings POSIX)
        target_compile_definitions(test_linux_settings PRIVATE _POSIX_C_SOURCE=200809L)
        target_link_libraries(test_linux_settings PRIVATE ${CMAKE_DL_LIBS})
        # The power through a portal and UPower of the test's own.
        maul_add_test(linux_power POSIX)
        target_compile_definitions(test_linux_power PRIVATE _POSIX_C_SOURCE=200809L)
        target_link_libraries(test_linux_power PRIVATE ${CMAKE_DL_LIBS})
        # Input methods through Fcitx 5 and IBus of the test's own (mwin-0030).
        maul_add_test(x11_ime POSIX)
        target_compile_definitions(test_x11_ime PRIVATE _POSIX_C_SOURCE=200809L)
        target_link_libraries(test_x11_ime PRIVATE ${CMAKE_DL_LIBS} PkgConfig::MWIN_TEST_XCB)
        set(MWIN_BUS_TESTS linux_services linux_awake linux_dialog linux_settings linux_power
            x11_ime)
        set_tests_properties(${MWIN_BUS_TESTS} PROPERTIES SKIP_RETURN_CODE 77)
        # It moves the X server's focus, as the X11 tests do.
        set_tests_properties(x11_ime PROPERTIES RESOURCE_LOCK x11-focus)
        if(MAUL_WINDOW_TSAN)
            set_tests_properties(${MWIN_BUS_TESTS} PROPERTIES
                ENVIRONMENT "TSAN_OPTIONS=suppressions=${PROJECT_SOURCE_DIR}/test/tsan.supp")
        endif()
    endif()
    if((MAUL_WINDOW_WAYLAND OR MAUL_WINDOW_X11) AND MAUL_WINDOW_GAMEPAD)
        # Numbered controls through their mapping.
        maul_add_test(pad_map WHITEBOX)
        # Makes virtual gamepads through uinput: skipped without it or a
        # display.
        maul_add_test(linux_gamepad POSIX)
        target_compile_definitions(test_linux_gamepad PRIVATE _GNU_SOURCE)
        set_tests_properties(linux_gamepad PROPERTIES SKIP_RETURN_CODE 77 RESOURCE_LOCK uinput)
        # Where both backends are built, the X11 one too, Wayland's
        # display hidden; the pads of one run would be the other's.
        if(MAUL_WINDOW_WAYLAND AND MAUL_WINDOW_X11)
            add_test(NAME linux_gamepad_x11 COMMAND test_linux_gamepad)
            set_tests_properties(linux_gamepad_x11 PROPERTIES SKIP_RETURN_CODE 77
                                 ENVIRONMENT_MODIFICATION "WAYLAND_DISPLAY=unset:"
                                 RESOURCE_LOCK uinput)
        endif()
    endif()
    if(MAUL_WINDOW_WEB)
        # Runs in headless Chrome through puppeteer, which the runner
        # finds through MWIN_NODE_MODULES: skipped without it. Without
        # Emscripten a test is a reactor whose main the runner calls, with
        # its imports beside it (mwin-0022).
        function(mwin_add_web_test name)
            maul_add_test(${name} MANUAL WHITEBOX)
            add_test(NAME ${name} COMMAND node ${PROJECT_SOURCE_DIR}/test/web_runner.cjs
                     $<TARGET_FILE:test_${name}>)
            set_tests_properties(${name} PROPERTIES SKIP_RETURN_CODE 77)
            if(NOT EMSCRIPTEN)
                target_link_options(test_${name} PRIVATE -mexec-model=reactor -Wl,--stack-first -Wl,--export-table
                                    -Wl,--export=main)
                mwin_web_glue(test_${name}_glue ${PROJECT_BINARY_DIR}/test_${name}.mjs test/test_${name}.c)
                add_dependencies(test_${name} test_${name}_glue)
            endif()
        endfunction()
        mwin_add_web_test(web)
        mwin_add_web_test(web_input)
        mwin_add_web_test(web_host)
        mwin_add_web_test(web_ime)
        mwin_add_web_test(web_lifecycle)
        if(MAUL_WINDOW_GAMEPAD)
            mwin_add_web_test(web_gamepad)
        endif()
        mwin_add_web_test(web_clipboard)
        mwin_add_web_test(web_services)
        mwin_add_web_test(web_drop)
        mwin_add_web_test(web_locales)
        mwin_add_web_test(web_power)
    endif()
    if(MAUL_WINDOW_MACOS)
        # Needs the session's window server, which CI's macOS runners have.
        maul_add_test(macos)
        if(MAUL_WINDOW_GAMEPAD)
            maul_add_test(macos_gamepad)
        endif()
        maul_add_test(macos_services)
        target_link_libraries(test_macos_services PRIVATE "-framework CoreFoundation"
                              "-framework IOKit")
        # Objective-C, to make the NSEvents AppKit would deliver and call
        # the view as an input method would.
        foreach(name macos_input macos_ime macos_pen macos_drop macos_dialog macos_system
                     macos_owned macos_chrome macos_access)
            add_executable(test_${name} test/test_${name}.m)
            target_link_libraries(test_${name} PRIVATE maul-window "-framework AppKit"
                                  "-framework Carbon" "-framework UniformTypeIdentifiers")
            maul_apply_flags(test_${name})
            set_target_properties(test_${name} PROPERTIES OBJC_STANDARD 23
                                  OBJC_STANDARD_REQUIRED ON OBJC_EXTENSIONS OFF)
            target_compile_options(test_${name} PRIVATE -fno-objc-arc)
            add_test(NAME ${name} COMMAND test_${name})
        endforeach()
        set_tests_properties(macos macos_input macos_ime macos_pen macos_services macos_drop
                             macos_dialog macos_system macos_owned macos_chrome macos_access
                             PROPERTIES TIMEOUT 60)
        if(MAUL_WINDOW_GAMEPAD)
            set_tests_properties(macos_gamepad PROPERTIES TIMEOUT 60)
        endif()
    endif()
    if(MAUL_WINDOW_ANDROID AND DEFINED ENV{ANDROID_HOME})
        # Applications in the emulator, built by tools/build_android_app.sh
        # without Gradle and run by tools/run_android_app.sh.
        set(MWIN_ANDROID_TESTS android android_input android_cursor android_ime android_pad
            android_services android_dialog android_drop android_system android_access)
        foreach(name ${MWIN_ANDROID_TESTS})
            set(package maul.window.tests.${name})
            add_library(test_${name} SHARED test/test_${name}.c)
            target_link_libraries(test_${name} PRIVATE maul-window android)
            target_compile_definitions(test_${name} PRIVATE MWIN_TEST_PACKAGE="${package}")
            maul_apply_flags(test_${name})
            file(GLOB_RECURSE MWIN_JAVA_SOURCES ${PROJECT_SOURCE_DIR}/java/*.java
                 ${PROJECT_SOURCE_DIR}/test/android/java/*.java
                 ${PROJECT_SOURCE_DIR}/test/android/res/*.xml)
            add_custom_command(OUTPUT ${PROJECT_BINARY_DIR}/test_${name}.apk
                COMMAND ${CMAKE_COMMAND} -E env ANDROID_HOME=$ENV{ANDROID_HOME}
                        ${PROJECT_SOURCE_DIR}/tools/build_android_app.sh
                        ${PROJECT_BINARY_DIR}/test_${name}.apk $<TARGET_FILE:test_${name}>
                        ${package} ${ANDROID_ABI}
                DEPENDS test_${name} ${MWIN_JAVA_SOURCES}
                        ${PROJECT_SOURCE_DIR}/test/android/AndroidManifest.xml
                        ${PROJECT_SOURCE_DIR}/java/proguard-rules.pro
                        ${PROJECT_SOURCE_DIR}/test/android/proguard-rules.pro
                        ${PROJECT_SOURCE_DIR}/tools/build_android_app.sh
                VERBATIM)
            add_custom_target(test_${name}_apk ALL DEPENDS ${PROJECT_BINARY_DIR}/test_${name}.apk)
            add_test(NAME ${name} COMMAND ${PROJECT_SOURCE_DIR}/tools/run_android_app.sh
                     ${PROJECT_BINARY_DIR}/test_${name}.apk ${package})
            set_tests_properties(${name} PROPERTIES TIMEOUT 240 RESOURCE_LOCK emulator)
        endforeach()
    endif()
    if(MAUL_WINDOW_IOS)
        # Applications in the simulator, run by tools/run_ios_app.sh; ios_input,
        # ios_keys and ios_ime hand the view touches, recognizers, presses, text
        # and compositions as UIKit would.
        set(MWIN_IOS_TESTS ios ios_input ios_keys ios_ime ios_services ios_drop ios_dialog
            ios_system)
        if(MAUL_WINDOW_GAMEPAD)
            list(APPEND MWIN_IOS_TESTS ios_gamepad)
        endif()
        foreach(name ${MWIN_IOS_TESTS})
            string(REPLACE "_" "-" bundle ${name})
            add_executable(test_${name} MACOSX_BUNDLE test/test_${name}.m)
            target_link_libraries(test_${name} PRIVATE maul-window "-framework UIKit")
            maul_apply_flags(test_${name})
            set_target_properties(test_${name} PROPERTIES OBJC_STANDARD 23
                OBJC_STANDARD_REQUIRED ON OBJC_EXTENSIONS OFF
                MACOSX_BUNDLE_INFO_PLIST ${PROJECT_SOURCE_DIR}/test/ios/Info.plist.in
                MACOSX_BUNDLE_GUI_IDENTIFIER org.maul.window.test.${bundle}
                MACOSX_BUNDLE_BUNDLE_NAME test_${name}
                MACOSX_BUNDLE_BUNDLE_VERSION ${PROJECT_VERSION}
                MACOSX_BUNDLE_SHORT_VERSION_STRING ${PROJECT_VERSION})
            target_compile_options(test_${name} PRIVATE -fno-objc-arc)
            add_test(NAME ${name} COMMAND ${PROJECT_SOURCE_DIR}/tools/run_ios_app.sh
                     $<TARGET_BUNDLE_DIR:test_${name}>)
            set_tests_properties(${name} PROPERTIES TIMEOUT 240 RESOURCE_LOCK simulator)
        endforeach()
        if(MAUL_WINDOW_GAMEPAD)
            target_link_libraries(test_ios_gamepad PRIVATE "-framework GameController")
        endif()
    endif()
    if(MAUL_WINDOW_WIN32)
        maul_add_test(win32)
        target_link_libraries(test_win32 PRIVATE user32 shcore)
        # Drives the desktop through SendInput.
        maul_add_test(win32_input)
        target_link_libraries(test_win32_input PRIVATE user32)
        # Injects touch and pen: skipped where Windows cannot (wine).
        maul_add_test(win32_pointer)
        target_link_libraries(test_win32_pointer PRIVATE user32)
        set_tests_properties(win32_pointer PROPERTIES SKIP_RETURN_CODE 77)
        if(MAUL_WINDOW_GAMEPAD AND MAUL_WINDOW_TEST_BACKEND)
            # The XInput gamepads against a stand-in for XInput.
            maul_add_test(win32_gamepad WHITEBOX)
            # Generic gamepads against a stand-in for Raw Input and the HID
            # parser.
            maul_add_test(win32_hid WHITEBOX)
            # Windows.Gaming.Input itself, where the system has it.
            maul_add_test(win32_wgi WHITEBOX)
            target_link_libraries(test_win32_wgi PRIVATE ole32)
        endif()
        # Composes through the input context: skipped with no input method.
        maul_add_test(win32_ime)
        target_link_libraries(test_win32_ime PRIVATE user32 imm32)
        set_tests_properties(win32_ime PROPERTIES SKIP_RETURN_CODE 77)
        # Changes the user's settings for a moment, and puts them back.
        maul_add_test(win32_system)
        target_link_libraries(test_win32_system PRIVATE user32 advapi32 dwmapi)
        # What Windows reports, read without its settings.
        maul_add_test(win32_system_facts WHITEBOX)
        # What Windows tells of a monitor, read without one.
        maul_add_test(win32_monitor_facts WHITEBOX)
        # A composition shaped without an input method.
        maul_add_test(win32_ime_text WHITEBOX)
        # One desktop: the focus, the pointer and the settings are not
        # shared.
        # The drop target driven as OLE would, and WM_DROPFILES.
        maul_add_test(win32_drop WHITEBOX)
        target_link_libraries(test_win32_drop PRIVATE user32 ole32 shell32 uuid)
        # The system clipboard, shared by the whole desktop.
        maul_add_test(win32_clipboard)
        target_link_libraries(test_win32_clipboard PRIVATE user32)
        # Keeps the display awake, and points http at itself for a moment.
        maul_add_test(win32_services)
        target_link_libraries(test_win32_services PRIVATE user32 advapi32 shell32)
        # Window icons read back from Windows.
        maul_add_test(win32_icon)
        target_link_libraries(test_win32_icon PRIVATE user32 gdi32)
        # The common item dialog, driven from the program's frames.
        maul_add_test(win32_dialog)
        target_link_libraries(test_win32_dialog PRIVATE user32)
        # Owned windows and popups placed against their owners.
        maul_add_test(win32_owned)
        target_link_libraries(test_win32_owned PRIVATE user32)
        # Custom chrome and hit regions read back from Windows.
        maul_add_test(win32_chrome)
        target_link_libraries(test_win32_chrome PRIVATE user32)
        # The touch keyboard shown and hidden through the window's InputPane.
        maul_add_test(win32_touch_keyboard WHITEBOX)
        target_link_libraries(test_win32_touch_keyboard PRIVATE user32 uuid)
        # Styles, limits, ratio and opacity read back from Windows.
        maul_add_test(win32_window_props)
        target_link_libraries(test_win32_window_props PRIVATE user32)
        # Message boxes answered from a second thread.
        maul_add_test(win32_message_box)
        target_link_libraries(test_win32_message_box PRIVATE user32)
        # The accessibility root handed to UI Automation.
        maul_add_test(win32_accessibility)
        target_link_libraries(test_win32_accessibility PRIVATE user32 ole32 oleaut32 uuid)
        set_tests_properties(win32 win32_input win32_pointer win32_ime win32_system
                             win32_clipboard win32_drop win32_services win32_dialog win32_owned
                             win32_chrome win32_touch_keyboard win32_message_box
                             win32_window_props
                             PROPERTIES RESOURCE_LOCK desktop)
    endif()
    maul_add_header_tests()
endif()

# The fuzz targets: test/fuzz_<area>.c, run as fuzz_<area> <corpus dir>,
# over the parsers of bytes other programs and devices hand the library.
if(MAUL_WINDOW_FUZZ)
    if(NOT MAUL_WINDOW_TEST_BACKEND)
        message(FATAL_ERROR "The fuzz targets run on the test backend: set MAUL_WINDOW_TEST_BACKEND")
    endif()
    # The library is instrumented too: its coverage guides libFuzzer, and
    # the sanitizers watch its code, not only the targets'.
    target_compile_options(maul-window PRIVATE -fsanitize=fuzzer-no-link,address,undefined)
    target_link_options(maul-window PUBLIC -fsanitize=address,undefined)
    set(MWIN_FUZZ_AREAS edid payload preedit)
    if(MAUL_WINDOW_WAYLAND OR MAUL_WINDOW_X11)
        list(APPEND MWIN_FUZZ_AREAS locale)
    endif()
    if(MAUL_WINDOW_X11)
        list(APPEND MWIN_FUZZ_AREAS scroll)
    endif()
    if(MAUL_WINDOW_GAMEPAD AND (MAUL_WINDOW_WAYLAND OR MAUL_WINDOW_X11))
        list(APPEND MWIN_FUZZ_AREAS pad)
    endif()
    foreach(area ${MWIN_FUZZ_AREAS})
        add_executable(fuzz_${area} test/fuzz_${area}.c)
        target_link_libraries(fuzz_${area} PRIVATE maul-window maul-unicode::maul-unicode)
        # The targets reach the library's own functions.
        target_include_directories(fuzz_${area} PRIVATE ${PROJECT_SOURCE_DIR}/src)
        maul_apply_flags(fuzz_${area})
        target_compile_options(fuzz_${area} PRIVATE -fsanitize=fuzzer,address,undefined)
        target_link_options(fuzz_${area} PRIVATE -fsanitize=fuzzer,address,undefined)
    endforeach()
    if(MAUL_WINDOW_WAYLAND OR MAUL_WINDOW_X11)
        target_compile_definitions(fuzz_payload PRIVATE MAUL_WINDOW_ZENITY)
    endif()
    if(MAUL_WINDOW_X11)
        target_include_directories(fuzz_scroll SYSTEM PRIVATE ${MWIN_XCB_INCLUDE_DIRS})
    endif()
endif()

if(MAUL_WINDOW_BUILD_SAMPLES)
    add_subdirectory(samples)
endif()

if(MAUL_WINDOW_BUILD_BENCH AND EXISTS ${PROJECT_SOURCE_DIR}/bench/bench_main.c)
    add_subdirectory(bench)
endif()

if(MAUL_WINDOW_INSTALL)
    include(cmake/Install.cmake)
endif()
