#pragma once

// Studio extensions (SPEC-0051 Part 7, ADR-0032 section 6, D874): Kest
// programs under a game's `extensions/` directory, each compiled against
// the Kest library and run as untrusted code with `rawframe.studio`'s doors
// and no others, under a heap and fuel ceiling, so opening a game never
// runs its code with the tool's authority. An extension names its commands
// in `extend()`; a command's run stages operations, which become one atomic
// request the session applies as any other. What Studio offers of them,
// any session offers headless (`authoring.extensions`,
// `authoring.extension`).

#include "rawframe/authoring/operations.h"
#include "rawframe/authoring/request.h"
#include "rawframe/base/bits128.h"
#include "rawframe/result/result.h"
#include "rawframe/scene/scene.h"

#include <cstddef>
#include <filesystem>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace rawframe::authoring_session {

/// The most extensions a game's directory holds that are read, the most
/// commands one offers, and the most operations one run stages.
inline constexpr std::size_t kMostExtensions = 32;
inline constexpr std::size_t kMostCommands = 32;
inline constexpr std::size_t kMostStaged = 4096;
/// The most rows one section shows.
inline constexpr std::size_t kMostRows = 64;

struct ExtensionCommand {
    std::string name;
    std::string function;
};

/// An Inspector section an extension offers (D875): the component it is
/// shown on, by the name the scene records it by, and its function.
struct ExtensionSection {
    std::string component;
    std::string function;
};

/// A row a section shows.
struct SectionRow {
    std::string label;
    double value = 0;
};

/// One extension: its path under the game's directory, the commands it
/// offers, the capabilities its doors are of, and, where it offers
/// nothing, why (it did not compile, start, or extend).
struct GameExtension {
    std::string path;
    std::vector<ExtensionCommand> commands;
    std::vector<ExtensionSection> sections;
    std::vector<std::string> capabilities;
    std::string refused;
};

/// Every `.kest` file directly under `<game directory>/extensions/`, in
/// the order of their names, each with the others there to import.
[[nodiscard]] std::vector<GameExtension> readExtensions(const std::filesystem::path& gameDirectory);

/// The atomic request the command `command` of the extension at `path`
/// stages, run on `chosen` (entities of `scene`), its components named as
/// `catalog` names them. Refuses (`TargetNotFound`) an extension or
/// command there is not, and (`ValidationFailed`) a run the machine
/// refused, with its report, or one that staged nothing.
[[nodiscard]] result::Result<authoring::Request> runExtension(const std::filesystem::path& gameDirectory,
                                                              std::string_view path,
                                                              std::string_view command,
                                                              const scene::Scene& scene,
                                                              std::span<const base::Bits128> chosen,
                                                              const authoring::ComponentCatalog& catalog);

/// The rows the extension at `path` shows in its section of `component`
/// for `entity` of `scene`: the section's function run with the entity as
/// the one chosen, staging nothing. Refuses as `runExtension` does, and
/// (`TargetNotFound`) a section the extension does not offer.
[[nodiscard]] result::Result<std::vector<SectionRow>> runSection(const std::filesystem::path& gameDirectory,
                                                                 std::string_view path,
                                                                 std::string_view component,
                                                                 const scene::Scene& scene,
                                                                 base::Bits128 entity,
                                                                 const authoring::ComponentCatalog& catalog);

} // namespace rawframe::authoring_session
