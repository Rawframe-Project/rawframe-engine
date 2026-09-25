#pragma once

// A game as a process reads it (D89): its description, the documents the
// description names, and its Kest files, from which each program it names
// compiles with the engine's own library (D86). One owner reads them, once
// for the Runtime, and every part of the process that plays, hears, or
// controls the game takes them from it rather than opening paths itself.

#include "rawframe/base/sha256.h"
#include "rawframe/composition/participant.h"
#include "rawframe/kest/program.h"
#include "rawframe/result/result.h"
#include "rawframe/world_kest/game.h"

#include <filesystem>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace rawframe::world_kest {

class GameFiles {
public:
    /// No game: `named()` is false and nothing else is asked.
    GameFiles() = default;

    /// The game whose description is at `path`, in development: the
    /// documents it names read beside it, and its Kest files every `.kest`
    /// file under the description's directory. Programs compile from the
    /// files as they are when asked, so a changed file is seen.
    [[nodiscard]] static result::Result<GameFiles> fromDirectory(const std::filesystem::path& path);

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
    /// Compiles the program the description names `name`.
    [[nodiscard]] result::Result<std::shared_ptr<const kest::Program>>
    compile(std::string_view name, const kest::CompileSettings& settings = {}, std::string* report = nullptr) const;
    /// Everything the game is, as one digest: the description, each
    /// document, and each Kest file, as they were read.
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
    };

    bool named_ = false;
    std::string text_;
    GameDescription description_;
    std::vector<Named> documents_;
    std::vector<kest::SourceFile> files_;
    base::Sha256Digest digest_{};
    std::optional<std::filesystem::path> directory_;
};

/// The Runtime's game files: `named()` when the configuration names a game.
inline constexpr composition::Capability<GameFiles> kGameFiles{"rawframe.world_kest.game_files"};

} // namespace rawframe::world_kest
