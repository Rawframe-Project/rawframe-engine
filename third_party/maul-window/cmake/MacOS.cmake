# The macOS backend (mwin-0024), in Objective-C with manual retain and
# release, as Maul RHI's Metal driver: AppKit for windows and input,
# Carbon for the keyboard layouts, IOKit to keep the display awake and
# read the power source, QuartzCore for the views' CAMetalLayers,
# UniformTypeIdentifiers for the dialogs' filters, GameController and
# CoreHaptics for the gamepads.

enable_language(OBJC)
set(MWIN_MACOS_SOURCES
    src/apple_locale.m
    src/apple_text.m
    src/backend_macos.m
    src/macos_chrome.m
    src/macos_cursor.m
    src/macos_dialog.m
    src/macos_drop.m
    src/macos_icon.m
    src/macos_keys.m
    src/macos_message_box.m
    src/macos_output.m
    src/macos_owned.m
    src/macos_pen.m
    src/macos_services.m
    src/macos_system.m
    src/macos_text.m
    src/macos_view.m
    src/macos_window.m)
if(MAUL_WINDOW_GAMEPAD)
    list(APPEND MWIN_MACOS_SOURCES src/apple_pad.m src/apple_rumble.m)
    target_link_libraries(maul-window PRIVATE "-framework CoreHaptics"
        "-framework GameController")
endif()
target_sources(maul-window PRIVATE ${MWIN_MACOS_SOURCES})
target_compile_definitions(maul-window PRIVATE MAUL_WINDOW_MACOS)
set_target_properties(maul-window PROPERTIES
    OBJC_STANDARD 23
    OBJC_STANDARD_REQUIRED ON
    OBJC_EXTENSIONS OFF
    OBJC_VISIBILITY_PRESET hidden)
target_compile_options(maul-window PRIVATE
    $<$<COMPILE_LANGUAGE:OBJC>:-fno-objc-arc -Wmissing-prototypes>)
target_link_libraries(maul-window PRIVATE "-framework AppKit" "-framework Carbon"
    "-framework IOKit" "-framework QuartzCore" "-framework UniformTypeIdentifiers")
