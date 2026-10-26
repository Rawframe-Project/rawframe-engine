// The golden vectors of container generation 1 and packer generation 2
// (rawframe.build.2: Zstandard 1.5.7 at level 12, window log 22), in
// tests/golden/. A change to any of them is a new generation, never a silent
// one:
//
//   gear.txt            the FastCDC gear table: entry i is the first eight
//                       bytes of SHA-256 of the one byte i, big-endian
//   chunks.txt          where chunks end for `noise` inputs of the listed
//                       sizes; noise is SHA-256 of the decimal text of 0, 1,
//                       2 and so on, concatenated and cut to size
//   build.manifest      the canonical BuildManifest of the Build packed from
//                       cooked.h's output plus resource 4 (the decimal
//                       numbers from 0 up, each followed by a space, cut to
//                       1 MiB)
//   root.txt            that Build's root hash
//   build.manifest.sig  its signature envelope under the key whose seed is
//                       32 bytes of 0x2a, kid 2a2a2a2a2a2a2a2a
//   rawframe.keys       that key's publisher key set, updated_at 1790000000
//
// To regenerate them on purpose, run the test with RAWFRAME_WRITE_GOLDEN=1
// set, and commit the files with the new generation recorded beside the
// packer's parameters.

#include "cooked.h"
#include "rawframe/build/chunking.h"
#include "rawframe/build/publisher_key.h"

#include <algorithm>
#include <cstdlib>
#include <string>

using namespace rawframe;
using namespace rawframe::build;
using namespace rawframe::build::testing;

namespace {

const fs::path kGolden{RAWFRAME_BUILD_GOLDEN};

/// Whether `made` is the golden file `name`, byte for byte; writes it
/// instead when asked to regenerate. Reports the first difference.
bool golden(std::string_view name, std::string_view made) {
    const fs::path kPath = kGolden / name;
    if (std::getenv("RAWFRAME_WRITE_GOLDEN") != nullptr) {
        writeText(kPath, made);
        return true;
    }
    const std::string kExpected = readText(kPath);
    if (kExpected == made) {
        return true;
    }
    const auto kDiffer = std::ranges::mismatch(kExpected, made);
    std::fprintf(stderr,
                 "golden %.*s differs at byte %zu (expected %zu bytes, made %zu)\n",
                 static_cast<int>(name.size()),
                 name.data(),
                 static_cast<std::size_t>(kDiffer.in1 - kExpected.begin()),
                 kExpected.size(),
                 made.size());
    return false;
}

std::string hex16(std::uint64_t value) {
    std::string text(16, '0');
    for (int index = 15; index >= 0; --index) {
        text[static_cast<std::size_t>(index)] = "0123456789abcdef"[value & 0xFU];
        value >>= 4U;
    }
    return text;
}

} // namespace

RAWFRAME_TEST(TheGearTableIsGolden) {
    std::string text;
    for (const std::uint64_t kEntry : gearTable()) {
        text += hex16(kEntry) + "\n";
    }
    RAWFRAME_EXPECT(golden("gear.txt", text));
}

RAWFRAME_TEST(ChunkBoundariesAreGolden) {
    // Empty, one byte, exactly the minimum and one past it, a resource
    // between the target and the maximum, and 24 MiB: where each ends.
    std::string text;
    for (const std::size_t kSize : {std::size_t{0},
                                    std::size_t{1},
                                    kMinimumChunk,
                                    kMinimumChunk + 1,
                                    std::size_t{3} * 1024 * 1024,
                                    std::size_t{24} * 1024 * 1024}) {
        const std::string kNoise = noise(kSize);
        text += "noise " + std::to_string(kSize) + ":";
        for (const std::size_t kEnd : chunkEnds(bytesOf(kNoise))) {
            text += " " + std::to_string(kEnd);
        }
        text += "\n";
    }
    RAWFRAME_EXPECT(golden("chunks.txt", text));
}

RAWFRAME_TEST(ASpecifiedBuildIsGolden) {
    // The fixed cook output and resource 4, the decimal numbers from 0 up,
    // each followed by one space, to 1 MiB: compressible, but not so
    // uniformly that every level compresses it alike. Packed under the
    // fixed identity and signed by a key whose seed is 32 bytes of 0x2a
    // under kid 2a2a2a2a2a2a2a2a.
    Cooked cooked;
    std::string counted;
    for (std::uint64_t number = 0; counted.size() < std::size_t{1024} * 1024; ++number) {
        counted += std::to_string(number) + " ";
    }
    counted.resize(std::size_t{1024} * 1024);
    cooked.add(4, counted);
    cooked.prove(0);
    const Cooked& kCooked = cooked;
    PublisherKey key{.publisher = "rawframe", .kid = "2a2a2a2a2a2a2a2a", .seed = {}};
    key.seed.fill(std::byte{0x2a});
    const auto kPacked = kCooked.pack(kIdentity, &key);
    RAWFRAME_EXPECT(kPacked.has_value());
    if (!kPacked.has_value()) {
        return;
    }
    RAWFRAME_EXPECT(golden("build.manifest", readText(kCooked.output / "build.manifest")));
    RAWFRAME_EXPECT(golden("build.manifest.sig", readText(kCooked.output / "build.manifest.sig")));
    RAWFRAME_EXPECT(golden("root.txt", content::ContentDigest{.bytes = kPacked->root}.text() + "\n"));
    const auto kKeys = keySetOf(key, 1'790'000'000);
    const auto kKeysText =
        kKeys.has_value() ? signature::writePublisherKeySet(*kKeys) : result::Result<std::string>{std::string{}};
    RAWFRAME_EXPECT(kKeysText.has_value() && golden("rawframe.keys", *kKeysText));
}
