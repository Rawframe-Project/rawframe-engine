# The 3D scene (ADR-0051, SPEC-0024): `rawframe.model`'s models extracted
# from a client's World where their poses put them, then seen through a
# camera, culled, and queued in draw order, lit by the sun and the sky.
# Client only: the dedicated server carries the components as plain values
# and links none of this. The device half is render_scene_gpu's.
rawframe_module(NAME render_scene
    SOURCES src/camera.cpp src/clusters.cpp src/decals.cpp src/grading.cpp src/lights.cpp src/meshes.cpp src/effects.cpp src/game_scene.cpp src/loading.cpp src/participant.cpp src/pictures.cpp src/probes.cpp src/scene.cpp
    DEPS view particles game_textures texture material graph game_content content physics3d mesh world_replication world_kest kest world composition schema execution diagnostics result base
)

rawframe_module_tests(NAME render_scene SOURCES tests/camera_test.cpp tests/kelvin_test.cpp tests/picking_test.cpp tests/scene_test.cpp)
