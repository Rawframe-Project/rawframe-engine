#include "codec.h"

namespace rawframe::world_snapshot::detail {

namespace {

void writeFingerprints(Output& out, const Fingerprints& fingerprints) {
    out.bytes(fingerprints.schema);
    out.bytes(fingerprints.package);
    out.bytes(fingerprints.projection);
    out.bytes(fingerprints.toolchain);
    out.bytes(fingerprints.profile);
}

bool readFingerprints(Input& in, Fingerprints& fingerprints) {
    return in.array(fingerprints.schema) && in.array(fingerprints.package) && in.array(fingerprints.projection) &&
           in.array(fingerprints.toolchain) && in.array(fingerprints.profile);
}

void writeTotals(Output& out, const Totals& totals) {
    out.u64(totals.entities);
    out.u64(totals.rows);
    out.u64(totals.references);
    out.u64(totals.decodedBytes);
}

bool readTotals(Input& in, Totals& totals) {
    return in.u64(totals.entities) && in.u64(totals.rows) && in.u64(totals.references) && in.u64(totals.decodedBytes);
}

} // namespace

Subject subjectOf(schema::ComponentTypeId id) noexcept {
    Subject subject{};
    for (std::size_t index = 0; index < 8; ++index) {
        subject[index] = static_cast<std::byte>((id.value.high >> (8U * (7 - index))) & 0xFFU);
        subject[8 + index] = static_cast<std::byte>((id.value.low >> (8U * (7 - index))) & 0xFFU);
    }
    return subject;
}

void writeWorldHeader(Output& out, const WorldHeader& header) {
    out.u64(header.tick);
    out.u64(header.rateNumerator);
    out.u64(header.rateDenominator);
    // Persistence namespaces: none until persistent identities exist (D29).
    out.u64(0);
    writeFingerprints(out, header.fingerprints);
    // Content catalogs: none until packages exist.
    out.u64(0);
    writeTotals(out, header.totals);
    // Feature bits: none in generation 1.
    out.u64(0);
}

result::Result<WorldHeader> readWorldHeader(std::span<const std::byte> payload) {
    Input in{payload};
    WorldHeader header;
    std::uint64_t namespaces = 0;
    std::uint64_t catalogs = 0;
    std::uint64_t features = 0;
    if (!in.u64(header.tick) || !in.u64(header.rateNumerator) || !in.u64(header.rateDenominator) ||
        !in.u64(namespaces) || !readFingerprints(in, header.fingerprints) || !in.u64(catalogs) ||
        !readTotals(in, header.totals) || !in.u64(features) || in.remaining() != 0) {
        return malformed("the world header is not its exact length");
    }
    if (namespaces != 0 || catalogs != 0 || features != 0) {
        return malformed("the world header names namespaces, catalogs, or features generation 1 does not have");
    }
    return header;
}

bool validModSet(std::span<const CheckpointMod> mods) noexcept {
    const auto kValid = [](const std::string& text) {
        return !text.empty() && text.size() <= kMaximumCheckpointModText;
    };
    for (std::size_t index = 0; index < mods.size(); ++index) {
        if (!kValid(mods[index].subject) || !kValid(mods[index].version) ||
            (index > 0 && mods[index - 1].subject >= mods[index].subject)) {
            return false;
        }
    }
    return mods.size() <= kMaximumCheckpointMods;
}

void writeModSet(Output& out, std::span<const CheckpointMod> mods) {
    for (const CheckpointMod& mod : mods) {
        for (const std::string* text : {&mod.subject, &mod.version}) {
            out.u32(static_cast<std::uint32_t>(text->size()));
            out.bytes(std::as_bytes(std::span{text->data(), text->size()}));
        }
    }
}

result::Result<std::vector<CheckpointMod>> readModSet(std::span<const std::byte> payload, std::uint64_t records) {
    if (records > kMaximumCheckpointMods) {
        return malformed("a checkpoint records more mods than the bound");
    }
    Input in{payload};
    std::vector<CheckpointMod> mods;
    for (std::uint64_t record = 0; record < records; ++record) {
        CheckpointMod& mod = mods.emplace_back();
        for (std::string* text : {&mod.subject, &mod.version}) {
            std::uint32_t length = 0;
            std::span<const std::byte> taken;
            if (!in.u32(length) || length > kMaximumCheckpointModText || !in.take(length, taken)) {
                return malformed("a recorded mod is short or past its bound");
            }
            text->assign(reinterpret_cast<const char*>(taken.data()), taken.size());
        }
    }
    if (in.remaining() != 0 || !validModSet(mods)) {
        return malformed("the recorded mods are out of order, repeated, empty, or not their exact length");
    }
    return mods;
}

void writeManifest(Output& out, const Manifest& manifest) {
    for (const ChunkHeader& entry : manifest.entries) {
        out.u64(entry.ordinal);
        out.u16(static_cast<std::uint16_t>(entry.kind));
        out.zeros(6);
        out.bytes(entry.subject);
        out.u64(entry.offset);
        out.u64(entry.storedSize);
        out.u64(entry.storedSize);
        out.u64(entry.recordCount);
        out.u16(kPayloadCodec);
        out.u16(kNoCompression);
        out.zeros(4);
        out.bytes(entry.digest);
    }
    writeFingerprints(out, manifest.fingerprints);
    writeTotals(out, manifest.totals);
}

result::Result<Manifest> readManifest(std::span<const std::byte> payload, std::uint64_t entries) {
    Input in{payload};
    Manifest manifest;
    for (std::uint64_t index = 0; index < entries; ++index) {
        ChunkHeader entry;
        std::uint16_t kind = 0;
        std::uint64_t decoded = 0;
        std::uint16_t codec = 0;
        std::uint16_t compression = 0;
        if (!in.u64(entry.ordinal) || !in.u16(kind) || !in.zeros(6) || !in.array(entry.subject) ||
            !in.u64(entry.offset) || !in.u64(entry.storedSize) || !in.u64(decoded) || !in.u64(entry.recordCount) ||
            !in.u16(codec) || !in.u16(compression) || !in.zeros(4) || !in.array(entry.digest)) {
            return malformed("a manifest entry is short or has a reserved byte set");
        }
        if (decoded != entry.storedSize || codec != kPayloadCodec || compression != kNoCompression) {
            return malformed("a manifest entry names a codec or compression generation 1 does not have");
        }
        entry.kind = static_cast<ChunkKind>(kind);
        manifest.entries.push_back(entry);
    }
    if (!readFingerprints(in, manifest.fingerprints) || !readTotals(in, manifest.totals) || in.remaining() != 0) {
        return malformed("the manifest is not its exact length");
    }
    return manifest;
}

} // namespace rawframe::world_snapshot::detail
