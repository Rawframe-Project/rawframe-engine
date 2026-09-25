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

struct Scene {
    std::vector<SchemaMark> schema;
    std::vector<SceneEntity> entities;

    friend bool operator==(const Scene&, const Scene&) = default;
};

/// The one form of `scene`; refused (`scene_invalid`) when the scene breaks
/// a rule above: an id nought or twice, components or fields out of name
/// order or twice, a field at its default, an entity reference to no entity
/// of the document, a schema that is not exactly the components used, or a
/// limit passed.
[[nodiscard]] result::Result<std::string> writeScene(const Scene& scene);

/// Reads a scene, refusing (`scene_invalid`) anything `writeScene` would not
/// have written byte for byte.
[[nodiscard]] result::Result<Scene> readScene(std::string_view text);

} // namespace rawframe::scene
