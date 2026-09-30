#pragma once

// A game description cooked (D88, D95): its text, the documents it names
// beside it (actions, mixer, sounds), each Kest program it names as a file
// of the game's Kest sources resource (D87), and each scene, mesh,
// animator's graph, text document (D147), texture (D254), and material
// (D303) it names as a resource, all in one record. A process
// reads the game from it and opens no path: every name the text uses is
// answered from the record.
//
// The record is canonical:
//
//   {"animators": [{"graph", "path"}], "files": [{"path", "text"}],
//    "formatVersion": 7, "kind": "game.description", "materials": [{"material", "path"}],
//    "meshes": [{"mesh", "path"}],
//    "programs": [{"entry", "path", "sources"}], "scenes": [{"path", "scene"}], "text",
//    "texts": [{"document", "path"}], "textures": [{"path", "texture"}]}
//
// `path` is as the description writes it; `sources` is the Kest sources
// resource, `scene` the scene resource, `mesh` the mesh resource, and
// `graph` the animation graph resource, `document` the string table or
// translation resource, `texture` the texture resource, `material` the
// material resource, as 32 hex digits, and `entry` the program's path among
// its files. Animators, files, materials, meshes, programs, scenes, texts,
// and textures are each in path order, each path once.

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
/// The most animators, files, meshes, programs, scenes, texts, or textures
/// one names.
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

/// A mesh the description names, by the resource it is (D112).
struct CookedGameMesh {
    std::string path;
    base::Bits128 mesh{};
};

/// A material the description names (D303), by the resource it is.
struct CookedGameMaterial {
    std::string path;
    base::Bits128 material{};
};

/// An animator's graph, by the resource it is.
struct CookedGameAnimator {
    std::string path;
    base::Bits128 graph{};
};

/// A string table or translation (SPEC-0033), by the resource it is.
struct CookedGameText {
    std::string path;
    base::Bits128 document{};
};

/// A texture the description names (D254), by the resource it is.
struct CookedGameTexture {
    std::string path;
    base::Bits128 texture{};
};

struct CookedGame {
    std::string text;
    std::vector<CookedGameFile> files;
    std::vector<CookedGameProgram> programs;
    std::vector<CookedGameScene> scenes;
    std::vector<CookedGameMesh> meshes;
    std::vector<CookedGameAnimator> animators;
    std::vector<CookedGameText> texts;
    std::vector<CookedGameTexture> textures;
    std::vector<CookedGameMaterial> materials;

    /// The document the description names `path`, or none.
    [[nodiscard]] const CookedGameFile* file(std::string_view path) const noexcept;
    /// The program it names `path`, or none.
    [[nodiscard]] const CookedGameProgram* program(std::string_view path) const noexcept;
    /// The scene it names `path`, or none.
    [[nodiscard]] const CookedGameScene* scene(std::string_view path) const noexcept;
    /// The mesh it names `path`, or none.
    [[nodiscard]] const CookedGameMesh* mesh(std::string_view path) const noexcept;
    /// The text document it names `path`, or none.
    [[nodiscard]] const CookedGameText* textDocument(std::string_view path) const noexcept;
    /// The material it names `path`, or none.
    [[nodiscard]] const CookedGameMaterial* material(std::string_view path) const noexcept;
    /// The texture it names `path`, or none.
    [[nodiscard]] const CookedGameTexture* texture(std::string_view path) const noexcept;
    /// The animator graph it names `path`, or none.
    [[nodiscard]] const CookedGameAnimator* animator(std::string_view path) const noexcept;
};

/// The record's bytes, its lists each put in path order; refused
/// (`cooked_game_invalid`) for a path empty or named twice, a program with
/// no entry or no sources, a scene, mesh, graph, text, or texture of no resource, or more
/// than kMaximumCookedGameNames of any.
[[nodiscard]] result::Result<std::string> writeCookedGame(const CookedGame& game);
/// Refuses (`cooked_game_invalid`) anything `writeCookedGame` would not have
/// written byte for byte.
[[nodiscard]] result::Result<CookedGame> readCookedGame(std::string_view bytes);

} // namespace rawframe::world_kest
