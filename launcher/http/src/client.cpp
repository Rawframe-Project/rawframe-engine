#include "rawframe/http/client.h"

#include "connection.h"
#include "rawframe/http/errors.h"
#include "rawframe/http/response.h"

#include <array>

namespace rawframe::http {

namespace {

bool redirect(int status) noexcept {
    return status == 301 || status == 302 || status == 303 || status == 307 || status == 308;
}

std::unexpected<result::Error> statusRefused(std::string_view why, const Url& url, int status) {
    return std::unexpected<result::Error>{
        result::fail(result::ErrorClass::Unavailable, kHttpDomain, code(HttpError::Status), why)
            .error()
            .withContext("url", url.text())
            .withContext("status", std::to_string(status))};
}

} // namespace

struct Client::State {
    // Sockets are started before any connection and ended after the last.
    Sockets sockets;
    ClientSettings settings;
    std::unique_ptr<Tls> tls;
    std::unique_ptr<Connection> connection;
    ClientStatistics statistics;
    std::array<std::byte, 64 * 1024> room{};

    /// One request and its response over the kept connection, or a new one.
    result::Result<Response> exchange(const Url& url, std::uint64_t ceiling) {
        const std::string kRequest = "GET " + url.target + " HTTP/1.1\r\nHost: " + url.authority() +
                                     "\r\nUser-Agent: " + settings.agent +
                                     "\r\nAccept-Encoding: identity\r\nConnection: keep-alive\r\n\r\n";
        if (connection != nullptr && !connection->to(url)) {
            connection.reset();
        }
        // A kept connection the server has since closed fails before any of
        // the response comes: then the request goes again over a new one.
        for (int attempt = 0;; ++attempt) {
            const bool kKept = connection != nullptr;
            if (!kKept) {
                RAWFRAME_TRY_ASSIGN(connection, Connection::open(url, *tls, settings.timeoutMilliseconds));
                ++statistics.connections;
            }
            ++statistics.requests;
            ResponseReader reader{ceiling};
            std::uint64_t came = 0;
            result::Status done = connection->write(std::as_bytes(std::span{kRequest}));
            while (done.has_value() && !reader.whole()) {
                auto read = connection->read(room);
                if (!read.has_value()) {
                    done = std::unexpected<result::Error>{std::move(read).error()};
                } else if (*read == 0) {
                    done = reader.ended();
                } else {
                    came += *read;
                    auto taken = reader.read(std::span{room}.first(*read));
                    if (!taken.has_value()) {
                        done = std::unexpected<result::Error>{std::move(taken).error()};
                    }
                }
            }
            statistics.bytes += came;
            if (done.has_value()) {
                Response response = std::move(reader.response());
                if (!response.keepAlive) {
                    connection.reset();
                }
                return response;
            }
            connection.reset();
            if (!kKept || came > 0 || attempt > 0) {
                return std::unexpected<result::Error>{std::move(done).error().withContext("url", url.text())};
            }
        }
    }
};

Client::Client(std::unique_ptr<State> state) noexcept : state_(std::move(state)) {
}

Client::~Client() = default;

result::Result<std::unique_ptr<Client>> Client::create(ClientSettings settings) {
    auto state = std::make_unique<State>();
    RAWFRAME_TRY_ASSIGN(state->tls, Tls::create(settings.authorities));
    state->settings = std::move(settings);
    return std::unique_ptr<Client>{new Client{std::move(state)}};
}

result::Result<std::vector<std::byte>> Client::get(std::string_view text, std::uint64_t ceiling) {
    State& state = *state_;
    RAWFRAME_TRY_ASSIGN(Url url, parseUrl(text));
    for (std::uint32_t hop = 0;; ++hop) {
        RAWFRAME_TRY_ASSIGN(Response response, state.exchange(url, ceiling));
        if (redirect(response.status)) {
            if (hop >= state.settings.redirects) {
                return statusRefused("too many redirects", url, response.status);
            }
            RAWFRAME_TRY_ASSIGN(Url next, resolveUrl(url, response.location));
            if (url.secure && !next.secure) {
                return statusRefused("a redirect from https to http is not followed", url, response.status);
            }
            url = std::move(next);
            continue;
        }
        if (response.status != 200) {
            return statusRefused("the server did not answer 200", url, response.status);
        }
        return std::move(response.body);
    }
}

const ClientStatistics& Client::statistics() const noexcept {
    return state_->statistics;
}

} // namespace rawframe::http
