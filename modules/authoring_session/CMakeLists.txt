# An authoring session over a game's directory (ADR-0032, SPEC-0040, D407):
# records in, replies out, the scenes it opens kept with their histories,
# and a preview's tooling link. Toolchain only: rawframe-author and Studio
# hold one; no client or server closure contains it.
rawframe_module(NAME authoring_session
    SOURCES src/attach.cpp src/game.cpp src/handles.cpp src/link.cpp src/session.cpp
    DEPS authoring world_tooling network_quic network world_kest kest content scene schema document execution result base
)
