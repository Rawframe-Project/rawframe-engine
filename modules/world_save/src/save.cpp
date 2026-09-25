#include "rawframe/world_save/save.h"

#include "rawframe/base/sha256.h"
#include "rawframe/world/column_query.h"
#include "rawframe/world/command_buffer.h"
#include "rawframe/world_save/errors.h"

#include <algorithm>
#include <array>
#include <cstring>
#include <map>

namespace rawframe::world_save {

namespace {

constexpr std::array<std::byte, 8> kMagic = {std::byte{'R'},
                                             std::byte{'F'},
                                             std::byte{'S'},
                                             std::byte{'A'},
                                             std::byte{'V'},
                                             std::byte{'E'},
                                             std::byte{0},
                                             std::byte{0}};
constexpr std::size_t kDigestBytes = 32;
constexpr std::size_t kMaximumDocumentName = 255;

std::unexpected<result::Error> fail(result::ErrorClass errorClass, SaveError error, std::string_view why) {
    return result::fail(errorClass, kSaveDomain, code(error), why);
}

std::unexpected<result::Error> malformed(std::string_view why) {
    return fail(result::ErrorClass::InvalidArgument, SaveError::Malformed, why);
}

class Writer {
public:
    void bytes(std::span<const std::byte> from) {
        out_.insert(out_.end(), from.begin(), from.end());
    }
    template <typename T> void number(T value) {
        for (std::size_t index = 0; index < sizeof(T); ++index) {
            out_.push_back(static_cast<std::byte>((static_cast<std::uint64_t>(value) >> (8U * index)) & 0xFFU));
        }
    }
    void identity(base::Bits128 value) {
        number(value.high);
        number(value.low);
    }
    std::vector<std::byte>& out() noexcept {
        return out_;
    }

private:
    std::vector<std::byte> out_;
};

class Reader {
public:
    explicit Reader(std::span<const std::byte> in) noexcept : in_(in) {
    }
    result::Result<std::span<const std::byte>> bytes(std::size_t count) {
        if (count > in_.size() - at_) {
            return malformed("a save ends before its last field");
        }
        const auto kTaken = in_.subspan(at_, count);
        at_ += count;
        return kTaken;
    }
    template <typename T> result::Result<T> number() {
        RAWFRAME_TRY_ASSIGN(const auto kTaken, bytes(sizeof(T)));
        std::uint64_t value = 0;
        for (std::size_t index = 0; index < sizeof(T); ++index) {
            value |= std::to_integer<std::uint64_t>(kTaken[index]) << (8U * index);
        }
        return static_cast<T>(value);
    }
    result::Result<base::Bits128> identity() {
        RAWFRAME_TRY_ASSIGN(const std::uint64_t kHigh, number<std::uint64_t>());
        RAWFRAME_TRY_ASSIGN(const std::uint64_t kLow, number<std::uint64_t>());
        return base::Bits128{.high = kHigh, .low = kLow};
    }
    [[nodiscard]] std::size_t remaining() const noexcept {
        return in_.size() - at_;
    }

private:
    std::span<const std::byte> in_;
    std::size_t at_ = 0;
};

/// A declared component as the registry has it.
struct Resolved {
    schema::ComponentRuntimeId runtime;
    const schema::ComponentDescriptor* descriptor = nullptr;
};

result::Result<std::vector<Resolved>> resolve(const SaveDeclaration& declaration,
                                              const schema::SchemaRegistry& registry) {
    if (declaration.components.empty() || declaration.components.size() > kMaximumSavedComponents ||
        declaration.document.empty() || declaration.document.size() > kMaximumDocumentName) {
        return fail(result::ErrorClass::InvalidArgument,
                    SaveError::InvalidDeclaration,
                    "a save document has a name of 1 to 255 bytes and 1 to 64 components");
    }
    std::vector<Resolved> resolved;
    for (const SavedComponent& component : declaration.components) {
        if (std::ranges::count(declaration.components, component.id, &SavedComponent::id) != 1) {
            return fail(result::ErrorClass::InvalidArgument,
                        SaveError::InvalidDeclaration,
                        "a save document keeps a component once");
        }
        auto runtime = registry.find(component.id);
        if (!runtime.has_value()) {
            return fail(result::ErrorClass::InvalidArgument,
                        SaveError::InvalidDeclaration,
                        "a save document keeps a component the World does not have");
        }
        const schema::ComponentDescriptor& descriptor = registry.descriptor(*runtime);
        if (!descriptor.plainData || descriptor.size == 0) {
            return fail(result::ErrorClass::InvalidArgument,
                        SaveError::InvalidDeclaration,
                        "a saved component is plain data with a value");
        }
        for (const std::uint32_t kOffset : component.entityFields) {
            if (kOffset + sizeof(world::EntityHandle) > descriptor.size) {
                return fail(result::ErrorClass::InvalidArgument,
                            SaveError::InvalidDeclaration,
                            "a saved component's entity field lies outside its value");
            }
        }
        resolved.push_back(Resolved{.runtime = *runtime, .descriptor = &descriptor});
    }
    return resolved;
}

/// Every named persistent entity of the World, by identity; refused if two
/// hold one.
result::Result<std::map<world::PersistentEntityId, world::EntityHandle>> persistentEntities(world::World& world) {
    std::map<world::PersistentEntityId, world::EntityHandle> held;
    const auto kId = world.registry().find(world::Persistent::kComponentTypeId);
    if (!kId.has_value()) {
        return held;
    }
    const std::array<world::ColumnTerm, 1> kTerms = {world::ColumnTerm{*kId, world::Access::Read}};
    RAWFRAME_TRY_ASSIGN(world::ColumnQuery query, world::ColumnQuery::resolve(kTerms, world.registry()));
    bool twice = false;
    query.forEachChunk(world, [&held, &twice](const world::ColumnChunk& chunk) {
        for (std::size_t row = 0; row < chunk.entities.size(); ++row) {
            world::Persistent value;
            std::memcpy(static_cast<void*>(&value), chunk.columns[0] + (row * sizeof(world::Persistent)), sizeof value);
            if (value.named()) {
                twice = !held.emplace(value.id(), chunk.entities[row]).second || twice;
            }
        }
    });
    if (twice) {
        return fail(result::ErrorClass::FailedPrecondition,
                    SaveError::DuplicateIdentity,
                    "two entities of the World hold one persistent identity");
    }
    return held;
}

void writeHeader(Writer& writer,
                 const SaveDeclaration& declaration,
                 const std::vector<Resolved>& resolved,
                 base::Bits128 space) {
    writer.bytes(kMagic);
    writer.number(kSaveFormat);
    writer.identity(space);
    writer.number(static_cast<std::uint16_t>(declaration.document.size()));
    writer.bytes(std::as_bytes(std::span{declaration.document}));
    writer.number(static_cast<std::uint32_t>(declaration.components.size()));
    for (std::size_t index = 0; index < resolved.size(); ++index) {
        const SavedComponent& component = declaration.components[index];
        writer.identity(component.id.value);
        writer.number(component.mark);
        writer.number(static_cast<std::uint32_t>(resolved[index].descriptor->size));
        writer.number(static_cast<std::uint32_t>(component.entityFields.size()));
        for (const std::uint32_t kOffset : component.entityFields) {
            writer.number(kOffset);
        }
    }
}

} // namespace

result::Result<std::vector<std::byte>>
capture(world::World& world, const SaveDeclaration& declaration, base::Bits128 space, const SaveLimits& limits) {
    RAWFRAME_TRY_ASSIGN(const std::vector<Resolved> kResolved, resolve(declaration, world.registry()));
    RAWFRAME_TRY_ASSIGN(const auto kHeld, persistentEntities(world));
    std::map<world::EntityHandle, world::PersistentEntityId> names;
    for (const auto& [id, entity] : kHeld) {
        names.emplace(entity, id);
    }

    Writer body;
    std::uint32_t kept = 0;
    for (const auto& [id, entity] : kHeld) {
        std::uint64_t present = 0;
        for (std::size_t index = 0; index < kResolved.size(); ++index) {
            present |= world.hasErased(entity, kResolved[index].runtime) ? std::uint64_t{1} << index : 0U;
        }
        if (present == 0) {
            continue;
        }
        if (++kept > limits.maximumEntities) {
            return fail(result::ErrorClass::ResourceExhausted,
                        SaveError::LimitExceeded,
                        "a save holds more entities than its limit");
        }
        body.identity(id.value);
        body.number(present);
        std::vector<world::PersistentEntityId> references;
        for (std::size_t index = 0; index < kResolved.size(); ++index) {
            if ((present & (std::uint64_t{1} << index)) == 0) {
                continue;
            }
            const auto* value = static_cast<const std::byte*>(world.getErased(entity, kResolved[index].runtime));
            std::vector<std::byte> bytes(value, value + kResolved[index].descriptor->size);
            for (const std::uint32_t kOffset : declaration.components[index].entityFields) {
                world::EntityHandle named;
                std::memcpy(&named, bytes.data() + kOffset, sizeof named);
                std::memset(bytes.data() + kOffset, 0, sizeof named);
                if (named.isNull()) {
                    references.push_back(world::PersistentEntityId{});
                    continue;
                }
                const auto kName = names.find(named);
                if (kName == names.end()) {
                    return fail(result::ErrorClass::FailedPrecondition,
                                SaveError::UnnamedReference,
                                "a saved value names an entity that has no persistent identity");
                }
                references.push_back(kName->second);
            }
            body.bytes(bytes);
        }
        for (const world::PersistentEntityId& reference : references) {
            body.identity(reference.value);
        }
    }

    Writer out;
    writeHeader(out, declaration, kResolved, space);
    out.number(kept);
    out.bytes(body.out());
    const base::Sha256Digest kDigest = base::sha256(out.out());
    out.bytes(kDigest);
    if (out.out().size() > limits.maximumBytes) {
        return fail(result::ErrorClass::ResourceExhausted, SaveError::LimitExceeded, "a save is larger than its limit");
    }
    return std::move(out.out());
}

result::Result<StagedSave> read(std::span<const std::byte> bytes,
                                const SaveDeclaration& declaration,
                                const schema::SchemaRegistry& registry,
                                base::Bits128 space,
                                const SaveLimits& limits) {
    if (bytes.size() > limits.maximumBytes) {
        return fail(result::ErrorClass::ResourceExhausted, SaveError::LimitExceeded, "a save is larger than its limit");
    }
    if (bytes.size() < kMagic.size() + sizeof(std::uint32_t) + kDigestBytes) {
        return malformed("a save is shorter than its header and digest");
    }
    const auto kBody = bytes.first(bytes.size() - kDigestBytes);
    const base::Sha256Digest kDigest = base::sha256(kBody);
    if (!std::ranges::equal(kDigest, bytes.last(kDigestBytes))) {
        return fail(result::ErrorClass::DataLoss, SaveError::DigestMismatch, "a save's digest does not match it");
    }
    RAWFRAME_TRY_ASSIGN(const std::vector<Resolved> kResolved, resolve(declaration, registry));

    Reader reader{kBody};
    RAWFRAME_TRY_ASSIGN(const auto kMagicRead, reader.bytes(kMagic.size()));
    if (!std::ranges::equal(kMagicRead, kMagic)) {
        return malformed("not a save");
    }
    RAWFRAME_TRY_ASSIGN(const std::uint32_t kFormat, reader.number<std::uint32_t>());
    if (kFormat > kSaveFormat) {
        return fail(result::ErrorClass::FailedPrecondition, SaveError::TooNew, "a save of a later format");
    }
    if (kFormat != kSaveFormat) {
        return malformed("a save of no format this engine knows");
    }
    // What it says it is must be exactly what is asked for.
    Writer expected;
    writeHeader(expected, declaration, kResolved, space);
    const auto kHeader = std::span{expected.out()}.subspan(kMagic.size() + sizeof(std::uint32_t));
    RAWFRAME_TRY_ASSIGN(const auto kHeaderRead, reader.bytes(std::min(kHeader.size(), reader.remaining())));
    if (!std::ranges::equal(kHeaderRead, kHeader)) {
        return fail(result::ErrorClass::FailedPrecondition,
                    SaveError::Mismatch,
                    "a save of another namespace, document, or declaration");
    }

    RAWFRAME_TRY_ASSIGN(const std::uint32_t kCount, reader.number<std::uint32_t>());
    if (kCount > limits.maximumEntities) {
        return fail(result::ErrorClass::ResourceExhausted,
                    SaveError::LimitExceeded,
                    "a save holds more entities than its limit");
    }
    StagedSave staged;
    staged.entities.reserve(kCount);
    for (std::uint32_t index = 0; index < kCount; ++index) {
        StagedSave::Entity& entity = staged.entities.emplace_back();
        RAWFRAME_TRY_ASSIGN(const base::Bits128 kId, reader.identity());
        entity.id = world::PersistentEntityId{kId};
        if (kId == base::Bits128{} || (index > 0 && !(staged.entities[index - 1].id < entity.id))) {
            return malformed("a save's entities are named and in ascending identity");
        }
        RAWFRAME_TRY_ASSIGN(const std::uint64_t kPresent, reader.number<std::uint64_t>());
        const std::uint64_t kDeclared =
            kResolved.size() == 64 ? ~std::uint64_t{0} : (std::uint64_t{1} << kResolved.size()) - 1U;
        if (kPresent == 0 || (kPresent & ~kDeclared) != 0) {
            return malformed("a saved entity has some of its document's components and no others");
        }
        entity.values.resize(kResolved.size());
        entity.references.resize(kResolved.size());
        for (std::size_t component = 0; component < kResolved.size(); ++component) {
            if ((kPresent & (std::uint64_t{1} << component)) == 0) {
                continue;
            }
            RAWFRAME_TRY_ASSIGN(const auto kValue, reader.bytes(kResolved[component].descriptor->size));
            entity.values[component].emplace(kValue.begin(), kValue.end());
            for (const std::uint32_t kOffset : declaration.components[component].entityFields) {
                if (std::ranges::any_of(kValue.subspan(kOffset, sizeof(world::EntityHandle)), [](std::byte held) {
                        return held != std::byte{0};
                    })) {
                    return malformed("a saved value's entity field holds bytes of its own");
                }
            }
        }
        for (std::size_t component = 0; component < kResolved.size(); ++component) {
            if (!entity.values[component].has_value()) {
                continue;
            }
            for (std::size_t field = 0; field < declaration.components[component].entityFields.size(); ++field) {
                RAWFRAME_TRY_ASSIGN(const base::Bits128 kNamed, reader.identity());
                entity.references[component].push_back(world::PersistentEntityId{kNamed});
            }
        }
    }
    if (reader.remaining() != 0) {
        return malformed("a save continues past its last entity");
    }
    return staged;
}

result::Result<Applied> apply(const StagedSave& staged, const SaveDeclaration& declaration, world::World& world) {
    RAWFRAME_TRY_ASSIGN(const std::vector<Resolved> kResolved, resolve(declaration, world.registry()));
    const auto kPersistent = world.registry().find(world::Persistent::kComponentTypeId);
    if (!kPersistent.has_value()) {
        return fail(result::ErrorClass::InvalidArgument,
                    SaveError::InvalidDeclaration,
                    "a World without persistent identities cannot take a save");
    }
    RAWFRAME_TRY_ASSIGN(const auto kHeld, persistentEntities(world));
    std::map<world::PersistentEntityId, std::size_t> saved;
    for (std::size_t index = 0; index < staged.entities.size(); ++index) {
        saved.emplace(staged.entities[index].id, index);
    }
    // Every reference names something before anything changes.
    std::size_t valueBytes = 0;
    std::size_t commands = 0;
    for (const StagedSave::Entity& entity : staged.entities) {
        commands += 2 + kResolved.size();
        valueBytes += sizeof(world::Persistent) + alignof(std::max_align_t);
        for (std::size_t component = 0; component < kResolved.size(); ++component) {
            if (entity.values[component].has_value()) {
                valueBytes += kResolved[component].descriptor->size + alignof(std::max_align_t);
            }
            for (const world::PersistentEntityId& named : entity.references[component]) {
                if (named.value != base::Bits128{} && !saved.contains(named) && !kHeld.contains(named)) {
                    return fail(result::ErrorClass::FailedPrecondition,
                                SaveError::UnknownReference,
                                "a save names an identity neither it nor the World holds");
                }
            }
        }
    }

    world::CommandBuffer buffer{
        world::CommandBufferSettings{.maximumCommands = commands, .maximumValueBytes = valueBytes}};
    Applied applied;
    // Every new entity first, so a value may name any of them.
    std::vector<world::CommandTarget> targets;
    targets.reserve(staged.entities.size());
    for (const StagedSave::Entity& entity : staged.entities) {
        const auto kFound = kHeld.find(entity.id);
        if (kFound != kHeld.end()) {
            targets.emplace_back(kFound->second);
            ++applied.updated;
            continue;
        }
        RAWFRAME_TRY_ASSIGN(const world::PendingEntity kMade, buffer.create());
        targets.emplace_back(kMade);
        ++applied.created;
    }
    // What a reference becomes: nothing, an entity the save updates or
    // makes, or one the World holds that the save does not.
    const auto kHandleOf = [&](const world::PersistentEntityId& named) {
        if (named.value == base::Bits128{}) {
            return world::EntityHandle{};
        }
        const auto kSaved = saved.find(named);
        if (kSaved == saved.end()) {
            return kHeld.at(named);
        }
        const world::CommandTarget& target = targets[kSaved->second];
        return std::holds_alternative<world::EntityHandle>(target)
                   ? std::get<world::EntityHandle>(target)
                   : world::pendingReference(std::get<world::PendingEntity>(target));
    };
    const schema::ComponentDescriptor& kPersistentDescriptor = world.registry().descriptor(*kPersistent);
    for (std::size_t index = 0; index < staged.entities.size(); ++index) {
        const StagedSave::Entity& entity = staged.entities[index];
        if (std::holds_alternative<world::PendingEntity>(targets[index])) {
            const world::Persistent kName{.high = entity.id.value.high, .low = entity.id.value.low};
            RAWFRAME_TRY(buffer.insertBytes(
                targets[index], *kPersistent, kPersistentDescriptor, std::as_bytes(std::span{&kName, 1})));
        }
        for (std::size_t component = 0; component < kResolved.size(); ++component) {
            if (!entity.values[component].has_value()) {
                RAWFRAME_TRY(buffer.removeErased(targets[index], kResolved[component].runtime));
                continue;
            }
            std::vector<std::byte> value = *entity.values[component];
            const std::vector<std::uint32_t>& fields = declaration.components[component].entityFields;
            std::vector<std::size_t> offsets;
            for (std::size_t field = 0; field < fields.size(); ++field) {
                const world::EntityHandle kNamed = kHandleOf(entity.references[component][field]);
                std::memcpy(value.data() + fields[field], &kNamed, sizeof kNamed);
                offsets.push_back(fields[field]);
            }
            RAWFRAME_TRY(buffer.insertBytes(
                targets[index], kResolved[component].runtime, *kResolved[component].descriptor, value, offsets));
        }
    }
    RAWFRAME_TRY(world.apply(buffer));
    return applied;
}

} // namespace rawframe::world_save
