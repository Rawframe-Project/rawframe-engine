# Studio's shell (ADR-0032, D435): a session on a game held in Studio's own
# process, shown as a UI of its own. Toolchain only; no client or server
# closure contains it.
rawframe_module(NAME studio
    SOURCES src/participant.cpp
    DEPS authoring_session font_import view ui texture document composition execution diagnostics result base
)
# Studio's own font (Inter, SIL Open Font License 1.1, fonts/Inter-LICENSE.txt).
rawframe_embedded_file(TARGET rawframe_studio FILE "${CMAKE_CURRENT_SOURCE_DIR}/fonts/Inter-Regular.ttf"
                       HEADER studio_font.h SYMBOL kStudioFont)
