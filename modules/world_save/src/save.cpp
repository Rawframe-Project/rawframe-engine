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
    /// Offsets of its entity fields, in field order.
    std::vector<std::uint32_t> entities;
};

std::size_t widthOf(FieldKind kind) noexcept {
    switch (kind) {
    case FieldKind::I8:
    case FieldKind::U8:
    case FieldKind::Bool:
        return 1;
    case FieldKind::I16:
    case FieldKind::U16:
        return 2;
    case FieldKind::I32:
    case FieldKind::U32:
    case FieldKind::F32:
        return 4;
    case FieldKind::I64:
    case FieldKind::U64:
    case FieldKind::F64:
    case FieldKind::Entity:
        return 8;
    }
    return 0;
}

bool knownKind(std::uint8_t kind) noexcept {
    return kind >= static_cast<std::uint8_t>(FieldKind::I8) && kind <= static_cast<std::uint8_t>(FieldKind::Entity);
}

/// Whether a component's fields are named once, sized to a known kind,
/// inside a value of `size`, and apart.
bool fieldsFit(const std::vector<SavedField>& fields, std::size_t size) {
    if (fields.size() > kMaximumSavedFields) {
        return false;
    }
    for (std::size_t index = 0; index < fields.size(); ++index) {
        const SavedField& field = fields[index];
        if (field.name.empty() || field.name.size() > 255 || !knownKind(static_cast<std::uint8_t>(field.kind)) ||
            field.offset + widthOf(field.kind) > size) {
            return false;
        }
        for (std::size_t other = 0; other < index; ++other) {
            const SavedField& before = fields[other];
            if (before.name == field.name || (field.offset < before.offset + widthOf(before.kind) &&
                                              before.offset < field.offset + widthOf(field.kind))) {
                return false;
            }
        }
    }
    return true;
}

std::vector<std::uint32_t> entityOffsets(const std::vector<SavedField>& fields) {
    std::vector<std::uint32_t> offsets;
    for (const SavedField& field : fields) {
        if (field.kind == FieldKind::Entity) {
            offsets.push_back(field.offset);
        }
    }
    return offsets;
}

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
        if (!fieldsFit(component.fields, descriptor.size)) {
            return fail(result::ErrorClass::InvalidArgument,
                        SaveError::InvalidDeclaration,
                        "a saved component's fields are named once, of a known kind, apart, inside its value");
        }
        resolved.push_back(
            Resolved{.runtime = *runtime, .descriptor = &descriptor, .entities = entityOffsets(component.fields)});
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
        writer.number(static_cast<std::uint16_t>(component.fields.size()));
        for (const SavedField& field : component.fields) {
            writer.number(static_cast<std::uint8_t>(field.name.size()));
            writer.bytes(std::as_bytes(std::span{field.name}));
            writer.number(field.offset);
            writer.number(static_cast<std::uint8_t>(field.kind));
        }
    }
}

} // namespace

