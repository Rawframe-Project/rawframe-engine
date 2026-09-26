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
//   program <file>                the Kest program its handlers are in
//   handle <point> <function>     a function of it as an event's handler
//   provide <point> <function>    a function of it as a service's provider
//   replace <point> <function>    a function of it in place of a game system's
//
// A mod with handlers, providers, or replacements names one program, compiled from the Kest sources of
// the `kest.project` beside it and run on a machine of its own under Kest's
// untrusted profile (D181).

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

struct ModHandler {
    /// The event point's name in the target's Mod API namespace.
    std::string point;
    /// The function of the mod's program that handles it.
    std::string function;
};

/// A function of a mod's program that answers a service point's calls: it
/// takes `values: [T]` and rewrites `values[0]` (D199).
struct ModProvider {
    /// The service point's name in the target's Mod API namespace.
    std::string point;
    std::string function;
};

/// A function of a mod's program that runs in place of the game system a
/// replacement point names: it takes the system's columns (D200).
struct ModReplacement {
    std::string point;
    std::string function;
};

struct ModDescription {
    std::string target;
    /// Every bound holds of a version the mod accepts.
    std::vector<ModApiBound> modApi;
    std::vector<ModContribution> contributions;
    /// The program its handlers, providers, and replacements are in; empty
    /// for a mod with none.
    std::string program;
    std::vector<ModHandler> handlers;
    std::vector<ModProvider> providers;
    std::vector<ModReplacement> replacements;
};

/// The most lines a mod description may have.
inline constexpr std::size_t kMaximumModLines = 1024;

// SPEC-0042's named limit points, valued here (D195). A point the engine
// does not build yet (capability grants, mod migrations) has no value, and
// no mod can use it.

/// `mod_descriptor_bytes_max`: checked before anything is parsed.
inline constexpr std::size_t kMaximumModDescriptorBytes = 64 * 1024;
/// `contributions_per_mod_max`: its `contribute`, `handle`, `provide`, and
/// `replace` lines.
inline constexpr std::size_t kMaximumModContributions = 256;
/// `contributions_per_point_max`: scenes given to one data point by every
/// mod of a Composition.
inline constexpr std::size_t kMaximumPointContributions = 1024;
/// `mod_event_handlers_per_event_max`: handlers of one event point by every
/// mod of a Composition, each a system run every tick.
inline constexpr std::size_t kMaximumEventHandlers = 64;

/// Parses a description. Refuses (`invalid_argument`, `BadGameLine`, with
/// the line as context) a description past its size limit, an unknown
/// keyword, a target or range missing, given twice, or outside its grammar,
/// a range no version satisfies, a contribution, handler, provider, or
/// replacement named twice, more of them than the limit, any of the last
/// three without a program, and a program without any.
[[nodiscard]] result::Result<ModDescription> parseMod(std::string_view text);

/// Whether `version` of a game's Mod API satisfies every bound.
[[nodiscard]] bool accepts(const std::vector<ModApiBound>& range, std::uint32_t version) noexcept;

/// A mod as a Composition names it: its subject and its description.
struct ComposedMod {
    std::string subject;
    ModDescription description;
};

/// A mod's claims on an exclusive point a game's `prefer` line gave to
/// another mod (D202): taken no further.
struct SetAsideClaim {
    std::string mod;
    std::string point;
};

/// SPEC-0042's Composition-build validation (D179): whether the game
/// `game`, of subject `gameSubject`, takes `mods`, whose own scenes hold the
/// components `gameHolds`. Refused (`invalid_argument`, `ModRefused`) for:
/// - any mod of a closed game, or one a curated game does not approve;
/// - a mod targeting another game, or whose range the game's version is
///   outside;
/// - values for a point the game does not declare as a data point, a
///   handler for one it does not declare as an event point, a provider for
///   one it does not declare as a service point, or a replacement for one
///   it does not declare as a replacement point;
/// - more mods than a Composition names, or more claimants of one point
///   than its limit;
/// - two claimants of an exclusive point, every one named, unless the game
///   prefers one of them that makes one claim: then every other mod's
///   claims on it are what comes back, to be set aside;
/// - a required point no mod fills and whose component none of the game's
///   own scenes hold.
[[nodiscard]] result::Result<std::vector<SetAsideClaim>> checkMods(const GameDescription& game,
                                                                   std::string_view gameSubject,
                                                                   std::span<const ComposedMod> mods,
                                                                   std::span<const std::string> gameHolds);

} // namespace rawframe::world_kest
