#pragma once

// Authored scenes (ADR-0048; the field-level grammar is D92): a world's
// starting entities as a document people and tools write and review as
// text. One canonical form, so an unedited load and save is byte for byte
// the same:
//
//   {
//     "kind": "rawframe.scene",
//     "formatVersion": 1,
//     "schema": {
//       "game.position": "5f3a0c2d9e81b746"
//     },
//     "entities": [
//       {
//         "id": "0d3f8a3e-7c55-4b8e-9d0e-2a61f3c4b5a1",
//         "name": "spawn point",
//         "components": {
//           "game.position": {
//             "x": -40,
//             "y": 10
//           }
//         }
//       }
//     ]
//   }
//
// Each entity has a `SourceEntityId` (SPEC-0006: a UUID in canonical
// lowercase text, unique in the document, never derived from its name or
// place) and optionally a name for people. Its components are named as the
// game names them, in name order, each with the fields that differ from
// their default of nought, in name order: a number in its canonical text,
// `true`, or another entity of the document by its id. A field at its
// default is never written. `schema` holds, for every component the
// entities use and no other, the layout mark of the Kest type it was
// authored against (sixteen lowercase hex digits), so a changed type is
// found rather than read as something else. Entities keep the order they
// were authored in.
//
// A scene may also instance other scenes (D96), after its entities:
//
//     "instances": [
//       {
//         "scene": "52771075251e7361deaecf4939c72e56",
//         "entities": {
//           "<an entity of that scene>": "<its id in this scene>"
//         },
//         "overrides": [
//           {
//             "entity": "<its id in this scene>",
//             "component": "game.position",
//             "set": {
//               "x": 0
//             }
//           }
//         ]
//       }
//     ]
//
// `scene` is the source scene's resource identity. `entities` maps every
// entity the source scene has (its own and those of its instances, as it
// names them) to the id it has here, in the order of the source's ids; the
// ids here are unique among everything this scene holds. `overrides` is the
// closed typed patch, one entry for an entity and component at most, in
// that order: `set` gives fields new values, a default among them; `add`
// adds a component the source's entity lacks, with its non-default fields;
// `remove: true` removes one it has. An entry of an entity and
// `remove: true` alone removes the entity itself, and is its only entry;
// nothing left may name it (D120). Children are entities attached to their
// parent, so adding a child is an entity of this scene attached to one of
// the instance's, and moving one is setting its attachment's parent. What the source holds is checked when
// the instance is resolved. The member is left out when there are no
// instances, and a reference, here or in an override, may name any entity
// this scene holds. `schema` covers the components the overrides name too.
//
// The document is engine-neutral: which components exist, their fields,
// and whether a value fits are the game's to check when it spawns a scene.

#include "rawframe/base/bits128.h"
#include "rawframe/result/result.h"

#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

namespace rawframe::scene {

/// The resource type of a scene (D95), and its one representation: the
/// document's text in its one form.
inline constexpr base::Bits128 kSceneType = base::parseBits128Hex("afe08759c97c18977fcf1a5f4f320b2b").value;
inline constexpr std::string_view kSceneRepresentation = "rawframe.scene";

inline constexpr std::size_t kMaximumEntities = 65536;
inline constexpr std::size_t kMaximumComponents = 256;
inline constexpr std::size_t kMaximumFields = 1024;

/// A field's authored value.
struct FieldValue {
    enum class Kind : std::uint8_t {
        /// A number, in its canonical text (`number`).
        Number,
        /// The truth value `true`.
        True,
        /// Another entity of the document (`entity`).
        Entity,
        /// The truth value `false`: only in an override's `set`, where a
        /// default is a change.
        False,
    };

    Kind kind = Kind::Number;
    std::string number;
    base::Bits128 entity{};

    friend bool operator==(const FieldValue&, const FieldValue&) = default;
};

struct SceneField {
    std::string name;
    FieldValue value;

    friend bool operator==(const SceneField&, const SceneField&) = default;
};

struct SceneComponent {
    std::string name;
    std::vector<SceneField> fields;

    friend bool operator==(const SceneComponent&, const SceneComponent&) = default;
};

struct SceneEntity {
    /// Its SourceEntityId.
    base::Bits128 id{};
    /// For people; empty for none.
    std::string name;
    std::vector<SceneComponent> components;

    friend bool operator==(const SceneEntity&, const SceneEntity&) = default;
};

/// The layout mark a component was authored against.
struct SchemaMark {
    std::string component;
    std::uint64_t mark = 0;

    friend bool operator==(const SchemaMark&, const SchemaMark&) = default;
};

/// One entry of an instance's patch: what it does to one component of one
/// of the instance's entities.
struct Override {
    enum class Kind : std::uint8_t {
        Set,
        Add,
        /// A component removed; with no component, the entity itself.
        Remove,
    };

    /// The entity, by its id in the instancing scene.
    base::Bits128 entity{};
    /// Empty only for the entity's removal.
    std::string component;
    Kind kind = Kind::Set;
    /// Set: the fields given new values. Add: the component's non-default
    /// fields. Remove: none.
    std::vector<SceneField> fields;

    friend bool operator==(const Override&, const Override&) = default;
};

/// A source scene's entity, and the id it has in the instancing scene.
struct IdentityMapping {
    base::Bits128 source{};
    base::Bits128 instance{};

    friend bool operator==(const IdentityMapping&, const IdentityMapping&) = default;
};

struct SceneInstance {
    /// The source scene's resource identity.
    base::Bits128 scene{};
    /// In the order of the source's ids.
    std::vector<IdentityMapping> entities;
    /// In the order of entity, then component.
    std::vector<Override> overrides;

    friend bool operator==(const SceneInstance&, const SceneInstance&) = default;
};

struct Scene {
    std::vector<SchemaMark> schema;
    std::vector<SceneEntity> entities;
    std::vector<SceneInstance> instances;

    friend bool operator==(const Scene&, const Scene&) = default;
};

inline constexpr std::size_t kMaximumInstances = 4096;
inline constexpr std::size_t kMaximumOverrides = 65536;

/// The one form of `scene`; refused (`scene_invalid`) when the scene breaks
/// a rule above: an id nought or twice, components or fields out of name
/// order or twice, a field at its default outside an override's `set`, an
/// entity reference to no entity the scene holds, a mapping or patch out of
/// order or naming an entity the instance does not map, a schema that is
/// not exactly the components used, or a limit passed.
[[nodiscard]] result::Result<std::string> writeScene(const Scene& scene);

/// Reads a scene, refusing (`scene_invalid`) anything `writeScene` would not
/// have written byte for byte.
[[nodiscard]] result::Result<Scene> readScene(std::string_view text);

} // namespace rawframe::scene
