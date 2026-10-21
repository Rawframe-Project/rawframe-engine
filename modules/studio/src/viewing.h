#pragma once

// What Studio tells a scene's preview to show (D791 to D798), kept apart
// from the shell, as the preview is told it again whenever one is attached;
// and the game played in the viewport (D789, D790).

#include "rawframe/render/capture.h"
#include "rawframe/texture/texture.h"
#include "rawframe/world_tooling/forwarding.h"

#include <array>
#include <cstdint>
#include <filesystem>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace rawframe::studio {

/// Whether `menu` is one of the viewport's pills, and whether the pill
/// lists `command` (D916).
[[nodiscard]] bool isPill(std::string_view menu);
[[nodiscard]] bool pillLists(std::string_view pill, std::string_view command);

struct PreviewShown {
    /// The layers the preview leaves out (D791), and how many times a
    /// preview was told.
    std::vector<std::uint32_t> hiddenLayers;
    std::uint64_t layersHidden = 0;
    /// How the preview draws, by the renderer's name (D794), and how many
    /// times a preview took it.
    std::string viewMode = "lit";
    std::uint64_t modesShown = 0;
    /// Bookmarks kept and gone to (D796).
    std::uint64_t bookmarksKept = 0;
    std::uint64_t bookmarksVisited = 0;
    /// How many views the preview shows, and how many times a preview
    /// split so (D797).
    std::uint8_t views = 1;
    std::uint64_t splitsShown = 0;
    /// The overlays the preview draws, by name, and how many times a
    /// preview took them (D798).
    std::vector<std::string> overlays;
    std::uint64_t overlaysShown = 0;
};

/// The game played in the viewport (D789, D790): whether it plays there,
/// the picture shown, the stream it comes through, and how many were shown; and what lands on the viewport handed to
/// it, how many events, and how many records the preview took.
struct PlayedViewport {
    bool inViewport = false;
    std::shared_ptr<const texture::Texture> picture;
    /// The stream its pictures come through, and the file it reads (D967).
    std::optional<render::PictureStreamReader> stream;
    std::filesystem::path streamed;
    std::uint64_t picturesShown = 0;
    /// The time its pictures took to read, in all and at the most, in
    /// microseconds (D964): what holds a moving view back.
    std::uint64_t readMicroseconds = 0;
    std::uint64_t slowestReadMicroseconds = 0;
    world_tooling::Forwarding* forwarding = nullptr;
    std::uint64_t inputsForwarded = 0;
    /// Where a right press went down over it, until it moves away or is
    /// let go (D973).
    std::optional<std::array<float, 2>> secondaryAt;
    std::uint64_t inputsTaken = 0;
    /// Whether the played game's World is paused, the tick its server last
    /// said, and how many times it was paused and stepped (D806).
    bool paused = false;
    std::int64_t tick = 0;
    std::uint64_t pauses = 0;
    std::uint64_t steps = 0;
    /// The tick it was paused at, and how many ran while it was, once let
    /// run: those stepped, and no more.
    std::int64_t pausedAt = 0;
    std::int64_t ranPaused = 0;
    /// The other players' clients the last play started (D804).
    std::uint64_t othersStarted = 0;
};

} // namespace rawframe::studio
