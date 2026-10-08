#pragma once

// An asset imported into a session's game (SPEC-0040's import, D503): a
// file from outside copied under the game's directory with what it names
// beside it (a glTF's buffers and images), a sidecar giving it a new
// resource identity and the importer its kind cooks with, and a line in
// the game's description naming it by a new asset identity, so the game
// declares it and the next cook makes it.

#include "rawframe/content/sidecar.h"
#include "rawframe/document/json.h"
#include "rawframe/result/result.h"

#include <cstdint>
#include <filesystem>
#include <string>
#include <string_view>

namespace rawframe::authoring_session {

/// What an import made.
struct Imported {
    /// The game description's keyword for it: texture, mesh, sound, or font.
    std::string kind;
    /// The asset identity its line names it by.
    std::uint64_t id = 0;
    /// The resource identity its sidecar gives it.
    content::ResourceId resource;
    /// Its path under the game's directory, as its line names it.
    std::string path;
    /// Whether its subassets were mapped by the cook tool (D316), for a
    /// source that holds any.
    bool mapped = false;
};

/// The answer an import gives: `authoring.imported` with what it made.
[[nodiscard]] document::Value importedOf(const Imported& made);

/// The kind and importer a source's file name says it is, by its
/// extension; false for one no importer takes from outside.
[[nodiscard]] bool kindOf(const std::filesystem::path& file, std::string& kind, std::string& importer);

/// Imports `source` into the game `game` describes, at `as` under its
/// directory. Refuses a path that leaves the directory or is taken, a kind
/// no importer takes, and a glTF naming files outside its own directory;
/// copies nothing over anything. A mesh's subassets are mapped by `cook`,
/// if it names the cook tool.
[[nodiscard]] result::Result<Imported> importAsset(const std::filesystem::path& game,
                                                   const std::filesystem::path& source,
                                                   std::string_view as,
                                                   const std::filesystem::path& cook);

} // namespace rawframe::authoring_session
