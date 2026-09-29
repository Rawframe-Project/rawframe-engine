# The web backend, for Emscripten. It needs nothing beyond the browser:
# its page side is JavaScript in the library's objects (EM_JS), which
# the program's link takes in.

set(MWIN_WEB_SOURCES
    src/backend_web.c
    src/web_clipboard.c
    src/web_drop.c
    src/web_input.c
    src/web_message_box.c
    src/web_page.c
    src/web_services.c
    src/web_text.c
    src/web_window.c)
if(MAUL_WINDOW_GAMEPAD)
    list(APPEND MWIN_WEB_SOURCES src/web_pad.c)
endif()
target_sources(maul-window PRIVATE ${MWIN_WEB_SOURCES})
target_compile_definitions(maul-window PRIVATE MAUL_WINDOW_WEB)
