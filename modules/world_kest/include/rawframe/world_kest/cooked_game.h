#pragma once

// A game description cooked (D88): its text, the documents it names beside
// it (actions, mixer, sounds), and each Kest program it names as a file of
// the game's Kest sources resource (D87), all in one resource. A process
// reads the game from it and opens no path: every name the text uses is
// answered from the record.
//
// The record is canonical:
//
//   {"files": [{"path", "text"}], "formatVersion": 1, "kind": "game.description",
//    "programs": [{"entry", "path", "sources"}], "text"}
//
// `path` is as the description writes it; `sources` is the Kest sources
// resource as 32 hex digits and `entry` the program's path among its files.
// Files and programs are in path order, each path once.

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
/// The most files, and the most programs, one names.
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

struct CookedGame {
    std::string text;
    std::vector<CookedGameFile> files;
    std::vector<CookedGameProgram> programs;

    /// The document the description names `path`, or none.
    [[nodiscard]] const CookedGameFile* file(std::string_view path) const noexcept;
    /// The program it names `path`, or none.
    [[nodiscard]] const CookedGameProgram* program(std::string_view path) const noexcept;
};

/// The record's bytes, files and programs put in path order; refused
/// (`cooked_game_invalid`) for a path empty or named twice, a program with
/// no entry or no sources, or more than kMaximumCookedGameNames of either.
[[nodiscard]] result::Result<std::string> writeCookedGame(const CookedGame& game);
/// Refuses (`cooked_game_invalid`) anything `writeCookedGame` would not have
/// written byte for byte.
[[nodiscard]] result::Result<CookedGame> readCookedGame(std::string_view bytes);

} // namespace rawframe::world_kest
