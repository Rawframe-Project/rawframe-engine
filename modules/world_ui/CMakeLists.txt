# The UI as World data (ADR-0034, D376): `rawframe.ui.Node` components of
# each local player's mirrored World laid out in the player's view as a
# `rawframe.ui` tree and drawn as its command list. Client only: the
# dedicated server carries the components as plain values and links none of
# this.
rawframe_module(NAME world_ui
    SOURCES src/world_ui.cpp src/interaction.cpp src/keeper.cpp src/participant.cpp
    DEPS ui document view game_textures assets game_content content texture graph world_localization localization world_replication world_kest kest world composition schema execution diagnostics result base
)

# The player's settings are kept in a file where there are files (D626).
if(CMAKE_SYSTEM_NAME STREQUAL "WASI")
    rawframe_module_tests(NAME world_ui SOURCES tests/world_ui_test.cpp)
else()
    rawframe_module_tests(NAME world_ui SOURCES tests/world_ui_test.cpp tests/kept_test.cpp)
endif()
if(TARGET rawframe_world_ui_tests)
    target_compile_definitions(rawframe_world_ui_tests PRIVATE
        RAWFRAME_UI_FONTS="${RAWFRAME_TEST_FILES}/modules/ui/tests/fonts/")
endif()
