#pragma once

// The player's settings kept between runs (D626): the values of a game's
// kept UI nodes, by their components' names, in a `ui.kept` document on the
// player's machine, ADR-0057's settings-class preferences outside any save.
//
//   {
//     "kind": "ui.kept",
//     "formatVersion": 1,
//     "values": {
//       "tycoon.top": 1,
//       "tycoon.zoom": 60
//     }
//   }
//
// Written by the engine alone, under SPEC-0028's profile, names in order;
// read leniently as to its whitespace, since a player may edit it.

#include "rawframe/base/platform.h"
#include "rawframe/execution/executor.h"
#include "rawframe/result/result.h"

#include <atomic>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <map>
#include <optional>
#include <string>
#include <string_view>

#if RAWFRAME_FILE_SYSTEM
#include <filesystem>
#endif

namespace rawframe::world_ui {

using KeptValues = std::map<std::string, float, std::less<>>;

/// A `ui.kept` document's most bytes and values.
inline constexpr std::size_t kMostKeptBytes = 65536;
inline constexpr std::size_t kMostKeptValues = 1024;

/// A `ui.kept` document's values: refused (`rawframe.document` errors) for
/// another kind or version, a name not a component's, a value not finite,
/// or more than `kMostKeptValues`.
[[nodiscard]] result::Result<KeptValues> readKept(std::string_view text);

/// `values` as a `ui.kept` document.
[[nodiscard]] std::string writeKept(const KeptValues& values);

#if RAWFRAME_FILE_SYSTEM

/// A small file's whole text, at most `most` bytes: none where it cannot be
/// read whole (D626, D634).
[[nodiscard]] std::optional<std::string> fileText(const std::filesystem::path& path, std::size_t most);

/// The file the player's settings are kept in, written one at a time on the
/// blocking-I/O executor, each whole or not at all.
class Keeper {
public:
    explicit Keeper(std::filesystem::path path) : path_(std::move(path)) {
    }
    Keeper(const Keeper&) = delete;
    Keeper& operator=(const Keeper&) = delete;
    ~Keeper();

    /// What the file holds, nothing where there is none; refused where it
    /// cannot be read or is no `ui.kept` document.
    [[nodiscard]] result::Result<KeptValues> read() const;

    /// Whether a write is under way.
    [[nodiscard]] bool writing() const noexcept {
        return writing_.load(std::memory_order_acquire);
    }

    /// `values` written on `io`, here where none takes it; nothing while a
    /// write is under way, which the caller asks again after.
    void keep(const KeptValues& values, execution::Executor* io, execution::OwnerId owner) noexcept;

    /// The write under way finished, helping `io` run it.
    void finish(execution::Executor* io) noexcept;

    /// Writes done, and failed.
    [[nodiscard]] std::uint64_t written() const noexcept {
        return written_.load(std::memory_order_acquire);
    }
    [[nodiscard]] std::uint64_t failed() const noexcept {
        return failed_.load(std::memory_order_acquire);
    }

private:
    void write(const std::string& text) noexcept;

    std::filesystem::path path_;
    std::atomic<bool> writing_{false};
    std::atomic<std::uint64_t> written_{0};
    std::atomic<std::uint64_t> failed_{0};
};

#endif

} // namespace rawframe::world_ui
