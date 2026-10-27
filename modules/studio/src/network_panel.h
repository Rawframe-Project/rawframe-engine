#pragma once

// Studio's Network panel: the link the next play's clients run
// over, and the played game's server's traffic, read once a second and
// shown as rates, the whole and each connection's.

#include "rawframe/ui/tree.h"

#include <array>
#include <cstdint>
#include <optional>
#include <string_view>
#include <utility>
#include <vector>

namespace rawframe::studio {

/// A link the clients a play starts run over: the conditions QUIC gives
/// them, by its settings.
struct LinkChoice {
    std::string_view name;
    std::uint32_t latencyMs = 0;
    std::uint32_t jitterMs = 0;
    std::uint32_t lossPpm = 0;
};

inline constexpr std::array<LinkChoice, 3> kLinks = {
    {{.name = "No conditions"},
     {.name = "Slow link", .latencyMs = 120, .jitterMs = 20},
     {.name = "Lossy link", .latencyMs = 150, .jitterMs = 30, .lossPpm = 50'000}}};

/// The server's totals when last read, to make rates of.
struct TrafficRead {
    double at = 0;
    std::int64_t stateBytes = 0;
    std::int64_t stateDatagrams = 0;
    std::int64_t inputWindows = 0;
    /// Each connection's state bytes, by its number.
    std::vector<std::pair<std::int64_t, std::int64_t>> connectionBytes;
};

struct NetworkShown {
    /// The link chosen, an index into `kLinks`.
    std::size_t link = 0;
    std::vector<std::pair<ui::Node, std::size_t>> linkButtons;
    /// The panel's lines under its buttons.
    std::vector<ui::Node> lines;
    double nextRead = 0;
    std::optional<TrafficRead> last;
    /// Times the server's traffic was read and shown.
    std::uint64_t reads = 0;
    /// Connections the server held when last read, and the most any read
    /// saw (the last may come as the game stops).
    std::int64_t connections = 0;
    std::int64_t mostConnections = 0;
    /// The most connections a read showed a line of.
    std::size_t mostConnectionLines = 0;
};

} // namespace rawframe::studio
