# Maul3D, vendored at the exact revision recorded in third_party/README.md.
# This file is ours; everything else in this directory is Maul3D's own,
# unchanged. Built the way Maul3D's CMake builds its static library: C17 with
# the flags its determinism contract depends on, AVX2 and FMA kernels on x64
# (every backend gives the same bits), and the CPU check compiled without
# them so it can refuse on a CPU that lacks them.
add_library(rawframe_maul3d_vendor STATIC
    src/allocator.c
    src/body.c
    src/body_motion.c
    src/body_read.c
    src/broad_phase.c
    src/character.c
    src/contact_kernel.c
    src/contact_prepare.c
    src/contact_solver.c
    src/continuous.c
    src/core.c
    src/destruction.c
    src/diff.c
    src/distance.c
    src/draw.c
    src/dynamic_tree.c
    src/hash.c
    src/hull.c
    src/island.c
    src/integrate.c
    src/joint.c
    src/joint_distance.c
    src/joint_fixed.c
    src/joint_gear.c
    src/joint_generic.c
    src/joint_motor.c
    src/joint_parallel.c
    src/joint_params.c
    src/joint_prismatic.c
    src/joint_pulley.c
    src/joint_revolute.c
    src/joint_row.c
    src/joint_solver.c
    src/joint_spherical.c
    src/joint_wheel.c
    src/journal.c
    src/journal_replay.c
    src/journal_replay_actors.c
    src/journal_replay_joints.c
    src/journal_replay_shapes.c
    src/manifold.c
    src/manifold_mesh.c
    src/math_functions.c
    src/mesh_bvh.c
    src/mover.c
    src/narrowphase.c
    src/query.c
    src/query_proxy.c
    src/quickhull.c
    src/raycast.c
    src/replay.c
    src/shape.c
    src/shape_cast.c
    src/shape_create.c
    src/shape_data.c
    src/shape_mass.c
    src/shape_params.c
    src/snapshot.c
    src/softbody.c
    src/softbody_pairs.c
    src/softbody_solver.c
    src/solver.c
    src/triangle_contact.c
    src/vehicle.c
    src/vehicle_drivetrain.c
    src/voxel.c
    src/voxel_fracture.c
    src/water.c
    src/world.c
    src/world_events.c
    src/world_hash.c
    src/world_state.c
)
add_library(rawframe_maul3d_cpu OBJECT src/cpu.c)
target_sources(rawframe_maul3d_vendor PRIVATE $<TARGET_OBJECTS:rawframe_maul3d_cpu>)
add_library(maul3d::maul3d ALIAS rawframe_maul3d_vendor)
target_include_directories(rawframe_maul3d_vendor PUBLIC include PRIVATE src)
target_include_directories(rawframe_maul3d_cpu PRIVATE include src)
foreach(target rawframe_maul3d_vendor rawframe_maul3d_cpu)
    set_target_properties(${target} PROPERTIES C_STANDARD 17 C_STANDARD_REQUIRED ON C_EXTENSIONS OFF
                                                C_VISIBILITY_PRESET hidden)
    # Maul3D's own warnings, but not -Werror: a new compiler's new warning
    # in code we do not edit must not stop the engine building.
    rawframe_gnu_options(${target} PRIVATE -ffp-contract=off -fno-trapping-math -fno-fast-math
                                             -fno-unsafe-math-optimizations -Wall -Wextra)
endforeach()
if(CMAKE_SYSTEM_PROCESSOR MATCHES "x86_64|AMD64" AND RAWFRAME_GNU_OPTIONS)
    rawframe_gnu_options(rawframe_maul3d_vendor PRIVATE -mavx2 -mfma)
    target_compile_definitions(rawframe_maul3d_vendor PRIVATE MAUL3D_BUILT_FOR_AVX2=1)
    target_compile_definitions(rawframe_maul3d_cpu PRIVATE MAUL3D_BUILT_FOR_AVX2=1)
endif()
if(NOT MSVC)
    target_link_libraries(rawframe_maul3d_vendor PUBLIC m)
endif()
