#pragma once

// One QUIC connection carrying records a line each, on one stream the
// client opens: the client side shared by a link to a
// running Runtime's tooling endpoint and one to a shared session's host.

#include "rawframe/document/json.h"
#include "rawframe/network/provider.h"

#include <chrono>
#include <cstdint>
#include <map>
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
    /// The same, waiting at most `within` for the reply: a last record to an
    /// endpoint that may have gone.
    [[nodiscard]] std::optional<std::string> ask(std::string_view record, std::chrono::milliseconds within);

    /// Sends one last record and waits, at most `within`, only until the
    /// endpoint has received it, not for its reply: whether it was
    /// received. Its connection acknowledges what arrives whatever its
    /// program is doing, so a busy program holds no one up. The
    /// connection must not buffer sends (`QuicSettings::sendBuffering`),
    /// or a record counts as received once handed over.
    [[nodiscard]] bool tell(std::string_view record, std::chrono::milliseconds within);

    /// Sends one record answered later, its reply and what is said of it
    /// meanwhile (a cook's progress) kept for `heard` under the record's own
    /// id. Whether it was sent.
    [[nodiscard]] bool send(std::string_view record);
    /// The number a record sent with `send` went out under, by the id it
    /// was given; none for one not waiting for its reply.
    [[nodiscard]] std::optional<std::int64_t> sentAs(const document::Value& id) const;

    /// What the endpoint said unasked, waiting up to `withinMilliseconds`
    /// for the first where none was kept.
    [[nodiscard]] std::vector<std::string> heard(std::int64_t withinMilliseconds);

    [[nodiscard]] bool closed() const noexcept {
        return closed_;
    }
    /// Whether the last question waited out its bound unanswered: an
    /// endpoint stopped by its system, which a last record need not wait
    /// for again.
    [[nodiscard]] bool silent() const noexcept {
        return silent_;
    }

    void close() noexcept {
        provider_.close(connection_);
    }

private:
    void pump();
    /// `record` numbered `id` in place of its own, as a line.
    static std::string numbered(const document::Value& record, std::int64_t id);
    /// `line` with `id` its id in place of the number it came under.
    static std::string renumbered(const document::Value& line, const document::Value& id);
    /// The whole lines received, each taken: a reply returned where it is
    /// the one numbered `id`, every other line that is no reply kept.
    std::optional<std::string> take(std::optional<std::int64_t> id);

    network::Provider& provider_;
    network::ConnectionId connection_;
    network::StreamId stream_;
    std::vector<network::Event> events_;
    std::string received_;
    std::vector<std::string> unasked_;
    /// Records sent with `send` whose reply has not come, by the number
    /// each went out under: the id each was given.
    std::map<std::int64_t, document::Value> waiting_;
    bool ready_ = false;
    bool closed_ = false;
    bool silent_ = false;
    /// The questions asked, each one's number.
    std::int64_t asked_ = 0;
};

} // namespace rawframe::authoring_session
