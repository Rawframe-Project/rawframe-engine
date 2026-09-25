#pragma once

// The Kest library every game compiles against (ADR-0084, D86): Kest's `std`
// at the vendored revision, and the engine's own Kest modules
// (`rawframe.world`, `rawframe.physics2d`, `rawframe.input`,
// `rawframe.sound`, ...), whose doors other modules implement. It is part of
// the engine and embedded in it when the engine is built, so a game's program
// is its own files and nothing else, and it compiles against exactly the
// library the running engine binds (ADR-0014: compiled under the pinned
// toolchain, never loaded from a path).
//
// A game's files are handed under `game/`, beside a project whose sources are
// the game's directory and the engine's modules; imports resolve as they do
// on disk from a project that says `source .` and names the engine's
// `modules/kest_library/kest`.

#include "rawframe/kest/program.h"
#include "rawframe/result/result.h"

#include <memory>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace rawframe::kest_library {

/// Where a compile is handed `std`, for CompileSettings::library.
inline constexpr std::string_view kStandardLibrary = "engine/lib/";
/// Where it is handed the engine's modules.
inline constexpr std::string_view kEngineModules = "engine/modules/";
/// Where it is handed a game's files.
inline constexpr std::string_view kGame = "game/";

/// Every file of the library, at the path a compile is handed it.
[[nodiscard]] std::vector<kest::SourceFile> files();

/// Compiles the program at `entry` among `game`, the game's own files, with
/// paths relative to the game's directory; the library is added. Refused
/// (`does_not_compile`) when `entry` is not among them, a path is not a
/// plain relative one, or a game file would stand where the project does.
/// `settings.library` is the library's own. On failure `report`, if given,
/// receives the compiler's text.
[[nodiscard]] result::Result<std::shared_ptr<const kest::Program>> compile(std::string_view entry,
                                                                           std::span<const kest::SourceFile> game,
                                                                           const kest::CompileSettings& settings,
                                                                           std::string* report = nullptr);

} // namespace rawframe::kest_library
