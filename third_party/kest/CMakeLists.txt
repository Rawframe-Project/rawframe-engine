# Kest, vendored at the exact revision recorded in third_party/README.md. This
# file is ours; everything else in this directory is Kest's own, unchanged.
# Built the way Kest's Makefile builds libkest.a, plus -ffp-contract=off so the
# machine's arithmetic follows the engine's determinism policy.
add_library(rawframe_kest_vendor STATIC
    src/ast.c
    src/build.c
    src/check.c
    src/compile.c
    src/contract.c
    src/dap.c
    src/debug.c
    src/diag.c
    src/emitc.c
    src/fmt.c
    src/ground.c
    src/held.c
    src/ir.c
    src/kest.c
    src/lexer.c
    src/loader.c
    src/lower.c
    src/lsp.c
    src/mem.c
    src/parser.c
    src/project.c
    src/types.c
    src/value.c
    src/verify.c
    src/vm.c
    src/wire.c
)
add_library(kest::kest ALIAS rawframe_kest_vendor)
target_include_directories(rawframe_kest_vendor PUBLIC include PRIVATE src)
set_target_properties(rawframe_kest_vendor PROPERTIES C_STANDARD 11 C_STANDARD_REQUIRED ON C_EXTENSIONS OFF)
target_compile_definitions(rawframe_kest_vendor PRIVATE KEST_LIB_DIR="${CMAKE_CURRENT_SOURCE_DIR}/lib/")
# Kest's own warnings, but not -Werror: a new compiler's new warning in code
# we do not edit must not stop the engine building.
rawframe_gnu_options(rawframe_kest_vendor PRIVATE -O2 -ffp-contract=off -Wall -Wextra)
if(NOT MSVC)
    target_link_libraries(rawframe_kest_vendor PUBLIC m)
endif()

# Where the Kest standard library is, for hosts building programs that import it.
set(RAWFRAME_KEST_LIBRARY "${CMAKE_CURRENT_SOURCE_DIR}/lib/" CACHE INTERNAL "Kest standard library directory")
