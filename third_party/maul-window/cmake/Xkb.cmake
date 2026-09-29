# What the Linux backends share: the keyboard (evdev key codes, and
# libxkbcommon, opened at run time; building needs only its headers)
# and the gamepads.

find_package(PkgConfig REQUIRED)
pkg_check_modules(MWIN_XKBCOMMON QUIET xkbcommon>=1.0)
if(NOT MWIN_XKBCOMMON_FOUND)
    message(FATAL_ERROR "The Linux backends need the development files of xkbcommon (1.0 or "
        "later): install them (libxkbcommon-dev on Debian and Ubuntu) or configure with "
        "-DMAUL_WINDOW_WAYLAND=OFF -DMAUL_WINDOW_X11=OFF.")
endif()
pkg_get_variable(MWIN_XKBCOMMON_INCLUDE_DIR xkbcommon includedir)
set(MWIN_XKB_SOURCES
    src/evdev.c
    src/xkb_api.c
    src/xkb_keyboard.c)
target_sources(maul-window PRIVATE ${MWIN_XKB_SOURCES})
target_include_directories(maul-window SYSTEM PRIVATE ${MWIN_XKBCOMMON_INCLUDE_DIR})
# dlopen is POSIX, outside strict C.
set_source_files_properties(${MWIN_XKB_SOURCES} PROPERTIES
    COMPILE_DEFINITIONS _POSIX_C_SOURCE=200809L)
target_link_libraries(maul-window PRIVATE ${CMAKE_DL_LIBS})
if(CMAKE_DL_LIBS AND NOT MAUL_PKG_LIBS_PRIVATE MATCHES "-l${CMAKE_DL_LIBS}")
    string(APPEND MAUL_PKG_LIBS_PRIVATE " -l${CMAKE_DL_LIBS}")
endif()

# The gamepads the Linux backends share: evdev devices, with the
# mappings compiled from SDL_GameControllerDB.
if(MAUL_WINDOW_GAMEPAD)
    set(MWIN_LINUX_PAD_SOURCES
        src/linux_pad.c
        src/pad_db.c
        src/pad_map.c
        src/generated/pad_linux.c)
    target_sources(maul-window PRIVATE ${MWIN_LINUX_PAD_SOURCES})
    # inotify and the event clock are Linux's, outside strict C.
    set_source_files_properties(src/linux_pad.c PROPERTIES COMPILE_DEFINITIONS _GNU_SOURCE)
endif()
