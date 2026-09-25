# SPEC-0021 golden vectors

Container generation 1, packer generation 2 (`rawframe.build.2`: Zstandard
1.5.7 at level 12, window log 22). `golden_test.cpp` recomputes every file
here and compares it byte for byte; a difference is a new generation.

| File | What it is |
| --- | --- |
| `gear.txt` | The FastCDC gear table: entry i is the first eight bytes of SHA-256 of the one byte i, big-endian, one 16-digit hexadecimal line each. |
| `chunks.txt` | Where chunks end for `noise` inputs of the listed sizes. `noise` is SHA-256 of the decimal text of 0, 1, 2, and so on, concatenated and cut to size. |
| `build.manifest` | The canonical BuildManifest of the Build packed from the tests' cook output (`cooked.h`) plus resource 4, the decimal numbers from 0 up each followed by a space, cut to 1 MiB, under the identity in `cooked.h`. |
| `root.txt` | That Build's root hash: SHA-256 of the canonical identity section. |
| `build.manifest.sig` | Its signature envelope under the key whose seed is 32 bytes of `0x2a`, kid `2a2a2a2a2a2a2a2a`. |
| `rawframe.keys` | The publisher key set of that key, `updated_at` 1790000000. |

Regenerate only on purpose: run the build tests with `RAWFRAME_WRITE_GOLDEN=1`
set, and record the new generation where the packer's parameters live.
