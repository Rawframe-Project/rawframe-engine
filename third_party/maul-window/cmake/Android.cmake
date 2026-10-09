# The Android backend (mwin-0026): NativeActivity's native surface, input
# queue and callbacks, and the choreographer's frames, all from libandroid;
# keys through Linux's input codes, as on Linux (evdev.c).
# The program's application also carries the library's Java activity
# (java/maul/window).

target_sources(maul-window PRIVATE
    src/android_access.c
    src/android_copy.c
    src/android_cursor.c
    src/android_dialog.c
    src/android_drop.c
    src/android_input.c
    src/android_motion.c
    src/android_name.c
    src/android_output.c
    src/android_services.c
    src/android_system.c
    src/android_text.c
    src/android_window.c
    src/backend_android.c
    src/evdev.c
    src/generated/android_keys.c)
target_compile_definitions(maul-window PRIVATE MAUL_WINDOW_ANDROID)
# Gamepads through the input queue and the library's Java helper.
if(MAUL_WINDOW_GAMEPAD)
    target_sources(maul-window PRIVATE src/android_pad.c src/android_pad_map.c)
endif()
target_link_libraries(maul-window PRIVATE android)
