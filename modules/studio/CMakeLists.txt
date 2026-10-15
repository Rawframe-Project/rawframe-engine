# Studio's shell (ADR-0032, D435): a session on a game held in Studio's own
# process, shown as a UI of its own. Toolchain only; no client or server
# closure contains it.
rawframe_module(NAME studio
    SOURCES src/commands.cpp src/console.cpp src/console_panel.cpp src/documents.cpp src/editing.cpp src/hierarchy.cpp src/layers.cpp src/hints.cpp src/operations.cpp src/panels.cpp src/participant.cpp src/play.cpp src/playing.cpp src/preferences.cpp src/preferences_panel.cpp src/records.cpp src/reparenting.cpp src/selection.cpp src/splitters.cpp src/start.cpp
    DEPS authoring_session material font_import process view ui texture document composition execution diagnostics result base
)
# Studio's own font (Inter, SIL Open Font License 1.1, fonts/Inter-LICENSE.txt).
rawframe_embedded_file(TARGET rawframe_studio FILE "${CMAKE_CURRENT_SOURCE_DIR}/fonts/Inter-Regular.ttf"
                       HEADER studio_font.h SYMBOL kStudioFont)

rawframe_module_tests(NAME studio SOURCES tests/records_test.cpp tests/console_test.cpp tests/hierarchy_test.cpp tests/hints_test.cpp tests/preferences_test.cpp)
if(TARGET rawframe_studio_tests AND TARGET rawframe_process_child)
    # A program that ends at once on every system, for a played game whose
    # server ends as it starts (D501): the process tests' child, given an
    # argument it does not know.
    rawframe_test_program(rawframe_child rawframe_process_child)
    target_compile_definitions(rawframe_studio_tests PRIVATE
        RAWFRAME_PROCESS_CHILD="${rawframe_child}")
    add_dependencies(rawframe_studio_tests rawframe_process_child)
endif()
