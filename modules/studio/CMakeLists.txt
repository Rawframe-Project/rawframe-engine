# Studio's shell (ADR-0032, D435): a session on a game held in Studio's own
# process, shown as a UI of its own. Toolchain only; no client or server
# closure contains it.
rawframe_module(NAME studio
    SOURCES src/editing.cpp src/panels.cpp src/participant.cpp src/play.cpp src/playing.cpp src/records.cpp
    DEPS authoring_session font_import process view ui texture document composition execution diagnostics result base
)
# Studio's own font (Inter, SIL Open Font License 1.1, fonts/Inter-LICENSE.txt).
rawframe_embedded_file(TARGET rawframe_studio FILE "${CMAKE_CURRENT_SOURCE_DIR}/fonts/Inter-Regular.ttf"
                       HEADER studio_font.h SYMBOL kStudioFont)

rawframe_module_tests(NAME studio SOURCES tests/records_test.cpp)
