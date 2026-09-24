# World replication (ADR-0015, SPEC-0010, SPEC-0041): component codecs, the
# mapping, state, and input payloads, and the server and client replication
# that turns committed World state into state datagrams and input datagrams
# into World input. Owns NetEntityId.
rawframe_module(NAME world_replication
    SOURCES src/codec.cpp src/records.cpp
    DEPS network world schema execution diagnostics result base
)

rawframe_module_tests(NAME world_replication SOURCES tests/records_test.cpp)
