# Authoring (ADR-0032, ADR-0065, SPEC-0040): the one surface that changes
# authored documents, by typed deltas in transactions with per-document
# undo. Toolchain only: no game runtime, dedicated server, or export
# closure contains it (tools/check_repo.py holds the server's).
rawframe_module(NAME authoring
    SOURCES src/delta.cpp
    DEPS scene schema document result base
)

rawframe_module_tests(NAME authoring SOURCES tests/delta_test.cpp)
