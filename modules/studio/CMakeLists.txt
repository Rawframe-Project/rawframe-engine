# Studio's shell (ADR-0032, D435): a session on a game held in Studio's own
# process, shown as a UI of its own. Toolchain only; no client or server
# closure contains it.
rawframe_module(NAME studio
    SOURCES src/commands.cpp src/context_menu.cpp src/console.cpp src/console_panel.cpp src/data.cpp src/docking.cpp src/pane_windows.cpp src/network_panel.cpp src/documents.cpp src/editing.cpp src/copying.cpp src/export_dialog.cpp src/extension_commands.cpp src/hierarchy.cpp src/joining.cpp src/layers.cpp src/live.cpp src/hints.cpp src/operations.cpp src/panels.cpp src/participant.cpp src/play.cpp src/playing.cpp src/preferences.cpp src/project.cpp src/preferences_panel.cpp src/records.cpp src/reparenting.cpp src/selection.cpp src/viewing.cpp src/asset_files.cpp src/browser.cpp src/canvas.cpp src/timeline.cpp src/ui_preview.cpp src/environment_panel.cpp src/style_preview.cpp src/profiler_panel.cpp src/frame_panel.cpp src/blockout_editor.cpp src/capture_view.cpp src/changes_panel.cpp src/version_control.cpp src/mixer_panel.cpp src/scatter_editor.cpp src/sequence_editor.cpp src/terrain_editor.cpp src/tilemap_editor.cpp src/viewport.cpp src/views.cpp src/world_panel.cpp src/mode_palette.cpp src/layouts.cpp src/splitters.cpp src/start.cpp src/frame.cpp src/inspector.cpp
    DEPS authoring_session blockout scatter sequence scene schema terrain tilemap animation world_tooling render material font_import process view ui texture document composition execution diagnostics result base
)
# Studio's own fonts (IBM Plex Sans and Mono, SIL Open Font License 1.1,
# fonts/IBMPlex-LICENSE.txt; D915), and its icons (tools/make_studio_icons.py).
rawframe_embedded_file(TARGET rawframe_studio FILE "${CMAKE_CURRENT_SOURCE_DIR}/fonts/IBMPlexSans-Regular.ttf"
                       HEADER studio_font.h SYMBOL kStudioFont)
rawframe_embedded_file(TARGET rawframe_studio FILE "${CMAKE_CURRENT_SOURCE_DIR}/fonts/IBMPlexSans-SemiBold.ttf"
                       HEADER studio_font_strong.h SYMBOL kStudioStrongFont)
rawframe_embedded_file(TARGET rawframe_studio FILE "${CMAKE_CURRENT_SOURCE_DIR}/fonts/IBMPlexMono-Regular.ttf"
                       HEADER studio_font_mono.h SYMBOL kStudioMonoFont)
rawframe_embedded_file(TARGET rawframe_studio FILE "${CMAKE_CURRENT_SOURCE_DIR}/icons/icons.rgba"
                       HEADER studio_icons.h SYMBOL kStudioIcons)
# Studio's own icon as its window shows it, 32, 64, and 256 pixels square,
# RGBA (the logo kit's app icon; D920).
rawframe_embedded_file(TARGET rawframe_studio FILE "${CMAKE_CURRENT_SOURCE_DIR}/icons/app_icon.rgba"
                       HEADER studio_app_icon.h SYMBOL kStudioAppIcon)

rawframe_module_tests(NAME studio SOURCES tests/records_test.cpp tests/console_test.cpp tests/hierarchy_test.cpp tests/hints_test.cpp tests/preferences_test.cpp tests/version_control_test.cpp)
if(TARGET rawframe_studio_tests AND TARGET rawframe_process_child)
    # A program that ends at once on every system, for a played game whose
    # server ends as it starts (D501): the process tests' child, given an
    # argument it does not know.
    rawframe_test_program(rawframe_child rawframe_process_child)
    target_compile_definitions(rawframe_studio_tests PRIVATE
        RAWFRAME_PROCESS_CHILD="${rawframe_child}")
    add_dependencies(rawframe_studio_tests rawframe_process_child)
endif()
