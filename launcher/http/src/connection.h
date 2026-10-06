#pragma once

// One TCP connection, plain or TLS, with every wait bounded: the client's
// transport. Sockets are the platform's; TLS is OpenSSL's, through a BIO of
// this file's own, so a write to a closed peer is an error, not a signal.

#include "rawframe/http/url.h"
#include "rawframe/result/result.h"

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <memory>
#include <span>

namespace rawframe::http {

/// The TLS configuration every secure connection of a client shares: its
/// trusted authorities and protocol floor (TLS 1.2).
class Tls {
public:
    [[nodiscard]] static result::Result<std::unique_ptr<Tls>> create(const std::filesystem::path& authorities);
    Tls(const Tls&) = delete;
    Tls& operator=(const Tls&) = delete;
    ~Tls();

    struct State;
    [[nodiscard]] State& state() noexcept {
        return *state_;
    }

private:
    explicit Tls(std::unique_ptr<State> state) noexcept;
    std::unique_ptr<State> state_;
};

class Connection {
public:
    /// Connects to `url`'s host and port, then agrees TLS when the URL is
    /// https, verifying the certificate for the host.
    [[nodiscard]] static result::Result<std::unique_ptr<Connection>>
    open(const Url& url, Tls& tls, std::uint32_t timeoutMilliseconds);
    Connection(const Connection&) = delete;
    Connection& operator=(const Connection&) = delete;
    ~Connection();

    [[nodiscard]] result::Status write(std::span<const std::byte> bytes);
    /// What came, at most `room.size()` bytes; 0 when the peer closed.
    [[nodiscard]] result::Result<std::size_t> read(std::span<std::byte> room);

    /// Whether this connection is to `url`'s scheme, host, and port.
    [[nodiscard]] bool to(const Url& url) const noexcept;

    struct State;

private:
    explicit Connection(std::unique_ptr<State> state) noexcept;
    std::unique_ptr<State> state_;
};

/// Starts and ends the platform's sockets (Windows counts its starts).
class Sockets {
public:
    Sockets() noexcept;
    Sockets(const Sockets&) = delete;
    Sockets& operator=(const Sockets&) = delete;
    ~Sockets();
};

} // namespace rawframe::http
