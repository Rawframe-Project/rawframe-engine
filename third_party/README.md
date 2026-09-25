# Third-party code

Everything here is pinned to an exact revision, never fetched during configure
or build (ADR-0005 as amended by ADR-0084), and unchanged from upstream except
for the build file we add beside it. Size, format, and em-dash checks do not
apply to vendored files; the code is upstream's, not ours.

| Name | Upstream | Revision | License | What is vendored |
| --- | --- | --- | --- | --- |
| Kest | `Rawframe-Project/kest` | `f45099f6d23f4323aa879361d9e9b7758ffa95ad` | MIT | `include/`, `src/` except `main.c`, `lib/`, `LICENSE` |
| Maul2D 0.0.1 | `Rawframe-Project/maul2d` | `42676bf8798be03b8436a040ca3c60bad4d13b8c` | MIT | `include/`, `src/`, `LICENSE` |
| Maul3D 0.0.1 | `Rawframe-Project/maul3d` | `a8f590b6d38afcf75133dd3d63777d09ba984c7a` | MIT | `include/`, `src/`, `LICENSE` |
| miniaudio 0.11.25 | `mackron/miniaudio` | `9634bedb5b5a2ca38c1ee7108a9358a4e233f14d` | public domain or MIT-0 (stb_vorbis v1.22: public domain or MIT) | `miniaudio.h`, `miniaudio.c`, `LICENSE`, `extras/stb_vorbis.c` |
| MsQuic 2.6.1 | `microsoft/msquic` | `a01333cf7c2659cce0ff03ef3f21e1ff15bb5b83` | MIT | build files, `src/` without tests, tools, or Windows PGO data, notices |
| Opus 1.5.2 | `xiph/opus` | `ddbe48383984d56acd9e1ab6a090c54ca6b735a6` | BSD-3-Clause | `include/`, `src/`, `celt/`, `silk/`, the three source lists, `COPYING` |
| Zstandard 1.5.7 | `facebook/zstd` | `f8745da6ff1ad1e7bab384bd1f9d742439278e99` | BSD-3-Clause (dual-licensed; the BSD license is the one taken) | `lib/common`, `lib/compress`, `lib/decompress`, `lib/zstd.h`, `lib/zstd_errors.h`, `LICENSE` |
| cgltf 1.15 | `jkuhlmann/cgltf` | `360db1a95480fe102ae9c69b27c5d101167ff5ba` | MIT | `cgltf.h`, `LICENSE`; the implementation unit `cgltf.c` is ours |
| CLDR 48.1.0 (JSON) | `unicode-org/cldr-json` | `d2988851207ba643d9ccc4c2e895e5ca1f4fbaf3` | Unicode-3.0 | `LICENSE`; `cldr-core`'s `plurals`, `ordinals`, `likelySubtags`, `parentLocales`, `aliases`, and `numberingSystems` supplements and `coverageLevels.json`; `cldr-numbers-full`'s `numbers.json` of every locale of modern coverage |
| OpenSSL 3.5 | `openssl/openssl` | `453eaaa9e6bb1304730abacfbb73d51868cb6ab9` | Apache-2.0 | everything but `test/`, `demos/`, the programs' sample keys, and the documentation and fuzzers other than their `build.info` files |

OpenSSL is built once per machine natively (tools/build_quic.sh) and, for the
web build, its libcrypto alone for wasm32-wasi (tools/build_openssl_wasm.sh,
with cmake/openssl_wasi_shim.h forced in for the chmod wasi-libc lacks). Moving
its pin moves both: the revision in third_party/quic.cmake and
third_party/openssl_wasm.cmake.

To move the Kest pin, run `tools/update_kest.sh <kest checkout> <revision>`,
build, run the full check, and commit the result with the new revision in this
table.

To move a Maul pin, run `tools/update_maul.sh <maul2d|maul3d> <checkout> <revision>`,
bring the source list in `third_party/<engine>/CMakeLists.txt` in line with
upstream's, and proceed as for Kest. Maul snapshots and journals refuse
another build, so both sides of anything that exchanges them need the same pin.

To move the cgltf pin, run `tools/update_cgltf.sh <checkout> <revision>` and
proceed as for Kest. Only the cook links it: glTF is decoded in import tooling,
never in a runtime (ADR-0058).

To move the miniaudio pin, run `tools/update_miniaudio.sh <checkout> <revision>`
and proceed as for Kest. The runtime builds only its device layer
(ADR-0038), and the audio module keeps its types out of every public header;
its MP3 decoder and stb_vorbis are built only into import tooling, never into
a runtime closure (ADR-0058).

To move the Opus pin, run `tools/update_opus.sh <checkout> <revision>` and
proceed as for Kest; the build reads upstream's own source lists, so a new
source file needs nothing here. It is built in floating point, portable C
only, without the deep-learning extensions: import tooling encodes and the
client decodes (ADR-0058).

MsQuic and OpenSSL move together: the OpenSSL revision is the one the MsQuic
revision pins as its `submodules/openssl`. Run
`tools/update_quic.sh <msquic revision> <openssl revision>`, update this table
and the two revisions in `third_party/quic.cmake`, and run the full check.
Configure builds both once per machine with `tools/build_quic.sh`, into
`~/.cache/rawframe` (or `$RAWFRAME_DEPENDENCY_CACHE`); it takes about twenty
seconds and is never repeated for the same revisions, script, and compiler.

To move the Zstandard pin, run `tools/update_zstd.sh <checkout> <revision>` and
proceed as for Kest. SPEC-0021's canonical packing pins the exact revision and
parameters: a new revision may change blob bytes, so it is a new packer
generation, never a silent change. Runtimes link the decoder only.

To move the CLDR pin, run `tools/update_cldr.sh <cldr-json commit>`, then
`tools/generate_cldr.py`, which remakes the localization module's generated
tables from it, and proceed as for Kest. Data, not code: no CLDR file is
compiled or read at run time; the tables are made ahead of time (ADR-0050).
