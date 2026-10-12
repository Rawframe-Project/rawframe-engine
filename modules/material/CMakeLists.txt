# Materials (ADR-0027, ADR-0031, SPEC-0026): the `surface` family of graph
# documents, checked against SPEC-0026's Surface contract and compiled to a
# material's typed data blob, and the `post_process` and `canvas` families
# folded to their forms (D348, D355). No renderer in it, so a cook reads
# materials without a device.
rawframe_module(NAME material
    SOURCES src/canvas.cpp src/common.cpp src/encoding.cpp src/fold.cpp src/material.cpp src/parameters.cpp src/post_process.cpp src/slang.cpp src/surface_graph.cpp
    DEPS graph document result base
)

rawframe_module_tests(NAME material SOURCES tests/canvas_test.cpp tests/material_test.cpp tests/nodes_test.cpp
                                           tests/post_process_test.cpp)
if(TARGET rawframe_material_tests)
    # ADR-0026's proof (D490): a graph and the Slang written from it, which
    # the scene's tests build beside its handwritten twin.
    rawframe_embedded_file(TARGET rawframe_material_tests FILE "${CMAKE_CURRENT_SOURCE_DIR}/tests/seeds/faded.material"
                           HEADER "faded_material.h" SYMBOL "kFadedMaterial")
    rawframe_embedded_file(TARGET rawframe_material_tests
                           FILE "${PROJECT_SOURCE_DIR}/modules/render_scene_gpu/tests/materials/faded_graph.slang"
                           HEADER "faded_graph.h" SYMBOL "kFadedGraph")
    # Every math node of SPEC-0026's atomic core (D694).
    rawframe_embedded_file(TARGET rawframe_material_tests FILE "${CMAKE_CURRENT_SOURCE_DIR}/tests/seeds/arithmetic.material"
                           HEADER "arithmetic_material.h" SYMBOL "kArithmeticMaterial")
    # A graph whose vertices rise with time (D691), and the Slang written
    # from it, which the scene's tests draw.
    rawframe_embedded_file(TARGET rawframe_material_tests FILE "${CMAKE_CURRENT_SOURCE_DIR}/tests/seeds/rising.material"
                           HEADER "rising_material.h" SYMBOL "kRisingMaterial")
    rawframe_embedded_file(TARGET rawframe_material_tests
                           FILE "${PROJECT_SOURCE_DIR}/modules/render_scene_gpu/tests/materials/rising_graph.slang"
                           HEADER "rising_graph.h" SYMBOL "kRisingGraph")
    # A graph whose color is its face's (D705), and the Slang written from
    # it, which the scene's tests draw.
    rawframe_embedded_file(TARGET rawframe_material_tests FILE "${CMAKE_CURRENT_SOURCE_DIR}/tests/seeds/facing.material"
                           HEADER "facing_material.h" SYMBOL "kFacingMaterial")
    rawframe_embedded_file(TARGET rawframe_material_tests
                           FILE "${PROJECT_SOURCE_DIR}/modules/render_scene_gpu/tests/materials/facing_graph.slang"
                           HEADER "facing_graph.h" SYMBOL "kFacingGraph")
endif()
rawframe_module_fuzz(NAME material TARGET documents RUNS 10000 SEEDS tests/seeds SOURCES tests/fuzz_material.cpp)
rawframe_module_fuzz(NAME material TARGET post_processes RUNS 10000 SEEDS tests/post_seeds SOURCES tests/fuzz_post_process.cpp)
rawframe_module_fuzz(NAME material TARGET canvases RUNS 10000 SEEDS tests/canvas_seeds SOURCES tests/fuzz_canvas.cpp)
rawframe_module_fuzz(NAME material TARGET programs RUNS 10000 SEEDS tests/program_seeds SOURCES tests/fuzz_program.cpp)
