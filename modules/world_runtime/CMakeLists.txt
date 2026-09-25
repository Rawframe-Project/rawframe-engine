# The World as a runtime participant: owns a World, its registry, schedule, and
# tick pacer, lets other participants add components and systems, and runs due
# ticks in the Host's run_worlds phase. Separate from rawframe.world because the
# World must not depend on composition (SPEC-0007).
rawframe_module(NAME world_runtime
    SOURCES src/checkpoints.cpp src/saves.cpp src/world_runtime.cpp
    DEPS world_save world_snapshot world composition schema execution diagnostics result base
)

rawframe_module_tests(NAME world_runtime SOURCES tests/world_runtime_test.cpp)
