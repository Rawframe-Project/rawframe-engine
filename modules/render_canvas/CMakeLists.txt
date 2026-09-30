# The 2D canvas (ADR-0045, SPEC-0024): `rawframe.canvas`'s sprites extracted
# from a client's World and queued into batched quads. Client only: the
# dedicated server carries the components as plain values and links none of
# this. Its textures are read decoded from cooked content; the device half,
# uploading, recording, and presenting, waits for Maul RHI.
rawframe_module(NAME render_canvas
    SOURCES src/canvas.cpp src/participant.cpp
    DEPS game_textures game_content assets content texture physics2d world_replication world_kest kest world composition schema execution diagnostics result base
)

rawframe_module_tests(NAME render_canvas SOURCES tests/canvas_test.cpp)
