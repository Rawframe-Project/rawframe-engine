#pragma once

// The tests' cook output: fixed resources with their manifest and a receipt
// that proves them, as the cook would leave them. The golden vectors are
// made from it, so what it holds is specified here exactly: resource 1 is
// "bang", resource 2 is `noise` of 3 MiB, and resource 3 is the phrase in
// `Cooked` repeated to at least 1 MiB, all of type 9:9 as
// `rawframe.audio.wave`.

#include "rawframe/build/build.h"
#include "rawframe/content/manifest.h"
#include "rawframe/document/json.h"
#include "rawframe/test/test.h"

#include <filesystem>
#include <fstream>
#include <iterator>
#include <string>
#include <unistd.h>
#include <vector>

namespace rawframe::build::testing {

namespace fs = std::filesystem;

inline std::string readText(const fs::path& path) {
    std::ifstream file{path, std::ios::binary};
    return std::string{std::istreambuf_iterator<char>{file}, std::istreambuf_iterator<char>{}};
}

inline void writeText(const fs::path& path, std::string_view text) {
    fs::create_directories(path.parent_path());
    std::ofstream{path, std::ios::binary} << text;
}

inline std::span<const std::byte> bytesOf(std::string_view text) {
    return std::as_bytes(std::span{text.data(), text.size()});
}

/// Bytes with no structure a chunker could find, `size` long: SHA-256 of the
/// decimal text of 0, 1, 2, and so on, concatenated and cut to size.
inline std::string noise(std::size_t size) {
    std::string text;
    for (std::uint64_t block = 0; text.size() < size; ++block) {
        const base::Sha256Digest kDigest = base::sha256(std::to_string(block));
        text.append(reinterpret_cast<const char*>(kDigest.data()), kDigest.size());
    }
    text.resize(size);
    return text;
}

inline const BuildIdentity kIdentity{.subject = "rawframe/runners",
                                     .version = "0.1.0",
                                     .engine = "0.1.0",
                                     .platform = "linux",
                                     .architecture = "x86_64",
                                     .side = "client",
                                     .configuration = "build.development",
                                     .profile = "tool"};

/// A cook's output of a small sound, a three-megabyte one of noise, and a
/// megabyte of a repeated phrase that compresses well, with its manifest and
/// a receipt that proves it.
struct Cooked {
    fs::path base = fs::temp_directory_path() / ("rawframe-build-" + std::to_string(::getpid()));
    fs::path cooked = base / "cooked";
    fs::path output = base / "build";
    std::vector<content::ManifestEntry> entries;
    std::string large = noise(std::size_t{3} * 1024 * 1024);
    std::string repeated;

    Cooked() {
        fs::remove_all(base);
        add(1, "bang");
        add(2, large);
        while (repeated.size() < std::size_t{1024} * 1024) {
            repeated += "every shot sounds from where it was fired; ";
        }
        add(3, repeated);
        prove(0);
    }
    ~Cooked() {
        fs::remove_all(base);
    }
    Cooked(const Cooked&) = delete;
    Cooked& operator=(const Cooked&) = delete;

    void add(std::uint64_t id, std::string_view bytes) {
        const content::ContentDigest kDigest = content::ContentDigest::of(bytesOf(bytes));
        const std::string kLocator = "objects/" + kDigest.text().substr(7);
        writeText(cooked / kLocator, bytes);
        entries.push_back(
            content::ManifestEntry{.id = content::ResourceId{base::Bits128{.high = 0, .low = id}},
                                   .type = content::ResourceTypeId{base::Bits128{.high = 9, .low = 9}},
                                   .representation = *content::RepresentationId::parse("rawframe.audio.wave"),
                                   .byteLength = bytes.size(),
                                   .digest = kDigest,
                                   .locator = kLocator});
    }

    /// The manifest, and a receipt of `failures` naming it.
    void prove(std::int64_t failures, std::string_view named = {}) const {
        const std::string kManifest = content::writeManifest(entries);
        writeText(cooked / "content.manifest", kManifest);
        document::Value artifacts = document::Value::array();
        for (const content::ManifestEntry& entry : entries) {
            std::array<char, 32> id{};
            base::formatBits128Hex(entry.id.value, id);
            document::Value artifact = document::Value::object();
            artifact.add("resourceId", document::Value::string(std::string{id.data(), id.size()}));
            artifact.add("representation", document::Value::string(std::string{entry.representation.text()}));
            artifact.add("digest", document::Value::string(entry.digest.text()));
            artifact.add("byteLength", document::Value::integer(static_cast<std::int64_t>(entry.byteLength)));
            artifacts.push(std::move(artifact));
        }
        document::Value receipt = document::Value::object();
        receipt.add("kind", document::Value::string("cook.receipt"));
        receipt.add("formatVersion", document::Value::integer(1));
        receipt.add("manifest",
                    document::Value::string(named.empty() ? content::ContentDigest::of(bytesOf(kManifest)).text()
                                                          : std::string{named}));
        receipt.add("artifacts", std::move(artifacts));
        receipt.add("failures", document::Value::integer(failures));
        writeText(cooked / "cook.receipt", document::write(receipt));
    }

    [[nodiscard]] result::Result<BuildReport> pack(BuildIdentity identity = kIdentity,
                                                   const PublisherKey* signer = nullptr) const {
        return packBuild(
            BuildRequest{.cooked = cooked, .output = output, .identity = std::move(identity), .signer = signer});
    }
};

} // namespace rawframe::build::testing
