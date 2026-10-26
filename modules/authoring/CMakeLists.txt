# Authoring: the one surface that changes
# authored documents, by typed deltas in transactions with per-document
# undo. Toolchain only: no game runtime, dedicated server, or export
# closure contains it (tools/check_repo.py holds the server's).
rawframe_module(NAME authoring
    SOURCES src/authored_data.cpp src/authored_lines.cpp src/authored_material.cpp src/authored_scene.cpp src/authored_styles.cpp src/authored_ui.cpp src/delta.cpp src/json_document.cpp src/instances.cpp src/journal.cpp src/operations.cpp src/patches.cpp src/queries.cpp src/request.cpp src/session.cpp
    DEPS material graph ui scene schema document result base
)

rawframe_module_tests(NAME authoring SOURCES tests/authored_data_test.cpp tests/authored_lines_test.cpp tests/authored_material_test.cpp tests/authored_scene_test.cpp tests/authored_styles_test.cpp tests/authored_ui_test.cpp tests/delta_test.cpp tests/json_document_test.cpp tests/operations_test.cpp tests/queries_test.cpp tests/request_test.cpp tests/session_test.cpp)
rawframe_module_fuzz(NAME authoring TARGET journal RUNS 15000 SEEDS tests/seeds/journal SOURCES tests/fuzz_journal.cpp)
rawframe_module_fuzz(NAME authoring TARGET session RUNS 15000 SEEDS tests/seeds/session SOURCES tests/fuzz_session.cpp)
rawframe_module_fuzz(NAME authoring TARGET authored RUNS 15000 SEEDS tests/seeds/authored SOURCES tests/fuzz_authored.cpp)
