# The Win32 backend. It links the system libraries every Windows has:
# user32 for windows and input, shcore for monitor DPI, imm32 for input
# methods, advapi32 for the user's settings in the registry, dwmapi for
# the frame's theme, ole32 and shell32 for drag and drop.

set(MWIN_WIN32_SOURCES
    src/backend_win32.c
    src/win32_clipboard.c
    src/win32_dialog.c
    src/win32_drop.c
    src/win32_accessibility.c
    src/win32_icon.c
    src/win32_ime.c
    src/win32_message_box.c
    src/win32_input.c
    src/win32_output.c
    src/win32_pointer.c
    src/win32_services.c
    src/win32_system.c
    src/win32_window.c)
# XInput is loaded at run time: the gamepads need no library to link.
if(MAUL_WINDOW_GAMEPAD)
    list(APPEND MWIN_WIN32_SOURCES src/win32_pad.c src/win32_hid.c src/pad_db.c src/pad_map.c
        src/generated/pad_windows.c)
endif()
target_sources(maul-window PRIVATE ${MWIN_WIN32_SOURCES})
target_compile_definitions(maul-window PRIVATE MAUL_WINDOW_WIN32)
# Windows 10 1703 or later, for per-monitor DPI awareness version 2.
set_source_files_properties(${MWIN_WIN32_SOURCES} PROPERTIES
    COMPILE_DEFINITIONS "_WIN32_WINNT=0x0A00;WINVER=0x0A00;UNICODE;_UNICODE")
target_link_libraries(maul-window PRIVATE user32 shcore imm32 advapi32 dwmapi ole32 shell32)
string(APPEND MAUL_PKG_LIBS_PRIVATE " -luser32 -lshcore -limm32 -ladvapi32 -ldwmapi -lole32 -lshell32")
