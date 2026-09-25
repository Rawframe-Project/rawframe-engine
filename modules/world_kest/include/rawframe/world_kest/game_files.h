#pragma once

// A game as a process reads it (D89): its description, the documents the
// description names, and its Kest files, from which each program it names
// compiles with the engine's own library (D86). One owner reads them, once
// for the Runtime, and every part of the process that plays, hears, or
// controls the game takes them from it rather than opening paths itself.
// They come from a directory in development, or from the Runtime's content
// as the cook made them (D88, D90), and are the same game either way.

#include "rawframe/animation/resources.h"
#include "rawframe/base/bits128.h"
#include "rawframe/base/sha256.h"
#include "rawframe/composition/participant.h"
#include "rawframe/content/identity.h"
#include "rawframe/game_content/game_content.h"
#include "rawframe/kest/program.h"
#include "rawframe/physics3d/physics.h"
#include "rawframe/result/result.h"
#include "rawframe/world_kest/game.h"

#include <filesystem>
#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace rawframe::world_kest {

/// A string table or translation a `text` line names, by the resource it
/// is (D147).
struct GameText {
    std::string path;
    base::Bits128 document{};
};

class GameFiles {
public:
    /// No game: `named()` is false and nothing else is asked.
    GameFiles() = default;

    /// The game whose description is at `path`, in development: the
    /// documents and scenes it names read beside it, a scene an instance
    /// names found by the sidecar under the description's directory that
    /// names it, and its Kest files every `.kest` file under that
    /// directory. Programs compile from the
    /// files as they are when asked, so a changed file is seen. Each mesh
    /// is the resource its sidecar names, read from `content` (D112): the
    /// runtime decodes no source format, so a game with meshes needs its
    /// sources cooked. Each animator's graph is read beside it too, and the
    /// clips and masks it names and their skeletons by the sidecars under
    /// the description's directory that name them.
    [[nodiscard]] static result::Result<GameFiles> fromDirectory(const std::filesystem::path& path,
                                                                 game_content::GameContent* content = nullptr);
    /// The game whose cooked description is `description` in `content`
    /// (D88): its documents from the description's record, each scene from
    /// the scene resource it names (D95), each mesh from the mesh resource
    /// it names (D112), each animator's graph from the graph resource it
    /// names, the clips, masks, and skeletons below it by their
    /// identities, and each program's files from the Kest sources resource
    /// it names (D87).
    /// Admits their representations, and waits for each read. Refused when
    /// a read fails, a record does not read, or the description uses a
    /// name the record does not answer.
    [[nodiscard]] static result::Result<GameFiles> fromContent(game_content::GameContent& content,
                                                               content::ResourceId description);

