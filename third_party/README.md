# Third-party code

Everything here is pinned to an exact revision, never fetched during configure
or build (ADR-0005 as amended by ADR-0084), and unchanged from upstream except
for the build file we add beside it. Size, format, and em-dash checks do not
apply to vendored files; the code is upstream's, not ours.

| Name | Upstream | Revision | License | What is vendored |
| --- | --- | --- | --- | --- |
| Kest | `Rawframe-Project/kest` | `ed4d2ea5380ac113420b670180d3d9716c3ad8a6` | MIT | `include/`, `src/` except `main.c`, `lib/`, `LICENSE` |
| Maul2D 0.0.1 | `Rawframe-Project/maul2d` | `42676bf8798be03b8436a040ca3c60bad4d13b8c` | MIT | `include/`, `src/`, `LICENSE` |
| MsQuic 2.6.1 | `microsoft/msquic` | `a01333cf7c2659cce0ff03ef3f21e1ff15bb5b83` | MIT | build files, `src/` without tests, tools, or Windows PGO data, notices |
| OpenSSL 3.5 | `openssl/openssl` | `453eaaa9e6bb1304730abacfbb73d51868cb6ab9` | Apache-2.0 | everything but `test/`, `demos/`, the programs' sample keys, and the documentation and fuzzers other than their `build.info` files |

To move the Kest pin, run `tools/update_kest.sh <kest checkout> <revision>`,
build, run the full check, and commit the result with the new revision in this
table.

To move the Maul2D pin, run `tools/update_maul2d.sh <maul2d checkout> <revision>`,
bring the source list in `third_party/maul2d/CMakeLists.txt` in line with
upstream's, and proceed as for Kest. Maul2D snapshots and journals refuse
another build, so both sides of anything that exchanges them need the same pin.

MsQuic and OpenSSL move together: the OpenSSL revision is the one the MsQuic
revision pins as its `submodules/openssl`. Run
`tools/update_quic.sh <msquic revision> <openssl revision>`, update this table
and the two revisions in `third_party/quic.cmake`, and run the full check.
Configure builds both once per machine with `tools/build_quic.sh`, into
`~/.cache/rawframe` (or `$RAWFRAME_DEPENDENCY_CACHE`); it takes about twenty
seconds and is never repeated for the same revisions, script, and compiler.