namespace {

/// The document of `kept`, each entity saved under the identity paired
/// with it, in ascending identity; references name what the World's
/// persistent entities are named.
result::Result<std::vector<std::byte>>
captureOf(world::World& world,
          const SaveDeclaration& declaration,
          base::Bits128 space,
          const SaveLimits& limits,
          const std::map<world::PersistentEntityId, world::EntityHandle>& kKept) {
    RAWFRAME_TRY_ASSIGN(const std::vector<Resolved> kResolved, resolve(declaration, world.registry()));
    RAWFRAME_TRY_ASSIGN(const auto kHeld, persistentEntities(world));
    std::map<world::EntityHandle, world::PersistentEntityId> names;
    for (const auto& [id, entity] : kHeld) {
        names.emplace(entity, id);
    }

    Writer body;
    std::uint32_t kept = 0;
    for (const auto& [id, entity] : kKept) {
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
            for (const std::uint32_t kOffset : kResolved[index].entities) {
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

} // namespace

result::Result<std::vector<std::byte>>
capture(world::World& world, const SaveDeclaration& declaration, base::Bits128 space, const SaveLimits& limits) {
    RAWFRAME_TRY_ASSIGN(const auto kHeld, persistentEntities(world));
    return captureOf(world, declaration, space, limits, kHeld);
}

result::Result<std::vector<std::byte>> captureEntity(world::World& world,
                                                     const SaveDeclaration& declaration,
                                                     base::Bits128 space,
                                                     world::EntityHandle entity,
                                                     world::PersistentEntityId as,
                                                     const SaveLimits& limits) {
    if (!world.alive(entity) || as.value == base::Bits128{}) {
        return fail(result::ErrorClass::InvalidArgument,
                    SaveError::InvalidDeclaration,
                    "an entity saved alone is alive and saved under an identity");
    }
    return captureOf(world, declaration, space, limits, {{as, entity}});
}

namespace {

/// A component as a save was written: its declaration there, and what it
/// becomes here.
struct Written {
    schema::ComponentTypeId id;
    std::uint32_t size = 0;
    std::vector<SavedField> fields;
    std::vector<std::uint32_t> entities;
    /// Its place in this declaration; none when it was dropped since.
    std::optional<std::size_t> declared;
    /// Whether it is laid out as this declaration lays it out.
    bool same = false;
};

bool isSigned(FieldKind kind) noexcept {
    return kind == FieldKind::I8 || kind == FieldKind::I16 || kind == FieldKind::I32 || kind == FieldKind::I64;
}

bool isUnsigned(FieldKind kind) noexcept {
    return kind == FieldKind::U8 || kind == FieldKind::U16 || kind == FieldKind::U32 || kind == FieldKind::U64;
}

/// Whether a value of `from` becomes one of `to` exactly: the same kind, an
/// integer into a wider one that holds every value, an integer into a real
/// that holds every value, or a real into a wider real.
bool widens(FieldKind from, FieldKind to) noexcept {
    if (from == to) {
        return true;
    }
    const std::size_t kFrom = widthOf(from);
    const std::size_t kTo = widthOf(to);
    if (isSigned(from)) {
        return (isSigned(to) && kTo > kFrom) || (to == FieldKind::F32 && kFrom <= 2) ||
               (to == FieldKind::F64 && kFrom <= 4);
    }
    if (isUnsigned(from)) {
        return ((isUnsigned(to) || isSigned(to)) && kTo > kFrom) || (to == FieldKind::F32 && kFrom <= 2) ||
               (to == FieldKind::F64 && kFrom <= 4);
    }
    return from == FieldKind::F32 && to == FieldKind::F64;
}

/// Writes the value of kind `from` at `in` as kind `to` at `out`; the kinds
/// widen.
void widen(const std::byte* in, FieldKind from, std::byte* out, FieldKind to) noexcept {
    if (from == to) {
        std::memcpy(out, in, widthOf(from));
        return;
    }
    std::uint64_t bits = 0;
    std::memcpy(&bits, in, widthOf(from));
    double real = 0;
    std::int64_t whole = 0;
    if (from == FieldKind::F32) {
        float single = 0;
        std::memcpy(&single, in, sizeof single);
        real = single;
    } else if (isSigned(from)) {
        const unsigned kShift = 64U - (8U * static_cast<unsigned>(widthOf(from)));
        whole = static_cast<std::int64_t>(bits << kShift) >> kShift;
        real = static_cast<double>(whole);
    } else {
        whole = static_cast<std::int64_t>(bits);
        real = static_cast<double>(bits);
    }
    if (to == FieldKind::F64) {
        std::memcpy(out, &real, sizeof real);
    } else if (to == FieldKind::F32) {
        const auto kSingle = static_cast<float>(real);
        std::memcpy(out, &kSingle, sizeof kSingle);
    } else {
        std::memcpy(out, &whole, widthOf(to));
    }
}

/// Reads one component of the declaration a save was written under.
result::Result<Written>
readComponent(Reader& reader, const SaveDeclaration& declaration, const std::vector<Resolved>& resolved) {
    Written read;
    RAWFRAME_TRY_ASSIGN(const base::Bits128 kId, reader.identity());
    read.id = schema::ComponentTypeId{kId};
    RAWFRAME_TRY_ASSIGN(const std::uint64_t kMark, reader.number<std::uint64_t>());
    RAWFRAME_TRY_ASSIGN(read.size, reader.number<std::uint32_t>());
    RAWFRAME_TRY_ASSIGN(const std::uint16_t kFields, reader.number<std::uint16_t>());
    if (read.size == 0 || read.size > (std::uint32_t{1} << 20U) || kFields > kMaximumSavedFields) {
        return malformed("a saved component is 1 byte to 1 MiB with at most 256 fields");
    }
    for (std::uint16_t index = 0; index < kFields; ++index) {
        SavedField field;
        RAWFRAME_TRY_ASSIGN(const std::uint8_t kLength, reader.number<std::uint8_t>());
        RAWFRAME_TRY_ASSIGN(const auto kName, reader.bytes(kLength));
        for (const std::byte kByte : kName) {
            field.name.push_back(static_cast<char>(kByte));
        }
        RAWFRAME_TRY_ASSIGN(field.offset, reader.number<std::uint32_t>());
        RAWFRAME_TRY_ASSIGN(const std::uint8_t kKind, reader.number<std::uint8_t>());
        if (!knownKind(kKind)) {
            return malformed("a saved field of no kind this engine knows");
        }
        field.kind = static_cast<FieldKind>(kKind);
        read.fields.push_back(std::move(field));
    }
    if (!fieldsFit(read.fields, read.size)) {
        return malformed("a saved component's fields overlap, repeat a name, or lie outside its value");
    }
    read.entities = entityOffsets(read.fields);
    const auto kDeclared = std::ranges::find(declaration.components, read.id, &SavedComponent::id);
    if (kDeclared == declaration.components.end()) {
        return read;
    }
    const auto kPlace = static_cast<std::size_t>(kDeclared - declaration.components.begin());
    read.declared = kPlace;
    read.same =
        kMark == kDeclared->mark && read.size == resolved[kPlace].descriptor->size && read.fields == kDeclared->fields;
    if (read.same) {
        return read;
    }
    // Migrated by field name: every field both have must widen, and a
    // component without fields on either side cannot be.
    bool fits = !read.fields.empty() && !kDeclared->fields.empty();
    for (const SavedField& field : kDeclared->fields) {
        const auto kWas = std::ranges::find(read.fields, field.name, &SavedField::name);
        fits = fits && (kWas == read.fields.end() || widens(kWas->kind, field.kind));
    }
    if (!fits) {
        return fail(result::ErrorClass::FailedPrecondition,
                    SaveError::Mismatch,
                    "a save's component changed in a way no migration covers: a field's kind narrowed, or it has no "
                    "fields");
    }
    return read;
}

/// A written value, and what its entity fields name, as the declared
/// component lays it out.
void convert(const Written& from,
             std::span<const std::byte> raw,
             const std::vector<world::PersistentEntityId>& named,
             const SavedComponent& to,
             const schema::ComponentDescriptor& descriptor,
             std::optional<std::vector<std::byte>>& value,
             std::vector<world::PersistentEntityId>& references) {
    if (from.same) {
        value.emplace(raw.begin(), raw.end());
        references = named;
        return;
    }
    value.emplace(descriptor.size, std::byte{0});
    references.clear();
    for (const SavedField& field : to.fields) {
        const auto kWas = std::ranges::find(from.fields, field.name, &SavedField::name);
        if (field.kind == FieldKind::Entity) {
            world::PersistentEntityId target;
            if (kWas != from.fields.end()) {
                const auto kAt = std::ranges::find(from.entities, kWas->offset) - from.entities.begin();
                target = named[static_cast<std::size_t>(kAt)];
            }
            references.push_back(target);
            continue;
        }
        if (kWas != from.fields.end()) {
            widen(raw.data() + kWas->offset, kWas->kind, value->data() + field.offset, field.kind);
        }
    }
}

} // namespace

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
    RAWFRAME_TRY_ASSIGN(const base::Bits128 kSpace, reader.identity());
    RAWFRAME_TRY_ASSIGN(const std::uint16_t kNameLength, reader.number<std::uint16_t>());
    RAWFRAME_TRY_ASSIGN(const auto kName, reader.bytes(kNameLength));
    if (kSpace != space || !std::ranges::equal(kName, std::as_bytes(std::span{declaration.document}))) {
        return fail(
            result::ErrorClass::FailedPrecondition, SaveError::Mismatch, "a save of another namespace or document");
    }

    // The declaration it was written under, and how each of its components
    // becomes one of this declaration's.
    RAWFRAME_TRY_ASSIGN(const std::uint32_t kComponents, reader.number<std::uint32_t>());
    if (kComponents == 0 || kComponents > kMaximumSavedComponents) {
        return malformed("a save keeps 1 to 64 components");
    }
    std::vector<Written> written;
    bool migrated = false;
    for (std::uint32_t index = 0; index < kComponents; ++index) {
        RAWFRAME_TRY_ASSIGN(Written read, readComponent(reader, declaration, kResolved));
        if (std::ranges::any_of(written, [&read](const Written& before) {
                return before.id == read.id;
            })) {
            return malformed("a save keeps a component once");
        }
        migrated = migrated || !read.same;
        written.push_back(std::move(read));
    }
    for (const SavedComponent& component : declaration.components) {
        migrated = migrated || std::ranges::none_of(written, [&component](const Written& each) {
                       return each.id == component.id;
                   });
    }

    RAWFRAME_TRY_ASSIGN(const std::uint32_t kCount, reader.number<std::uint32_t>());
    if (kCount > limits.maximumEntities) {
        return fail(result::ErrorClass::ResourceExhausted,
                    SaveError::LimitExceeded,
                    "a save holds more entities than its limit");
    }
    StagedSave staged{.entities = {}, .migrated = migrated};
    staged.entities.reserve(kCount);
    world::PersistentEntityId last;
    const std::uint64_t kWritten = written.size() == 64 ? ~std::uint64_t{0} : (std::uint64_t{1} << written.size()) - 1U;
    for (std::uint32_t index = 0; index < kCount; ++index) {
        RAWFRAME_TRY_ASSIGN(const base::Bits128 kId, reader.identity());
        const world::PersistentEntityId kEntity{kId};
        if (kId == base::Bits128{} || (index > 0 && !(last < kEntity))) {
            return malformed("a save's entities are named and in ascending identity");
        }
        last = kEntity;
        RAWFRAME_TRY_ASSIGN(const std::uint64_t kPresent, reader.number<std::uint64_t>());
        if (kPresent == 0 || (kPresent & ~kWritten) != 0) {
            return malformed("a saved entity has some of its document's components and no others");
        }
        std::vector<std::span<const std::byte>> raw(written.size());
        for (std::size_t component = 0; component < written.size(); ++component) {
            if ((kPresent & (std::uint64_t{1} << component)) == 0) {
                continue;
            }
            RAWFRAME_TRY_ASSIGN(raw[component], reader.bytes(written[component].size));
            for (const std::uint32_t kOffset : written[component].entities) {
                if (std::ranges::any_of(raw[component].subspan(kOffset, sizeof(world::EntityHandle)),
                                        [](std::byte held) {
                                            return held != std::byte{0};
                                        })) {
                    return malformed("a saved value's entity field holds bytes of its own");
                }
            }
        }
        std::vector<std::vector<world::PersistentEntityId>> named(written.size());
        for (std::size_t component = 0; component < written.size(); ++component) {
            for (std::size_t field = 0; !raw[component].empty() && field < written[component].entities.size();
                 ++field) {
                RAWFRAME_TRY_ASSIGN(const base::Bits128 kNamed, reader.identity());
                named[component].push_back(world::PersistentEntityId{kNamed});
            }
        }
        StagedSave::Entity entity{.id = kEntity, .values = {}, .references = {}};
        entity.values.resize(kResolved.size());
        entity.references.resize(kResolved.size());
        bool kept = false;
        for (std::size_t component = 0; component < written.size(); ++component) {
            const Written& kFrom = written[component];
            if (raw[component].empty() || !kFrom.declared.has_value()) {
                continue;
            }
            convert(kFrom,
                    raw[component],
                    named[component],
                    declaration.components[*kFrom.declared],
                    *kResolved[*kFrom.declared].descriptor,
                    entity.values[*kFrom.declared],
                    entity.references[*kFrom.declared]);
            kept = true;
        }
        // An entity whose every component was dropped is no longer kept.
        if (kept) {
            staged.entities.push_back(std::move(entity));
        }
    }
    if (reader.remaining() != 0) {
        return malformed("a save continues past its last entity");
    }
    return staged;
}

namespace {

/// Applies `staged`, with `bound`'s identity, if any, standing for its
/// entity.
result::Result<Applied> applyWith(const StagedSave& staged,
                                  const SaveDeclaration& declaration,
                                  world::World& world,
                                  std::optional<std::pair<world::PersistentEntityId, world::EntityHandle>> bound) {
    RAWFRAME_TRY_ASSIGN(const std::vector<Resolved> kResolved, resolve(declaration, world.registry()));
    const auto kPersistent = world.registry().find(world::Persistent::kComponentTypeId);
    if (!kPersistent.has_value()) {
        return fail(result::ErrorClass::InvalidArgument,
                    SaveError::InvalidDeclaration,
                    "a World without persistent identities cannot take a save");
    }
    RAWFRAME_TRY_ASSIGN(auto held, persistentEntities(world));
    if (bound.has_value() && !held.emplace(bound->first, bound->second).second) {
        return fail(result::ErrorClass::FailedPrecondition,
                    SaveError::DuplicateIdentity,
                    "an entity of the World already holds the identity a save is applied under");
    }
    const auto& kHeld = held;
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
            const std::vector<std::uint32_t>& fields = kResolved[component].entities;
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

} // namespace

result::Result<Applied> apply(const StagedSave& staged, const SaveDeclaration& declaration, world::World& world) {
    return applyWith(staged, declaration, world, std::nullopt);
}

result::Result<Applied> applyTo(const StagedSave& staged,
                                const SaveDeclaration& declaration,
                                world::World& world,
                                world::EntityHandle entity,
                                world::PersistentEntityId as) {
    if (!world.alive(entity) || staged.entities.size() > 1 ||
        (!staged.entities.empty() && staged.entities[0].id != as)) {
        return fail(result::ErrorClass::FailedPrecondition,
                    SaveError::Mismatch,
                    "a save applied to one entity holds that entity alone, under the identity asked for");
    }
    return applyWith(staged, declaration, world, std::pair{as, entity});
}

} // namespace rawframe::world_save
