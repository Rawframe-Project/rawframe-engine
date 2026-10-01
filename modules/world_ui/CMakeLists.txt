# The UI as World data (ADR-0034, D376): `rawframe.ui.Node` components of
# each local player's mirrored World laid out in the player's view as a
# `rawframe.ui` tree and drawn as its command list. Client only: the
# dedicated server carries the components as plain values and links none of
# this.
rawframe_module(NAME world_ui
    SOURCES src/world_ui.cpp src/participant.cpp
    DEPS ui view game_textures assets game_content content texture graph world_localization localization world_replication world_kest kest world composition schema execution diagnostics result base
)

rawframe_module_tests(NAME world_ui SOURCES tests/world_ui_test.cpp)
if(TARGET rawframe_world_ui_tests)
    target_compile_definitions(rawframe_world_ui_tests PRIVATE
        RAWFRAME_UI_FONTS="${PROJECT_SOURCE_DIR}/modules/ui/tests/fonts/")
endif()
