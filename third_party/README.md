# Third-party code

Everything here is pinned to an exact revision, never fetched during configure
or build (ADR-0005 as amended by ADR-0084), and unchanged from upstream except
for the build file we add beside it. Size, format, and em-dash checks do not
apply to vendored files; the code is upstream's, not ours.

| Name | Upstream | Revision | License | What is vendored |
| --- | --- | --- | --- | --- |
| Kest | `Rawframe-Project/kest` | `e2930df874619950d347d19a75aa6a7ffb342083` | MIT | `include/`, `src/` except `main.c`, `lib/`, `LICENSE` |
| Maul2D 0.0.1 | `Rawframe-Project/maul2d` | `42676bf8798be03b8436a040ca3c60bad4d13b8c` | MIT | `include/`, `src/`, `LICENSE` |
| Maul3D 0.0.1 | `Rawframe-Project/maul3d` | `a8f590b6d38afcf75133dd3d63777d09ba984c7a` | MIT | `include/`, `src/`, `LICENSE` |
| Maul Unicode 0.2.0 | `Rawframe-Project/maul-unicode` | `de28bfc4cdd7a38993bfa0492ee1ac0cb7e9ace5` | MIT (its UCD tables: Unicode-3.0) | `include/`, `src/`, `cmake/`, `CMakeLists.txt`, `LICENSE` |
| Maul Window 0.5.0 (the web without Emscripten, mwin-0022, D250; Xbox pads through Windows.Gaming.Input, mwin-0023, D251; macOS's AppKit backend, mwin-0024, the client's windows there since D405) | `Rawframe-Project/maul-window` | `25775b69fb78fc87eeb192478a54f1f27b2d4770` | MIT (its gamepad tables: SDL_GameControllerDB, zlib) | `include/`, `src/`, `cmake/`, `protocols/`, `tools/gen_web_glue.py`, `CMakeLists.txt`, `LICENSE` |
| Maul RHI 0.2.0 (its Vulkan driver on Linux; its Direct3D 12 driver on Windows, D415; its Metal driver on macOS, D406; its WebGPU driver on the web, built with wasm32-wasi, D282) | `Rawframe-Project/maul-rhi` | `ff03f99e821be5a7b891c755597cb2c116041d9d` | MIT (its Khronos headers: Apache-2.0 or MIT) | `include/`, `src/`, `khronos/`, `cmake/`, `tools/mrhi_container.py` and the `docs/contract/mrhi.json` it reads, `tools/mrhi_msl.py` (D406), `tools/mrhi_dxil.py` (D415), `tools/gen_web_glue.py`, `CMakeLists.txt`, `LICENSE` |
| Maul UI, unreleased main at `8512ee8` (its core: node tree, flex layout, style, draw-command list; its text component on, over FreeType, HarfBuzz, and Maul Unicode below, D383; its glyph images, D398; its glyph atlases, D404) | `Rawframe-Project/maul-ui` | `8512ee88b1c1e9d0ec2d026c648c909301ee4082` | MIT | `include/`, `src/`, `cmake/`, `CMakeLists.txt`, `LICENSE`, `THIRD_PARTY.md` |
| Maul Nav, unreleased main at `ddbdbd0` (navmesh generation, path and spatial queries, flow fields, avoidance; ADR-0056 as amended by D126, D399) | `Rawframe-Project/maul-nav` | `ddbdbd084391fb0d4170a8f3190168d46ec1c262` | MIT | `include/`, `src/`, `cmake/`, `CMakeLists.txt`, `LICENSE` |
| Maul Audio, unreleased main at `fbd946d` (its device layer: PipeWire, PulseAudio, and ALSA opened at run time on Linux, WASAPI, Core Audio, and its offline backend; the audio module's output, D401) | `Rawframe-Project/maul-audio` | `fbd946d0e120d2e8726d7cef0d80610f9e0c4b5c` | MIT | `include/`, `src/`, `cmake/`, `CMakeLists.txt`, `LICENSE` |
| FreeType 2.14.3, Maul UI's text component's (D383) | `freetype/freetype` release archive, SHA-256 `36bc4f1cc413335368ee656c42afca65c5a3987e8768cc28cf11ba775e785a5f` | 2.14.3 | FreeType License (FTL; dual with GPLv2, the FTL is the one taken) | `include/`, `src/` of the modules Maul UI builds (`base`, `cff`, `psaux`, `pshinter`, `psnames`, `sfnt`, `smooth`, `truetype`), `LICENSE.TXT`, `docs/FTL.TXT` |
| HarfBuzz 14.5.1, Maul UI's text component's (D383) | `harfbuzz/harfbuzz` release archive, SHA-256 `7e2fa4e8c7c98e8d8140671f5772542afaaa6acccfbd746506886b6d85f7f8d6` | 14.5.1 | MIT ("Old MIT") | `src/` without its build files, scripts, and generator sources, `COPYING` |
| OpenType Sanitizer 9.3.0, the cook's font sanitizer (ADR-0078, D385); import tooling only | `khaledhosny/ots` release archive, SHA-256 `23814f8e90ee77379f54e86a012c09bba2d133940e5257546b29cf087a73beec` | 9.3.0 | BSD-3-Clause | `include/`, `src/` without Graphite's tables, `LICENSE`, `README.md`; ours: `CMakeLists.txt`, `rawframe/` (its configuration, and refusing stand-ins for zlib and the WOFF 2.0 decoder) |
| miniaudio 0.11.25, the import tooling's decoders (the output plays through Maul Audio, D401) | `mackron/miniaudio` | `9634bedb5b5a2ca38c1ee7108a9358a4e233f14d` | public domain or MIT-0 (stb_vorbis v1.22: public domain or MIT) | `miniaudio.h`, `miniaudio.c`, `LICENSE`, `extras/stb_vorbis.c` |
| MsQuic 2.6.1 | `microsoft/msquic` | `a01333cf7c2659cce0ff03ef3f21e1ff15bb5b83` | MIT | build files, `src/` without tests, tools, or Windows PGO data, notices |
| XDP for Windows, MsQuic's submodule | `microsoft/xdp-for-windows` | `d372b52577a724e04fa4c06acb90bbfa4719fc25`, the revision MsQuic's pin names | MIT | `published/external` (headers MsQuic's Windows datapath includes), `LICENSE`, at `msquic/submodules/xdp-for-windows` |
| Opus 1.5.2 | `xiph/opus` | `ddbe48383984d56acd9e1ab6a090c54ca6b735a6` | BSD-3-Clause | `include/`, `src/`, `celt/`, `silk/`, the three source lists, `COPYING` |
| Zstandard 1.5.7 | `facebook/zstd` | `f8745da6ff1ad1e7bab384bd1f9d742439278e99` | BSD-3-Clause (dual-licensed; the BSD license is the one taken) | `lib/common`, `lib/compress`, `lib/decompress`, `lib/zstd.h`, `lib/zstd_errors.h`, `LICENSE` |
| Wuffs 0.4.0-alpha.10 | `google/wuffs-mirror-release-c` | `7411f488fe2e2c205c3d3b3d28638b7356522930` | Apache-2.0 or MIT (dual; the Apache license is the one taken) | `release/c/wuffs-v0.4.c`, `LICENSE`, `LICENSE-APACHE`, `LICENSE-MIT`; the implementation unit `wuffs.cpp` is ours (D253) |
| bc7enc_rdo | `richgel999/bc7enc_rdo` | `b9438627eef73a1157e84201b6fa6eb2ffd6d9f0` | MIT or public domain (the MIT license is the one taken) | `bc7enc.cpp`, `bc7enc.h`, `bc7decomp.cpp`, `bc7decomp.h`, `LICENSE` (D253) |
| cgltf 1.15 | `jkuhlmann/cgltf` | `360db1a95480fe102ae9c69b27c5d101167ff5ba` | MIT | `cgltf.h`, `LICENSE`; the implementation unit `cgltf.c` is ours |
| CLDR 48.1.0 (JSON) | `unicode-org/cldr-json` | `d2988851207ba643d9ccc4c2e895e5ca1f4fbaf3` | Unicode-3.0 | `LICENSE`; `cldr-core`'s `plurals`, `ordinals`, `likelySubtags`, `parentLocales`, `aliases`, and `numberingSystems` supplements and `coverageLevels.json`; `cldr-numbers-full`'s `numbers.json` of every locale of modern coverage |
| OpenSSL 3.5 | `openssl/openssl` | `453eaaa9e6bb1304730abacfbb73d51868cb6ab9` | Apache-2.0 | everything but `test/`, `demos/`, the programs' sample keys, and the documentation and fuzzers other than their `build.info` files |

OpenSSL is built once per machine natively (tools/build_quic.sh) and, for the
web build, its libcrypto alone for wasm32-wasi (tools/build_openssl_wasm.sh,
with cmake/openssl_wasi_shim.h forced in for the chmod wasi-libc lacks). Moving
its pin moves both: the revision in third_party/quic.cmake and
third_party/openssl_wasm.cmake.

OTS is vendored by tools/update_ots.sh from its release archive, checked by
SHA-256; WOFF and WOFF 2.0 are refused before it, and the stand-ins make its
paths for them fail too.

FreeType and HarfBuzz are vendored by tools/update_text_deps.sh from their
release archives, checked by the SHA-256 Maul UI's
cmake/TextDependencies.cmake pins; move them when that file moves. Maul UI's
build finds them, and the vendored Maul Unicode, through
FETCHCONTENT_SOURCE_DIR_* (third_party/maul_ui.cmake): nothing is fetched.

To move the Kest pin, run `tools/update_kest.sh <kest checkout> <revision>`,
build, run the full check, and commit the result with the new revision in this
table.

To move a Maul pin, run `tools/update_maul.sh <maul2d|maul3d|maul-unicode|maul-window|maul-rhi|maul-ui|maul-nav|maul-audio> <checkout> <revision>`,
bring the source list in `third_party/<engine>/CMakeLists.txt` in line with
upstream's (Maul2D and Maul3D; Maul Unicode, Maul Window, Maul RHI, and
Maul UI, Maul Nav, and Maul Audio keep their own CMake, configured by
`third_party/maul_window.cmake`, `third_party/maul_rhi.cmake`,
`third_party/maul_ui.cmake`, `third_party/maul_nav.cmake`, and
`third_party/maul_audio.cmake`), and proceed
as for Kest.
Maul Window's pin names the Maul Unicode release it was made with; move both
together. Maul snapshots and journals refuse
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
