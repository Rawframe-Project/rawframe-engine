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
| Maul2D 0.4.0 | `Rawframe-Project/maul2d` release archive `maul2d-0.4.0.tar.gz`, SHA-256 `43c5cb62599ec64de3284975f4d668b709566cf189142ae12e187e7bde4182eb` | `eab40045fe8f1bd193d7cecc383ff04253108bcd` (`v0.4.0`) | MIT | `include/`, `src/`, `LICENSE`; the build file is the engine's |
| Maul3D 0.6.0 | `Rawframe-Project/maul3d` release archive `maul3d-0.6.0.tar.gz`, SHA-256 `944c740109b1212f72de20bc8a4537b1afd56446b23c148d32a85aca1c2986fd` | `d152345512bed3bbe77327360918ba55353c0544` (`v0.6.0`) | MIT | `include/`, `src/`, `LICENSE`; the build file is the engine's |
| Maul Unicode 0.3.0 | `Rawframe-Project/maul-unicode` | `45523494be854c073dcb1a26a0b55dd07ca68879` | MIT; its Unicode tables Unicode-3.0 | `include/`, `src/`, `cmake/`, `CMakeLists.txt`, `LICENSE` |
| Maul Window 0.13.0 and its main after it | `Rawframe-Project/maul-window` | `a0b6e4413e5d9ba3979e5dc756b1af5f7328b177` | MIT | `include/`, `src/`, `cmake/`, `protocols/`, `java/`, `tools/gen_web_glue.py`, `CMakeLists.txt`, `LICENSE` |
| Maul RHI 0.7.0 and its main after it | `Rawframe-Project/maul-rhi` | `23012f3815d6c70535dd1a69a7ed03edeea69bba` | MIT; Khronos headers Apache-2.0 or MIT; DirectX headers MIT | `include/`, `src/`, `khronos/`, `directx/`, `cmake/`, the container, MSL and DXIL tools, `docs/contract/mrhi.json`, `tools/gen_web_glue.py`, `CMakeLists.txt`, `LICENSE` |
| Maul UI 0.5.11 and its main after it | `Rawframe-Project/maul-ui` | `c680614c757f868ea7a6fb30bb9cce6d9ef26c2b` | MIT | `include/`, `src/`, `cmake/`, `java/`, `tools/gen_web_glue.py`, `CMakeLists.txt`, `LICENSE`, `THIRD_PARTY.md` |
| Maul Nav 1.0.1 and its main after it | `Rawframe-Project/maul-nav` | `4b7dd797f6c6106c222356a57c3e96f4ca52b95f` | MIT | `include/`, `src/`, `cmake/`, `CMakeLists.txt`, `LICENSE` |
| Maul Audio 0.0.1 | `Rawframe-Project/maul-audio` release archive `maul-audio-0.0.1.tar.gz`, SHA-256 `17aa4f822fef7bbb0a6b1354155f0215b0df131738e8e2e27028f8864fb85719` | `b606c80ecb40e7aa3877aa0f45942026e6eaf683` (`v0.0.1`) | MIT | `include/`, `src/`, `cmake/`, `CMakeLists.txt`, `LICENSE` |
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

- **Maul2D and Maul3D** keep the engine's own `CMakeLists.txt` beside their sources; bring its source list in line with upstream's when it changes. The other Maul libraries build with their own CMake, configured by `third_party/maul_*.cmake`.
- **Maul Window and Maul Unicode** move together: Maul Window's pin names the Maul Unicode release it was made with. Maul snapshots and journals refuse another build, so both sides of anything that exchanges them need the same pin.
- **MsQuic and OpenSSL** move together: the OpenSSL revision is the one MsQuic pins as its submodule. Configure builds both once per machine with `tools/build_quic.sh` into `~/.cache/rawframe` (or `$RAWFRAME_DEPENDENCY_CACHE`), and again only when the revisions, the script or the compiler change. For the web build, OpenSSL's libcrypto alone is built for wasm32-wasi by `tools/build_openssl_wasm.sh`.
- **Zstandard** is part of the packing format: a new revision may change compressed bytes, so moving it is a new packer generation, never a silent change. Runtimes link the decoder only.
- **cgltf, miniaudio's decoders, stb_vorbis and the Opus encoder** are import tooling: they are linked into the cook, never into a runtime. The runtime builds only Maul Audio's device layer and the Opus decoder.
- **OpenType Sanitizer** rebuilds every font the cook takes. WOFF and WOFF 2.0 are refused before it.
- **CLDR** is data: `tools/generate_cldr.py` turns it into `modules/localization/src/generated/`, and no CLDR file is read at run time.
