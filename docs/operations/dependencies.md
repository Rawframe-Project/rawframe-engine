# Dependencies

Every third-party library is vendored under `third_party/` at an exact revision and never fetched during configure or build. Vendored files are upstream's, unchanged except for a build file the engine adds beside some of them; the format, size and documentation checks skip them.

## Contents

- [Pins](#pins)
- [Moving a pin](#moving-a-pin)
- [Notes per library](#notes-per-library)

## Pins

| Library | Upstream | Revision | License | What is vendored |
|---|---|---|---|---|
| Kest | `Rawframe-Project/kest` | `fa092f540ffe2cb94f75199842792fb61e68451f` | MIT | `include/`, `src/` except `main.c`, `lib/`, `LICENSE` |
| Maul2D 0.6.0 | `Rawframe-Project/maul2d` release archive `maul2d-0.6.0.tar.gz`, SHA-256 `f97262cd98a47e2f52ed7a811ce144bff1c3f51931323457d1313484c9a845ae` | `501ac5c191658435cbdd4f70a0abd6cdd9e37f03` (`v0.6.0`) | MIT | `include/`, `src/`, `LICENSE`; the build file is the engine's |
| Maul3D 0.7.0 | `Rawframe-Project/maul3d` release archive `maul3d-0.7.0.tar.gz`, SHA-256 `b4e42736c2c8413f8218846b9ce777bab79506d75c620707d571f24cb0632d4f` | `bf11f1fc3976ffb8b2ab4e4604252e760aea221c` (`v0.7.0`) | MIT | `include/`, `src/`, `LICENSE`; the build file is the engine's |
| Maul Unicode 0.6.0 | `Rawframe-Project/maul-unicode` release archive `maul-unicode-0.6.0.tar.gz`, SHA-256 `7ccaf1b0b61647865e1002c809dbc32aae4e1fc6b1e3d74c7f33589b8f2ba652` | `ecc875a6a75c83b1e0a5a35d1ac521b50c400570` (`v0.6.0`) | MIT; its Unicode tables Unicode-3.0 | `include/`, `src/`, `cmake/`, `CMakeLists.txt`, `LICENSE` |
| Maul Window 0.6.0 | `Rawframe-Project/maul-window` release archive `maul-window-0.6.0.tar.gz`, SHA-256 `15c8e659134b600c85c2c406dce78b54fdf781bb14289d5d4bee38aea3aba67c` | `0e2d3852c02786dd4b9bf3eabfc64004753ec715` (`v0.6.0`) | MIT | `include/`, `src/`, `cmake/`, `protocols/`, `java/`, `tools/gen_web_glue.py`, `CMakeLists.txt`, `LICENSE` |
| Maul RHI 0.13.0 | `Rawframe-Project/maul-rhi` release archive `maul-rhi-0.13.0.tar.gz`, SHA-256 `0909d86251f7a693cc7c889a418c5277e0c8c2c032b024327350728857a0d951` | `da1a2bfb43371e3361f4dd167f61255043466c09` (`v0.13.0`) | MIT; Khronos headers Apache-2.0 or MIT; DirectX headers MIT | `include/`, `src/`, `khronos/`, `directx/`, `cmake/`, the container, MSL and DXIL tools, `contract/mrhi.json`, `tools/gen_web_glue.py`, `CMakeLists.txt`, `LICENSE` |
| Maul UI 0.5.11 and its main after it | `Rawframe-Project/maul-ui` | `c680614c757f868ea7a6fb30bb9cce6d9ef26c2b` | MIT | `include/`, `src/`, `cmake/`, `java/`, `tools/gen_web_glue.py`, `CMakeLists.txt`, `LICENSE`, `THIRD_PARTY.md` |
| Maul Nav 0.2.0 | `Rawframe-Project/maul-nav` release archive `maul-nav-0.2.0.tar.gz`, SHA-256 `e5867312f2c981a198f774cead5692e4fce15f412af2af9e7330957d663dc8fd` | `db1e65197b809af34689aeca4e6a6da37f95f08f` (`v0.2.0`) | MIT | `include/`, `src/`, `cmake/`, `CMakeLists.txt`, `LICENSE` |
| Maul Audio 0.4.0 | `Rawframe-Project/maul-audio` release archive `maul-audio-0.4.0.tar.gz`, SHA-256 `572bf30ba89f56b6a32452585bf46265ece3a1ec285b5233c04cff110ff4926d` | `fbad1b28250657a2b5704830454c8bc097dbb590` (`v0.4.0`) | MIT | `include/`, `src/`, `cmake/`, `CMakeLists.txt`, `LICENSE` |
| FreeType 2.14.3 | release archive, SHA-256 `36bc4f1cc413335368ee656c42afca65c5a3987e8768cc28cf11ba775e785a5f` | 2.14.3 | FreeType License | the modules Maul UI builds, `LICENSE.TXT`, `docs/FTL.TXT` |
| HarfBuzz 14.5.1 | release archive, SHA-256 `7e2fa4e8c7c98e8d8140671f5772542afaaa6acccfbd746506886b6d85f7f8d6` | 14.5.1 | MIT | `src/` without build files, scripts and generators, `COPYING` |
| OpenType Sanitizer 9.3.0 | release archive, SHA-256 `23814f8e90ee77379f54e86a012c09bba2d133940e5257546b29cf087a73beec` | 9.3.0 | BSD-3-Clause | `include/`, `src/` without Graphite's tables, `LICENSE`, `README.md`; the build file and `rawframe/` are the engine's |
| miniaudio 0.11.25 | `mackron/miniaudio` | `9634bedb5b5a2ca38c1ee7108a9358a4e233f14d` | public domain or MIT-0 | `miniaudio.h`, `miniaudio.c`, `LICENSE`, `extras/stb_vorbis.c` |
| MsQuic 2.6.1 | `microsoft/msquic` | `a01333cf7c2659cce0ff03ef3f21e1ff15bb5b83` | MIT | build files, `src/` without tests, tools or PGO data, notices |
| XDP for Windows | `microsoft/xdp-for-windows` | `d372b52577a724e04fa4c06acb90bbfa4719fc25`, MsQuic's pin | MIT | `published/external`, `LICENSE` |
| OpenSSL 3.5 | `openssl/openssl` | `453eaaa9e6bb1304730abacfbb73d51868cb6ab9` | Apache-2.0 | all but tests, demos, sample keys and documentation |
| Opus 1.5.2 | `xiph/opus` | `ddbe48383984d56acd9e1ab6a090c54ca6b735a6` | BSD-3-Clause | `include/`, `src/`, `celt/`, `silk/`, the source lists, `COPYING` |
| Zstandard 1.5.7 | `facebook/zstd` | `f8745da6ff1ad1e7bab384bd1f9d742439278e99` | BSD-3-Clause | `lib/common`, `lib/compress`, `lib/decompress`, the two headers, `LICENSE` |
| Wuffs 0.4.0-alpha.10 | `google/wuffs-mirror-release-c` | `7411f488fe2e2c205c3d3b3d28638b7356522930` | Apache-2.0 | `release/c/wuffs-v0.4.c`, the licenses; `wuffs.cpp` is the engine's |
| bc7enc_rdo | `richgel999/bc7enc_rdo` | `b9438627eef73a1157e84201b6fa6eb2ffd6d9f0` | MIT | the encoder and decoder, `LICENSE` |
| cgltf 1.15 | `jkuhlmann/cgltf` | `360db1a95480fe102ae9c69b27c5d101167ff5ba` | MIT | `cgltf.h`, `LICENSE`; `cgltf.c` is the engine's |
| CLDR 48.1.0 (JSON) | `unicode-org/cldr-json` | `d2988851207ba643d9ccc4c2e895e5ca1f4fbaf3` | Unicode-3.0 | the core supplements the localization tables are made from, every modern-coverage locale's `numbers.json`, `LICENSE` |
| OpenXR SDK 1.1.63 | `KhronosGroup/OpenXR-SDK` | `f2448a8797c85814aa892efc1ab8707900fbcc78` | Apache-2.0 | the loader, its headers and jsoncpp, the licenses |

## Moving a pin

Each library has an update script that replaces its folder from a checkout or archive. After it runs: build, run `tools/check.sh full`, update the table above, and commit both in one pull request with the `deps` scope.

| Library | Command |
|---|---|
| Kest | `tools/update_kest.sh <checkout> <revision>` |
| A Maul library | `tools/update_maul.sh <maul2d\|maul3d\|maul-unicode\|maul-window\|maul-rhi\|maul-ui\|maul-nav\|maul-audio> <checkout> <revision>` |
| MsQuic and OpenSSL | `tools/update_quic.sh <msquic revision> <openssl revision>`, then the two revisions in `third_party/quic.cmake` |
| cgltf, miniaudio, Opus, Zstandard | `tools/update_cgltf.sh`, `update_miniaudio.sh`, `update_opus.sh`, `update_zstd.sh` with `<checkout> <revision>` |
| FreeType and HarfBuzz | `tools/update_text_deps.sh`, checked against the SHA-256 Maul UI's `cmake/TextDependencies.cmake` pins |
| OpenType Sanitizer | `tools/update_ots.sh`, from its release archive checked by SHA-256 |
| CLDR | `tools/update_cldr.sh <cldr-json commit>`, then `tools/generate_cldr.py` |

A Maul library that publishes a release is taken from its release archive after `sha256sum -c --ignore-missing SHA256SUMS`, and its archive digest goes in the table.

## Notes per library

- **Maul RHI** cooks the engine's shader containers: a move runs `tools/gen_shaders.py` with the shader toolchain and commits the containers and their stamps, which the check refuses from another release ([development.md](development.md#generated-files)).
- **Maul2D and Maul3D** keep the engine's own `CMakeLists.txt` beside their sources; bring its source list in line with upstream's when it changes. The other Maul libraries build with their own CMake, configured by `third_party/maul_*.cmake`.
- **Maul UI, Maul Window and Maul Unicode** move together: Maul UI and Maul Window both build against the vendored Maul Unicode (`third_party/maul_ui.cmake`, included first, configures it with its HarfBuzz functions). Maul UI's CMake names the lowest Maul Unicode it takes, but Maul Window's names the one release it was built against and takes no other, so Maul Unicode moves to the release Maul Window names. A move of any of them is built and checked with the others. Maul snapshots and journals refuse another build, so both sides of anything that exchanges them need the same pins.
- **MsQuic and OpenSSL** move together: the OpenSSL revision is the one MsQuic pins as its submodule. Configure builds both once per machine with `tools/build_quic.sh` into `~/.cache/rawframe` (or `$RAWFRAME_DEPENDENCY_CACHE`), and again only when the revisions, the script or the compiler change. For the web build, OpenSSL's libcrypto alone is built for wasm32-wasi by `tools/build_openssl_wasm.sh`.
- **Zstandard** is part of the packing format: a new revision may change compressed bytes, so moving it is a new packer generation, never a silent change. Runtimes link the decoder only.
- **cgltf, miniaudio's decoders, stb_vorbis and the Opus encoder** are import tooling: they are linked into the cook, never into a runtime. The runtime builds only Maul Audio's device layer and the Opus decoder.
- **OpenType Sanitizer** rebuilds every font the cook takes. WOFF and WOFF 2.0 are refused before it.
- **CLDR** is data: `tools/generate_cldr.py` turns it into `modules/localization/src/generated/`, and no CLDR file is read at run time.
