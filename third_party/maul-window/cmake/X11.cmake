# The X11 backend (mwin-0006). libxcb and its extension
# libraries are opened at run time; building needs only their headers.

find_package(PkgConfig REQUIRED)
pkg_check_modules(MWIN_XCB QUIET xcb>=1.11 xcb-randr xcb-xkb xcb-cursor xcb-render
    xcb-xinput xkbcommon-x11)
if(NOT MWIN_XCB_FOUND)
    message(FATAL_ERROR "The X11 backend needs the development files of xcb (1.11 or later), "
        "xcb-randr, xcb-xkb, xcb-cursor, xcb-render, xcb-xinput and xkbcommon-x11: install "
        "them (libxcb1-dev, libxcb-randr0-dev, libxcb-xkb-dev, libxcb-cursor-dev, "
        "libxcb-render0-dev, libxcb-xinput-dev and libxkbcommon-x11-dev on Debian and Ubuntu) "
        "or configure with "
        "-DMAUL_WINDOW_X11=OFF.")
endif()

set(MWIN_X11_SOURCES
    src/backend_x11.c
    src/linux_ime.c
    src/x11_api.c
    src/x11_clipboard.c
    src/x11_clipboard_read.c
    src/x11_cursor.c
    src/x11_drop.c
    src/x11_chrome.c
    src/x11_icon.c
    src/x11_input.c
    src/x11_output.c
    src/x11_pen.c
    src/x11_resources.c
    src/x11_scroll.c
    src/x11_touch.c
    src/x11_window.c)
target_sources(maul-window PRIVATE ${MWIN_X11_SOURCES})
target_include_directories(maul-window SYSTEM PRIVATE ${MWIN_XCB_INCLUDE_DIRS})
target_compile_definitions(maul-window PRIVATE MAUL_WINDOW_X11)
# clock_gettime and dlopen are POSIX, outside strict C.
set_source_files_properties(${MWIN_X11_SOURCES} PROPERTIES
    COMPILE_DEFINITIONS _POSIX_C_SOURCE=200809L)
target_link_libraries(maul-window PRIVATE ${CMAKE_DL_LIBS})
if(CMAKE_DL_LIBS AND NOT MAUL_PKG_LIBS_PRIVATE MATCHES "-l${CMAKE_DL_LIBS}")
    string(APPEND MAUL_PKG_LIBS_PRIVATE " -l${CMAKE_DL_LIBS}")
endif()
# The library reaches XCB only through the loaded table.
if(MAUL_WINDOW_BUILD_SHARED AND NOT MAUL_WINDOW_SANITIZE AND NOT MAUL_WINDOW_TSAN)
    target_link_options(maul-window PRIVATE "LINKER:--no-undefined")
endif()
