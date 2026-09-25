#pragma once

// Curated game saves (ADR-0057), generation 1: a game declares a save
// document as the components it keeps of its persistent entities
// (world/persistent.h). Capture reads exactly that projection of a committed
// World into canonical bytes; reading checks every byte before anything
// happens and stages the result; applying stages it into a running World
// through one command buffer at a barrier, completely or not at all. Where
// the bytes are kept is a storage provider's business (store.h).
//
// The container, little-endian throughout:
//
//   magic "RFSAVE\0\0", format u32 (1)
//   namespace: 16 bytes, the persistence namespace identities belong to
//   document: u16 length, its name's bytes
//   components: u32 count, then each: type id 16 bytes, layout mark u64,
//     size u32, entity field count u32, each field's offset u32
//   entities: u32 count, then each, in ascending identity: identity 16
//     bytes, a u64 of which components it has, each present component's
//     value with its entity fields zeroed, then for each of those entity
//     fields, in order, the identity it names (16 bytes, nought for none)
//   digest: SHA-256 of everything before it
//
// An identity is written high word first, each word little-endian.

#include "rawframe/base/bits128.h"
#include "rawframe/result/result.h"
#include "rawframe/schema/registry.h"
#include "rawframe/world/persistent.h"
#include "rawframe/world/world.h"

#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <string>
#include <vector>

namespace rawframe::world_save {

inline constexpr std::uint32_t kSaveFormat = 1;
/// The most components one document keeps.
inline constexpr std::size_t kMaximumSavedComponents = 64;

/// One component a document keeps, with what it must be: its layout's mark,
/// so a changed shape is refused, and where its value holds entities.
struct SavedComponent {
    schema::ComponentTypeId id;
    std::uint64_t mark = 0;
    std::vector<std::uint32_t> entityFields;
};

struct SaveDeclaration {
    /// The document's name, at most 255 bytes.
    std::string document;
    std::vector<SavedComponent> components;
};

struct SaveLimits {
    std::size_t maximumEntities = std::size_t{1} << 16U;
    std::size_t maximumBytes = std::size_t{64} << 20U;
};

/// Captures `declaration`'s projection of `world`: every entity with a
/// persistent identity and at least one declared component. The World is
/// only read. Refused: a declaration that does not fit the registry, two
/// entities holding one identity, a value naming an entity that has none,
/// and a capture over the limits.
[[nodiscard]] result::Result<std::vector<std::byte>>
capture(world::World& world, const SaveDeclaration& declaration, base::Bits128 space, const SaveLimits& limits = {});

/// Captures `declaration`'s components of the one entity `entity`, saved
/// under the identity `as`, which it need not carry: a player's document,
/// kept under the player's identity. References name the World's persistent
/// entities, as in `capture`.
[[nodiscard]] result::Result<std::vector<std::byte>> captureEntity(world::World& world,
                                                                   const SaveDeclaration& declaration,
                                                                   base::Bits128 space,
                                                                   world::EntityHandle entity,
                                                                   world::PersistentEntityId as,
                                                                   const SaveLimits& limits = {});

/// A read save: every value checked, waiting to be applied.
struct StagedSave {
    struct Entity {
        world::PersistentEntityId id;
        /// Per declared component, its value if the entity has it.
        std::vector<std::optional<std::vector<std::byte>>> values;
        /// Per declared component, what each of its entity fields names.
        std::vector<std::vector<world::PersistentEntityId>> references;
    };
    std::vector<Entity> entities;
};

/// Reads `bytes` as a save of `declaration` in `space` and stages it.
/// Nothing is trusted: the digest first, then the format (`too_new` for a
/// later one), the namespace, document, and declaration, every bound, and
/// the order of entities.
[[nodiscard]] result::Result<StagedSave> read(std::span<const std::byte> bytes,
                                              const SaveDeclaration& declaration,
                                              const schema::SchemaRegistry& registry,
                                              base::Bits128 space,
                                              const SaveLimits& limits = {});

struct Applied {
    /// Entities the World held that took the saved values, and entities
    /// made for identities it did not hold.
    std::size_t updated = 0;
    std::size_t created = 0;
};

/// Applies `staged` to `world`, whose structure must be unlocked: an entity
/// holding a saved identity takes its saved values, and loses a declared
/// component the save says it lacks; an identity the World lacks gets a new
/// entity. Everything is checked before the World is touched: the
/// declaration against the registry, identities held twice, and every
/// reference naming what the save or the World holds. Then it applies as
/// one command buffer.
[[nodiscard]] result::Result<Applied>
apply(const StagedSave& staged, const SaveDeclaration& declaration, world::World& world);

/// Applies a save of one entity, captured by `captureEntity` under `as`, to
/// the live `entity`, which takes its values as `apply` would have an entity
/// holding `as`. Refused (`mismatch`) for a save holding another entity or
/// more than one.
[[nodiscard]] result::Result<Applied> applyTo(const StagedSave& staged,
                                              const SaveDeclaration& declaration,
                                              world::World& world,
                                              world::EntityHandle entity,
                                              world::PersistentEntityId as);

} // namespace rawframe::world_save
