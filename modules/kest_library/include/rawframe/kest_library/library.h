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

#include "rawframe/base/bits128.h"
#include "rawframe/kest/program.h"
#include "rawframe/result/result.h"

#include <cstddef>
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

/// The resource type of a game's Kest files, all of them as one resource
/// (D87), and its one representation: a canonical record
/// `{kind: "kest.sources", formatVersion: 1, files: [{path, text}]}`, files
/// in path order, each path plain as `compile` requires.
inline constexpr base::Bits128 kGameSourcesType = base::parseBits128Hex("49be428d0ae12ec9b0f817b7c1e0e956").value;
inline constexpr std::string_view kGameSourcesRepresentation = "rawframe.kest.sources";
/// The most files a game's sources hold.
inline constexpr std::size_t kMaximumGameFiles = 4096;

/// A game's files as the resource holds them; refused (`SourcesInvalid`)
/// when they are more than kMaximumGameFiles, a path is not plain, or two
/// share one.
[[nodiscard]] result::Result<std::string> writeGameSources(std::span<const kest::SourceFile> files);
/// The files a resource holds, in path order; refused (`SourcesInvalid`) for
/// anything `writeGameSources` would not have written byte for byte.
[[nodiscard]] result::Result<std::vector<kest::SourceFile>> readGameSources(std::string_view bytes);

/// Whether `path` names one place under a game and only that one: relative,
/// in normal form, never climbing out, and not where the project stands.
[[nodiscard]] bool plainGamePath(std::string_view path);

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
