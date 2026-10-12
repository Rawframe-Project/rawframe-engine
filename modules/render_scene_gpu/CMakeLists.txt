# The 3D scene's GPU half (SPEC-0024's scene, D284) over the one device,
# apart from the CPU half because Maul RHI is not on every client yet.
# Client only: the dedicated server links none of it, and a platform Maul
# RHI is not built for has no module at all.
if(NOT TARGET rawframe_render)
    return()
endif()

rawframe_module(NAME render_scene_gpu SOURCES src/blocks.cpp src/bloom.cpp src/capture.cpp src/casting.cpp src/contact.cpp src/decals.cpp src/probes.cpp src/environment.cpp src/focus.cpp src/meshes.cpp src/metering.cpp src/models.cpp src/motion.cpp src/occlusion.cpp src/participant.cpp src/particles.cpp src/picture.cpp src/post.cpp src/pipelines.cpp src/programs.cpp src/reflection.cpp src/renderer.cpp src/runs.cpp src/sampled.cpp src/skinning.cpp src/tables.cpp src/temporal.cpp src/views.cpp DEPS render render_scene particles_gpu particles material texture mesh composition diagnostics result base)
rawframe_shader_containers(TARGET rawframe_render_scene_gpu NAMES scene occlusion reflect motion focus contact decal probe resolve fxaa bloom meter skin shadow sky temporal tonemap post)
# Which of a program material's containers the build's driver reads
# (D485), as the engine's own are chosen.
if(RAWFRAME_SHADER_SUFFIX STREQUAL ".metal")
    target_compile_definitions(rawframe_render_scene_gpu PRIVATE RAWFRAME_PROGRAM_CONTAINER=1)
elseif(RAWFRAME_SHADER_SUFFIX STREQUAL ".d3d12")
    target_compile_definitions(rawframe_render_scene_gpu PRIVATE RAWFRAME_PROGRAM_CONTAINER=2)
else()
    target_compile_definitions(rawframe_render_scene_gpu PRIVATE RAWFRAME_PROGRAM_CONTAINER=0)
endif()