    [[nodiscard]] bool named() const noexcept {
        return named_;
    }
    [[nodiscard]] const std::string& text() const noexcept {
        return text_;
    }
    [[nodiscard]] const GameDescription& description() const noexcept {
        return description_;
    }
    /// The document the description names `name`; refused (`unreadable_file`)
    /// for a name it does not use.
    [[nodiscard]] result::Result<std::string_view> document(std::string_view name) const;
    /// The scene the description names `name` (a `scene` line), its text;
    /// refused (`unreadable_file`) for a name it does not use.
    [[nodiscard]] result::Result<std::string_view> scene(std::string_view name) const;
    /// That scene's resource identity: the resource read from content, or
    /// in development the identity its sidecar gives, if it has one.
    [[nodiscard]] std::optional<base::Bits128> sceneIdentity(std::string_view name) const;
    /// The scene of resource identity `scene` that an instance names (D96),
    /// its text: one of the scenes the game's scenes instance, however far
    /// down, all read when the files were. Refused (`unreadable_file`) for
    /// any other.
    [[nodiscard]] result::Result<std::string_view> sceneById(base::Bits128 scene) const;
    /// Every text document the description names, by its resource, in the
    /// order of its lines: in development the identity its sidecar gives,
    /// which must name `rawframe.text`, and from content the one the record
    /// names. Only the identities are read here; a client reads the
    /// resources, and a dedicated server never does.
    [[nodiscard]] const std::vector<GameText>& texts() const noexcept {
        return texts_;
    }
    /// Every mesh the description names, decoded, by the identity its line
    /// gives it, in the order of its lines.
    [[nodiscard]] const std::vector<physics3d::BodyMesh>& meshes() const noexcept {
        return meshes_;
    }
    /// The graph of the animator whose line names `path`, its text;
    /// refused (`unreadable_file`) for a path no animator line names.
    [[nodiscard]] result::Result<std::string_view> animatorGraph(std::string_view path) const;
    /// The clip, mask, or skeleton of resource identity `id` that the
    /// game's graphs name, or their clips do, its text; refused
    /// (`unreadable_file`) for any other.
    [[nodiscard]] result::Result<std::string_view> animationDocument(base::Bits128 id) const;
    /// Compiles the program the description names `name`.
    [[nodiscard]] result::Result<std::shared_ptr<const kest::Program>>
    compile(std::string_view name, const kest::CompileSettings& settings = {}, std::string* report = nullptr) const;
    /// Everything the game is, as one digest: the description, each
    /// document, each scene, each scene instanced, each mesh, each
    /// animation document, each text document's identity, and each Kest
    /// file, as they were read.
    [[nodiscard]] const base::Sha256Digest& digest() const noexcept {
        return digest_;
    }
    /// The directory the game is read from in development, whose files a
    /// reload watches; none for a game from content.
    [[nodiscard]] const std::optional<std::filesystem::path>& directory() const noexcept {
        return directory_;
    }

private:
    struct Named {
        std::string name;
        std::string text;
        /// A scene's resource identity, when known.
        std::optional<base::Bits128> identity;
    };
    /// A program the description names: its entry among the files of one
    /// of `sources_`.
    struct Program {
        std::string name;
        std::string entry;
        std::size_t sources = 0;
    };

    /// Reads every scene the game's scenes instance, however far down,
    /// with `read`.
    result::Status readInstanced(const std::function<result::Result<std::string>(base::Bits128)>& read);
    /// Reads and decodes the description's meshes, the resources
    /// `resources` names in the order of its lines, from `content`.
    result::Status readMeshes(game_content::GameContent* content, const std::vector<base::Bits128>& resources);
    /// Reads every clip and mask the animators' graphs name, every skeleton
    /// those clips name, and each animator's subset mask, each once, with
    /// `read`, which is asked for a document of a kind by its identity.
    result::Status
    readAnimations(const std::function<result::Result<std::string>(base::Bits128, animation::DocumentKind)>& read);
    /// The digest of what has been read, set last.
    void seal();

    bool named_ = false;
    std::string text_;
    GameDescription description_;
    std::vector<Named> documents_;
    std::vector<Named> scenes_;
    /// Every scene an instance names, by identity, in identity order.
    std::vector<std::pair<base::Bits128, std::string>> instanced_;
    std::vector<Program> programs_;
    std::vector<physics3d::BodyMesh> meshes_;
    std::vector<GameText> texts_;
    /// The digest of each mesh's cooked bytes, in the same order.
    std::vector<base::Sha256Digest> meshDigests_;
    /// Each animator's graph, in the order of its lines.
    std::vector<Named> graphs_;
    /// Every clip, mask, and skeleton below them, by identity, in identity
    /// order.
    std::vector<std::pair<base::Bits128, std::string>> animations_;
    /// Each set of Kest files a program compiles from: one read from a
    /// directory, or one for each Kest sources resource named.
    std::vector<std::vector<kest::SourceFile>> sources_;
    base::Sha256Digest digest_{};
    std::optional<std::filesystem::path> directory_;
};

/// The Runtime's game files: `named()` when the configuration names a game.
inline constexpr composition::Capability<GameFiles> kGameFiles{"rawframe.world_kest.game_files"};

} // namespace rawframe::world_kest
