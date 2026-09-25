#pragma once

// A mod's description cooked (D178): its text, each scene it contributes
// as the resource it is, and the program its handlers are in as an entry of
// its Kest sources resource (D181), in one record, so a process reads the
// mod from its Build and opens no path.
//
//   {"formatVersion": 2, "kind": "mod.description",
//    "programs": [{"entry", "path", "sources"}], "scenes": [{"path", "scene"}], "text"}
//
// Scenes and programs are in path order, each path once, each resource as
// 32 hex digits. A mod has at most one program.

#include "rawframe/base/bits128.h"
#include "rawframe/result/result.h"
#include "rawframe/world_kest/cooked_game.h"

#include <string>
#include <string_view>
#include <vector>

namespace rawframe::world_kest {

/// The resource type of a cooked mod description, and its one
/// representation. A mod's Build holds exactly one.
inline constexpr base::Bits128 kCookedModType = base::parseBits128Hex("09ba8bacf0566f3e862f2b2cf7c513ce").value;
inline constexpr std::string_view kCookedModRepresentation = "rawframe.mod.description";

struct CookedMod {
    std::string text;
    std::vector<CookedGameScene> scenes;
    std::vector<CookedGameProgram> programs;

    /// The scene it contributes by `path`, or none.
    [[nodiscard]] const CookedGameScene* scene(std::string_view path) const noexcept;
};

/// The record's bytes, scenes in path order; refused (`cooked_game_invalid`)
/// for a path empty or named twice, a resource of no identity, more than
/// kMaximumCookedGameNames scenes, or more than one program.
[[nodiscard]] result::Result<std::string> writeCookedMod(const CookedMod& mod);

/// Reads a record as written, and nothing else.
[[nodiscard]] result::Result<CookedMod> readCookedMod(std::string_view bytes);

} // namespace rawframe::world_kest