rawframe_module_tests(NAME render_scene_gpu SOURCES tests/bloom_test.cpp tests/capture_test.cpp tests/contact_test.cpp tests/decal_test.cpp tests/environment_test.cpp tests/focus_test.cpp tests/grading_test.cpp tests/metering_test.cpp tests/motion_test.cpp tests/multisample_test.cpp tests/occlusion_test.cpp tests/particle_test.cpp tests/post_process_test.cpp tests/program_test.cpp tests/reflection_test.cpp tests/renderer_test.cpp tests/shadow_test.cpp tests/skinning_test.cpp tests/surface_test.cpp tests/view_test.cpp)
if(TARGET rawframe_render_scene_gpu_tests)
    # The material written for the tests, built by tools/gen_shaders.py
    # with the scene (D485), in the container the build's driver reads.
    rawframe_embedded_file(TARGET rawframe_render_scene_gpu_tests
                           FILE "${CMAKE_CURRENT_SOURCE_DIR}/tests/generated/checker.scene${RAWFRAME_SHADER_SUFFIX}.mrsc"
                           HEADER "checker_container.h" SYMBOL "kCheckerContainer")
    rawframe_embedded_file(TARGET rawframe_render_scene_gpu_tests
                           FILE "${CMAKE_CURRENT_SOURCE_DIR}/tests/generated/checker.shadow${RAWFRAME_SHADER_SUFFIX}.mrsc"
                           HEADER "checker_shadow_container.h" SYMBOL "kCheckerShadowContainer")
    # ADR-0026's proof (D490): a graph's material as rawframe.material writes
    # it, and the same written by hand.
    rawframe_embedded_file(TARGET rawframe_render_scene_gpu_tests
                           FILE "${CMAKE_CURRENT_SOURCE_DIR}/tests/generated/faded.scene${RAWFRAME_SHADER_SUFFIX}.mrsc"
                           HEADER "faded_scene_container.h" SYMBOL "kFadedScene")
    rawframe_embedded_file(TARGET rawframe_render_scene_gpu_tests
                           FILE "${CMAKE_CURRENT_SOURCE_DIR}/tests/generated/faded.shadow${RAWFRAME_SHADER_SUFFIX}.mrsc"
                           HEADER "faded_shadow_container.h" SYMBOL "kFadedShadow")
    rawframe_embedded_file(TARGET rawframe_render_scene_gpu_tests
                           FILE "${CMAKE_CURRENT_SOURCE_DIR}/tests/generated/faded_graph.scene${RAWFRAME_SHADER_SUFFIX}.mrsc"
                           HEADER "faded_graph_scene_container.h" SYMBOL "kFadedGraphScene")
    rawframe_embedded_file(TARGET rawframe_render_scene_gpu_tests
                           FILE "${CMAKE_CURRENT_SOURCE_DIR}/tests/generated/faded_graph.shadow${RAWFRAME_SHADER_SUFFIX}.mrsc"
                           HEADER "faded_graph_shadow_container.h" SYMBOL "kFadedGraphShadow")
    # A graph's material that lifts its model with the World's time (D691).
    rawframe_embedded_file(TARGET rawframe_render_scene_gpu_tests
                           FILE "${CMAKE_CURRENT_SOURCE_DIR}/tests/generated/rising_graph.scene${RAWFRAME_SHADER_SUFFIX}.mrsc"
                           HEADER "rising_graph_scene_container.h" SYMBOL "kRisingGraphScene")
    rawframe_embedded_file(TARGET rawframe_render_scene_gpu_tests
                           FILE "${CMAKE_CURRENT_SOURCE_DIR}/tests/generated/rising_graph.shadow${RAWFRAME_SHADER_SUFFIX}.mrsc"
                           HEADER "rising_graph_shadow_container.h" SYMBOL "kRisingGraphShadow")
    # A graph's material green on its front and red on its back (D705).
    rawframe_embedded_file(TARGET rawframe_render_scene_gpu_tests
                           FILE "${CMAKE_CURRENT_SOURCE_DIR}/tests/generated/facing_graph.scene${RAWFRAME_SHADER_SUFFIX}.mrsc"
                           HEADER "facing_graph_scene_container.h" SYMBOL "kFacingGraphScene")
    rawframe_embedded_file(TARGET rawframe_render_scene_gpu_tests
                           FILE "${CMAKE_CURRENT_SOURCE_DIR}/tests/generated/facing_graph.shadow${RAWFRAME_SHADER_SUFFIX}.mrsc"
                           HEADER "facing_graph_shadow_container.h" SYMBOL "kFacingGraphShadow")
    # A graph's material whose color is its tint parameter's (D708).
    rawframe_embedded_file(TARGET rawframe_render_scene_gpu_tests
                           FILE "${CMAKE_CURRENT_SOURCE_DIR}/tests/generated/tinted_graph.scene${RAWFRAME_SHADER_SUFFIX}.mrsc"
                           HEADER "tinted_graph_scene_container.h" SYMBOL "kTintedGraphScene")
    rawframe_embedded_file(TARGET rawframe_render_scene_gpu_tests
                           FILE "${CMAKE_CURRENT_SOURCE_DIR}/tests/generated/tinted_graph.shadow${RAWFRAME_SHADER_SUFFIX}.mrsc"
                           HEADER "tinted_graph_shadow_container.h" SYMBOL "kTintedGraphShadow")
    # A graph's material colored by its model's and its eye's spaces (D714).
    rawframe_embedded_file(TARGET rawframe_render_scene_gpu_tests
                           FILE "${CMAKE_CURRENT_SOURCE_DIR}/tests/generated/spaced_graph.scene${RAWFRAME_SHADER_SUFFIX}.mrsc"
                           HEADER "spaced_graph_scene_container.h" SYMBOL "kSpacedGraphScene")
    rawframe_embedded_file(TARGET rawframe_render_scene_gpu_tests
                           FILE "${CMAKE_CURRENT_SOURCE_DIR}/tests/generated/spaced_graph.shadow${RAWFRAME_SHADER_SUFFIX}.mrsc"
                           HEADER "spaced_graph_shadow_container.h" SYMBOL "kSpacedGraphShadow")
    # A graph's material coated smooth over a rough base (D716).
    rawframe_embedded_file(TARGET rawframe_render_scene_gpu_tests
                           FILE "${CMAKE_CURRENT_SOURCE_DIR}/tests/generated/coated_graph.scene${RAWFRAME_SHADER_SUFFIX}.mrsc"
                           HEADER "coated_graph_scene_container.h" SYMBOL "kCoatedGraphScene")
    rawframe_embedded_file(TARGET rawframe_render_scene_gpu_tests
                           FILE "${CMAKE_CURRENT_SOURCE_DIR}/tests/generated/coated_graph.shadow${RAWFRAME_SHADER_SUFFIX}.mrsc"
                           HEADER "coated_graph_shadow_container.h" SYMBOL "kCoatedGraphShadow")
    # A graph's material brushed along a turn its parameter gives (D717).
    rawframe_embedded_file(TARGET rawframe_render_scene_gpu_tests
                           FILE "${CMAKE_CURRENT_SOURCE_DIR}/tests/generated/brushed_graph.scene${RAWFRAME_SHADER_SUFFIX}.mrsc"
                           HEADER "brushed_graph_scene_container.h" SYMBOL "kBrushedGraphScene")
    rawframe_embedded_file(TARGET rawframe_render_scene_gpu_tests
                           FILE "${CMAKE_CURRENT_SOURCE_DIR}/tests/generated/brushed_graph.shadow${RAWFRAME_SHADER_SUFFIX}.mrsc"
                           HEADER "brushed_graph_shadow_container.h" SYMBOL "kBrushedGraphShadow")
    # A graph's material of clear glass, seen through (D720).
    rawframe_embedded_file(TARGET rawframe_render_scene_gpu_tests
                           FILE "${CMAKE_CURRENT_SOURCE_DIR}/tests/generated/glass_graph.scene${RAWFRAME_SHADER_SUFFIX}.mrsc"
                           HEADER "glass_graph_scene_container.h" SYMBOL "kGlassGraphScene")
    rawframe_embedded_file(TARGET rawframe_render_scene_gpu_tests
                           FILE "${CMAKE_CURRENT_SOURCE_DIR}/tests/generated/glass_graph.shadow${RAWFRAME_SHADER_SUFFIX}.mrsc"
                           HEADER "glass_graph_shadow_container.h" SYMBOL "kGlassGraphShadow")
    # A frosted glass, its roughness blurring what it lets through (D723).
    rawframe_embedded_file(TARGET rawframe_render_scene_gpu_tests
                           FILE "${CMAKE_CURRENT_SOURCE_DIR}/tests/generated/frosted_graph.scene${RAWFRAME_SHADER_SUFFIX}.mrsc"
                           HEADER "frosted_graph_scene_container.h" SYMBOL "kFrostedGraphScene")
    rawframe_embedded_file(TARGET rawframe_render_scene_gpu_tests
                           FILE "${CMAKE_CURRENT_SOURCE_DIR}/tests/generated/frosted_graph.shadow${RAWFRAME_SHADER_SUFFIX}.mrsc"
                           HEADER "frosted_graph_shadow_container.h" SYMBOL "kFrostedGraphShadow")
endif()
