# World checkpoints (ADR-0016, SPEC-0011): capture a committed World into a
# canonical, integrity-checked artifact and restore one into a candidate
# World. Bytes in, bytes out; where they are kept is the caller's.
rawframe_module(NAME world_snapshot
    SOURCES src/capture.cpp src/codec.cpp src/container.cpp src/projection.cpp src/restore.cpp
    DEPS world schema result base
)

rawframe_module_tests(NAME world_snapshot SOURCES tests/snapshot_test.cpp)
