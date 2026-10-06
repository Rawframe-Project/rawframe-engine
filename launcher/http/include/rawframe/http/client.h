#pragma once

// A blocking HTTP/1.1 client for the launcher (D414): GET only, over TCP or
// TLS, one connection kept for as long as the server keeps it. A secure
// connection verifies the server's certificate for the host against the
// trusted authorities, always: there is no way to trust any (SPEC-0020).
// Nothing a client fetches is trusted for having been fetched; what it
// carries is verified by its digest by whoever asked for it.

#include "rawframe/http/url.h"
#include "rawframe/result/result.h"

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <memory>
#include <string>
#include <string_view>
#include <vector>

namespace rawframe::http {

struct ClientSettings {
    /// A PEM file of the authorities to trust; empty, the system's.
    std::filesystem::path authorities;
    /// How long a connection or a read may wait, in milliseconds.
    std::uint32_t timeoutMilliseconds = 30'000;
    /// Redirects followed at most; never from https to http.
    std::uint32_t redirects = 4;
    std::string agent = "rawframe";
};

/// What a client has done, for a report.
struct ClientStatistics {
    std::uint64_t requests = 0;
    std::uint64_t connections = 0;
    std::uint64_t bytes = 0;
};

class Client {
public:
    /// Refuses (`NoAuthorities`) when the authorities cannot be read, or
    /// when the system's cannot be found.
    [[nodiscard]] static result::Result<std::unique_ptr<Client>> create(ClientSettings settings);

    Client(const Client&) = delete;
    Client& operator=(const Client&) = delete;
    ~Client();

    /// The body at `url`, at most `ceiling` bytes. Refuses a status other
    /// than 200 (`Status`, with the status as context) after any redirects.
    [[nodiscard]] result::Result<std::vector<std::byte>> get(std::string_view url, std::uint64_t ceiling);

    [[nodiscard]] const ClientStatistics& statistics() const noexcept;

    struct State;

private:
    explicit Client(std::unique_ptr<State> state) noexcept;
    std::unique_ptr<State> state_;
};

} // namespace rawframe::http
