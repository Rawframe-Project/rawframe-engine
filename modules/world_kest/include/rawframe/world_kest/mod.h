#pragma once

// A mod's description (SPEC-0042's contribution declaration, D177): the one
// game it targets, the Mod API versions it was written against, and what it
// contributes to that game's extension points. A `data` contribution is a
// scene whose every entity holds the point's component and nothing else, so
// the values a mod adds are read, checked, and migrated as a game's own
// scenes are (ADR-0048).
//
//   target <publisher/name>
//   modapi <constraint>...        >=2 <4, =3, or 3 alone
//   contribute <point> <scene>    once for each scene a point takes

#include "rawframe/result/result.h"
#include "rawframe/world_kest/game.h"

#include <cstdint>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace rawframe::world_kest {

/// One bound of a Mod API range: `>=`, `<=`, `=`, `<`, or `>` a version.
struct ModApiBound {
    enum class Relation : std::uint8_t {
        AtLeast,
        AtMost,
        Exactly,
        Below,
        Above,
    };
    Relation relation = Relation::Exactly;
    std::uint32_t version = 0;
};

struct ModContribution {
    /// The point's name in the target's Mod API namespace.
    std::string point;
    /// The scene, beside the description, whose entities are the values.
    std::string scene;
};

struct ModDescription {
    std::string target;
    /// Every bound holds of a version the mod accepts.
    std::vector<ModApiBound> modApi;
    std::vector<ModContribution> contributions;
};

/// The most lines a mod description may have.
inline constexpr std::size_t kMaximumModLines = 1024;

/// Parses a description. Refuses (`invalid_argument`, `BadGameLine`, with
/// the line as context) an unknown keyword, a target or range missing, given
/// twice, or outside its grammar, a range no version satisfies, and a
/// contribution named twice.
[[nodiscard]] result::Result<ModDescription> parseMod(std::string_view text);

/// Whether `version` of a game's Mod API satisfies every bound.
[[nodiscard]] bool accepts(const std::vector<ModApiBound>& range, std::uint32_t version) noexcept;

/// A mod as a Composition names it: its subject and its description.
struct ComposedMod {
    std::string subject;
    ModDescription description;
};

/// SPEC-0042's Composition-build validation (D179): whether the game
/// `game`, of subject `gameSubject`, takes `mods`, whose own scenes hold the
/// components `gameHolds`. Refused (`invalid_argument`, `ModRefused`) for:
/// - any mod of a closed game, or one a curated game does not approve;
/// - a mod targeting another game, or whose range the game's version is
///   outside;
/// - a contribution to a point the game does not declare;
/// - two claimants of an exclusive point, every one named;
/// - a required point no mod fills and whose component none of the game's
///   own scenes hold.
[[nodiscard]] result::Status checkMods(const GameDescription& game,
                                       std::string_view gameSubject,
                                       std::span<const ComposedMod> mods,
                                       std::span<const std::string> gameHolds);

} // namespace rawframe::world_kest
