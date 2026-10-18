# An authoring session over a game's directory (ADR-0032, SPEC-0040, D407):
# records in, replies out, the scenes it opens kept with their histories,
# and a preview's tooling link. Toolchain only: rawframe-author and Studio
# hold one; no client or server closure contains it.
rawframe_module(NAME authoring_session
    SOURCES src/attach.cpp src/bookmarks.cpp src/cooking.cpp src/data_kinds.cpp src/documents.cpp src/extensions.cpp src/game.cpp src/handles.cpp src/importing.cpp src/link.cpp src/mcp.cpp src/new_game.cpp src/presence.cpp src/previewing.cpp src/references.cpp src/settings.cpp src/thumbnails.cpp src/asset_references.cpp src/replies.cpp src/session.cpp src/viewing.cpp
    DEPS authoring blockout terrain tilemap scatter sequence cook animation audio input localization texture material graph ui process world_tooling view network_quic network world_kest kest_library kest content scene schema document execution result base
)
