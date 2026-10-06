# The particles, trails, and beams on the device (ADR-0053's one GPU
# simulation, D357): the pool, the births, and the drawing the scene and
# the canvas share. Client only: the dedicated server links none of it,
# and a platform Maul RHI is not built for has no module at all.
if(NOT TARGET rawframe_render)
    return()
endif()

rawframe_module(NAME particles_gpu SOURCES src/particles.cpp DEPS render particles result base)
rawframe_shader_containers(TARGET rawframe_particles_gpu NAMES spawn particle)
