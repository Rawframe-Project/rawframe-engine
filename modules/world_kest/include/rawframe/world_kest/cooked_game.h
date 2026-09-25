#pragma once

// A game description cooked (D88, D95): its text, the documents it names
// beside it (actions, mixer, sounds), each Kest program it names as a file
// of the game's Kest sources resource (D87), and each scene it names as a
// scene resource, all in one record. A process reads the game from it and
// opens no path: every name the text uses is answered from the record.
//
// The record is canonical:
//
//   {"files": [{"path", "text"}], "formatVersion": 2, "kind": "game.description",
//    "programs": [{"entry", "path", "sources"}], "scenes": [{"path", "scene"}], "text"}
//
// `path` is as the description writes it; `sources` is the Kest sources
// resource and `scene` the scene resource, as 32 hex digits, and `entry`
// the program's path among its files. Files, programs, and scenes are each
// in path order, each path once.

#include "rawframe/base/bits128.h"
#include "rawframe/result/result.h"

#include <cstddef>
#include <string>
#include <string_view>
#include <vector>

namespace rawframe::world_kest {

/// The resource type of a cooked game description, and its one
/// representation.
inline constexpr base::Bits128 kCookedGameType = base::parseBits128Hex("94011cff710065644e0f466b35941608").value;
inline constexpr std::string_view kCookedGameRepresentation = "rawframe.game.description";
/// The most files, programs, or scenes one names.
inline constexpr std::size_t kMaximumCookedGameNames = 1024;

/// A document the description names, by the name it uses.
struct CookedGameFile {
    std::string path;
    std::string text;
};

/// A program the description names: which Kest sources hold it, and where
/// among them.
struct CookedGameProgram {
    std::string path;
    base::Bits128 sources{};
    std::string entry;
};

/// A scene the description names, by the resource it is.
struct CookedGameScene {
    std::string path;
    base::Bits128 scene{};
};

struct CookedGame {
    std::string text;
    std::vector<CookedGameFile> files;
    std::vector<CookedGameProgram> programs;
    std::vector<CookedGameScene> scenes;

    /// The document the description names `path`, or none.
    [[nodiscard]] const CookedGameFile* file(std::string_view path) const noexcept;
    /// The program it names `path`, or none.
    [[nodiscard]] const CookedGameProgram* program(std::string_view path) const noexcept;
    /// The scene it names `path`, or none.
    [[nodiscard]] const CookedGameScene* scene(std::string_view path) const noexcept;
};

/// The record's bytes, files, programs, and scenes put in path order;
/// refused (`cooked_game_invalid`) for a path empty or named twice, a
/// program with no entry or no sources, a scene of no resource, or more
/// than kMaximumCookedGameNames of any.
[[nodiscard]] result::Result<std::string> writeCookedGame(const CookedGame& game);
/// Refuses (`cooked_game_invalid`) anything `writeCookedGame` would not have
/// written byte for byte.
[[nodiscard]] result::Result<CookedGame> readCookedGame(std::string_view bytes);

} // namespace rawframe::world_kest
