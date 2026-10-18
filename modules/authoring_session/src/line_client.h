#pragma once

// One QUIC connection carrying records a line each, on one stream the
// client opens (D408, D881): the client side shared by a link to a
// running Runtime's tooling endpoint and one to a shared session's host.

#include "rawframe/network/provider.h"

#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace rawframe::authoring_session {

/// The first line of a regular file of at most 4 KiB; none for anything
/// else, so a device or a large file is never read.
[[nodiscard]] std::optional<std::string> firstLine(const char* path);

class LineClient {
public:
    explicit LineClient(network::Provider& provider) : provider_(provider) {
    }

    /// Waits until the connection is ready to carry records.
    [[nodiscard]] bool connected(std::string_view endpoint);

    /// Sends one record and waits for its reply line; none when the
    /// endpoint closed or kept silent. A line that is no reply, which the
    /// endpoint said unasked meanwhile, is kept for `heard`.
    [[nodiscard]] std::optional<std::string> ask(std::string_view record);

    /// What the endpoint said unasked, waiting up to `withinMilliseconds`
    /// for the first where none was kept.
    [[nodiscard]] std::vector<std::string> heard(std::int64_t withinMilliseconds);

    [[nodiscard]] bool closed() const noexcept {
        return closed_;
    }

    void close() noexcept {
        provider_.close(connection_);
    }

private:
    void pump();
    /// The whole lines received, each taken: a reply returned where it is
    /// the one numbered `id`, every other line that is no reply kept.
    std::optional<std::string> take(std::optional<std::int64_t> id);

    network::Provider& provider_;
    network::ConnectionId connection_;
    network::StreamId stream_;
    std::vector<network::Event> events_;
    std::string received_;
    std::vector<std::string> unasked_;
    bool ready_ = false;
    bool closed_ = false;
    /// The questions asked, each one's number.
    std::int64_t asked_ = 0;
};

} // namespace rawframe::authoring_session
