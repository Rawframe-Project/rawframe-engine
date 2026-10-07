# Materials (ADR-0027, ADR-0031, SPEC-0026): the `surface` family of graph
# documents, checked against SPEC-0026's Surface contract and compiled to a
# material's typed data blob, and the `post_process` and `canvas` families
# folded to their forms (D348, D355). No renderer in it, so a cook reads
# materials without a device.
rawframe_module(NAME material
    SOURCES src/canvas.cpp src/common.cpp src/encoding.cpp src/fold.cpp src/material.cpp src/parameters.cpp src/post_process.cpp src/slang.cpp src/surface_graph.cpp
    DEPS graph document result base
)

rawframe_module_tests(NAME material SOURCES tests/canvas_test.cpp tests/material_test.cpp tests/post_process_test.cpp)
rawframe_module_fuzz(NAME material TARGET documents RUNS 10000 SEEDS tests/seeds SOURCES tests/fuzz_material.cpp)
rawframe_module_fuzz(NAME material TARGET post_processes RUNS 10000 SEEDS tests/post_seeds SOURCES tests/fuzz_post_process.cpp)
rawframe_module_fuzz(NAME material TARGET canvases RUNS 10000 SEEDS tests/canvas_seeds SOURCES tests/fuzz_canvas.cpp)
rawframe_module_fuzz(NAME material TARGET programs RUNS 10000 SEEDS tests/program_seeds SOURCES tests/fuzz_program.cpp)
