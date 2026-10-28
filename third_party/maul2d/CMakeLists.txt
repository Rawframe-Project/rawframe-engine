# Maul2D, vendored at the exact revision recorded in
# docs/operations/dependencies.md. This file is ours; everything else in this
# directory is Maul2D's own, unchanged. Built the way Maul2D's CMake builds its
# static library: C23 with the flags its determinism contract depends on, AVX2
# and FMA kernels on x64 (every backend gives the same bits), and the CPU check
# compiled without them, its public header first, so it can refuse on a CPU
# that lacks them.
add_library(rawframe_maul2d_vendor STATIC
    src/allocator.c
    src/body.c
    src/body_motion.c
    src/body_read.c
    src/broadphase.c
    src/buoyancy.c
    src/ccd.c
    src/chain.c
    src/contact.c
    src/contact_solver.c
    src/contact_kernel.c
    src/core.c
    src/decompose.c
    src/destruction.c
    src/distance.c
    src/draw.c
    src/dynamic_tree.c
    src/format_layout.c
    src/fpenv.c
    src/geometry.c
    src/graph_color.c
    src/hash.c
    src/island.c
    src/joint.c
    src/joint_read.c
    src/joint_row.c
    src/joint_solver.c
    src/joint_distance.c
    src/joint_edges.c
    src/joint_filter.c
    src/joint_gear.c
    src/joint_motor.c
    src/joint_mouse.c
    src/joint_prismatic.c
    src/joint_pulley.c
    src/joint_ratchet.c
    src/joint_revolute.c
    src/joint_weld.c
    src/joint_wheel.c
    src/journal.c
    src/journal_replay.c
    src/manifold.c
    src/mass.c
    src/math_functions.c
    src/mover.c
    src/object_ids.c
    src/parallel.c
    src/particle.c
    src/particle_contacts.c
    src/particle_pairs.c
    src/particle_solver.c
    src/platform_time.c
    src/query.c
    src/query_proxy.c
    src/proxies.c
    src/ray_cast.c
    src/shape.c
    src/shape_read.c
    src/snapshot.c
    src/snapshot_check.c
    src/state_checks.c
    src/solver.c
    src/world.c
    src/world_validate.c
    src/world_hash.c
    src/world_internal.c
    src/world_state.c
)
add_library(rawframe_maul2d_cpu OBJECT src/cpu.c)
target_sources(rawframe_maul2d_vendor PRIVATE $<TARGET_OBJECTS:rawframe_maul2d_cpu>)
add_library(maul2d::maul2d ALIAS rawframe_maul2d_vendor)
target_include_directories(rawframe_maul2d_vendor PUBLIC include PRIVATE src)
target_include_directories(rawframe_maul2d_cpu PRIVATE include src)
if(MSVC)
    target_compile_options(rawframe_maul2d_cpu PRIVATE /FImaul2d/base.h)
else()
    target_compile_options(rawframe_maul2d_cpu PRIVATE -include maul2d/base.h)
endif()
foreach(target rawframe_maul2d_vendor rawframe_maul2d_cpu)
    set_target_properties(${target} PROPERTIES C_STANDARD 23 C_STANDARD_REQUIRED ON C_EXTENSIONS OFF
                                                C_VISIBILITY_PRESET hidden)
    # Maul2D's own warnings, but not -Werror: a new compiler's new warning
    # in code we do not edit must not stop the engine building.
    rawframe_gnu_options(${target} PRIVATE -ffp-contract=off -fno-fast-math -fno-unsafe-math-optimizations
                                             -fno-math-errno -Wall -Wextra)
    # clang-cl takes C23 from Clang's own spelling, as Maul2D's CMake gives it.
    if(MSVC)
        target_compile_options(${target} PRIVATE $<$<COMPILE_LANGUAGE:C>:/clang:-std=c23>)
    endif()
endforeach()
if(CMAKE_SYSTEM_PROCESSOR MATCHES "x86_64|AMD64" AND RAWFRAME_GNU_OPTIONS)
    rawframe_gnu_options(rawframe_maul2d_vendor PRIVATE -mavx2 -mfma)
    target_compile_definitions(rawframe_maul2d_vendor PRIVATE MAUL2D_BUILT_FOR_AVX2=1)
    target_compile_definitions(rawframe_maul2d_cpu PRIVATE MAUL2D_BUILT_FOR_AVX2=1)
endif()
target_link_libraries(rawframe_maul2d_vendor PUBLIC ${RAWFRAME_MATH_LIBRARY})
