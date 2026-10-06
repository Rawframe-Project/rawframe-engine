#include "connection.h"

#include "rawframe/http/errors.h"

#include <array>
#include <chrono>
#include <openssl/err.h>
#include <openssl/ssl.h>
#include <openssl/x509v3.h>
#include <string>
#include <system_error>

#if defined(_WIN32)
#include <winsock2.h>
#include <ws2tcpip.h>
// clang-format off
#include <windows.h>
#include <wincrypt.h>
// clang-format on
#else
#include <arpa/inet.h>
#include <cerrno>
#include <fcntl.h>
#include <netdb.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <poll.h>
#include <sys/socket.h>
#include <unistd.h>
#endif

namespace rawframe::http {

namespace {

#if defined(_WIN32)
using Native = SOCKET;
constexpr Native kNoSocket = INVALID_SOCKET;
void closeNative(Native native) noexcept {
    ::closesocket(native);
}
int pollNative(pollfd* polled, int milliseconds) noexcept {
    return ::WSAPoll(polled, 1, milliseconds);
}
#else
using Native = int;
constexpr Native kNoSocket = -1;
void closeNative(Native native) noexcept {
    ::close(native);
}
int pollNative(pollfd* polled, int milliseconds) noexcept {
    return ::poll(polled, 1, milliseconds);
}
#endif

std::unexpected<result::Error> failed(HttpError error, std::string_view why) {
    const result::ErrorClass kClass =
        error == HttpError::Untrusted ? result::ErrorClass::PermissionDenied : result::ErrorClass::Unavailable;
    return std::unexpected<result::Error>{result::fail(kClass, kHttpDomain, code(error), why).error()};
}

/// Where systems keep the authorities they trust, as one PEM file: Debian
/// and Ubuntu, Fedora and RHEL, openSUSE, older RHEL, and Alpine and macOS.
constexpr std::array<const char*, 5> kSystemAuthorities = {
    "/etc/ssl/certs/ca-certificates.crt",
    "/etc/pki/tls/certs/ca-bundle.crt",
    "/etc/ssl/ca-bundle.pem",
    "/etc/pki/tls/cacert.pem",
    "/etc/ssl/cert.pem",
};

/// Waits until `native` can be read (or written), at most `milliseconds`.
bool ready(Native native, bool writing, std::uint32_t milliseconds) noexcept {
    pollfd polled{};
    polled.fd = native;
    polled.events = writing ? POLLOUT : POLLIN;
    return pollNative(&polled, static_cast<int>(milliseconds)) > 0;
}

} // namespace

Sockets::Sockets() noexcept {
#if defined(_WIN32)
    WSADATA started{};
    ::WSAStartup(MAKEWORD(2, 2), &started);
#endif
}

Sockets::~Sockets() {
#if defined(_WIN32)
    ::WSACleanup();
#endif
}

struct Tls::State {
    SSL_CTX* context = nullptr;
    BIO_METHOD* method = nullptr;

    ~State() {
        SSL_CTX_free(context);
        BIO_meth_free(method);
    }
};

struct Connection::State {
    Native native = kNoSocket;
    SSL* tls = nullptr;
    bool secure = false;
    std::string host;
    std::uint16_t port = 0;
    std::uint32_t timeout = 0;
    /// Why the last read or write through TLS failed, when it was the
    /// socket's doing.
    bool timedOut = false;

    ~State() {
        SSL_free(tls);
        if (native != kNoSocket) {
            closeNative(native);
        }
    }

    /// The socket's own read and write, every wait bounded.
    long rawWrite(const char* data, std::size_t size) noexcept {
        if (!ready(native, true, timeout)) {
            timedOut = true;
            return -1;
        }
#if defined(_WIN32)
        return ::send(native, data, static_cast<int>(std::min<std::size_t>(size, 1U << 30U)), 0);
#elif defined(MSG_NOSIGNAL)
        return ::send(native, data, size, MSG_NOSIGNAL);
#else
        return ::send(native, data, size, 0);
#endif
    }

    long rawRead(char* data, std::size_t size) noexcept {
        if (!ready(native, false, timeout)) {
            timedOut = true;
            return -1;
        }
#if defined(_WIN32)
        return ::recv(native, data, static_cast<int>(std::min<std::size_t>(size, 1U << 30U)), 0);
#else
        return ::recv(native, data, size, 0);
#endif
    }
};

namespace {

// The BIO between OpenSSL and the socket.
int bioWrite(BIO* bio, const char* data, std::size_t size, std::size_t* written) {
    auto* state = static_cast<Connection::State*>(BIO_get_data(bio));
    const long kSent = state->rawWrite(data, size);
    if (kSent <= 0) {
        return 0;
    }
    *written = static_cast<std::size_t>(kSent);
    return 1;
}

int bioRead(BIO* bio, char* data, std::size_t size, std::size_t* read) {
    auto* state = static_cast<Connection::State*>(BIO_get_data(bio));
    const long kCame = state->rawRead(data, size);
    if (kCame <= 0) {
        return 0;
    }
    *read = static_cast<std::size_t>(kCame);
    return 1;
}

long bioControl(BIO* /*bio*/, int command, long /*number*/, void* /*pointer*/) {
    return command == BIO_CTRL_FLUSH ? 1 : 0;
}

bool loadSystemAuthorities(SSL_CTX* context) {
#if defined(_WIN32)
    HCERTSTORE store = ::CertOpenSystemStoreW(0, L"ROOT");
    if (store == nullptr) {
        return false;
    }
    X509_STORE* trusted = SSL_CTX_get_cert_store(context);
    int added = 0;
    for (PCCERT_CONTEXT each = ::CertEnumCertificatesInStore(store, nullptr); each != nullptr;
         each = ::CertEnumCertificatesInStore(store, each)) {
        const unsigned char* bytes = each->pbCertEncoded;
        X509* certificate = d2i_X509(nullptr, &bytes, static_cast<long>(each->cbCertEncoded));
        if (certificate != nullptr) {
            added += X509_STORE_add_cert(trusted, certificate) == 1 ? 1 : 0;
            X509_free(certificate);
        }
    }
    ::CertCloseStore(store, 0);
    return added > 0;
#else
    for (const char* kFile : kSystemAuthorities) {
        if (::access(kFile, R_OK) == 0 && SSL_CTX_load_verify_locations(context, kFile, nullptr) == 1) {
            return true;
        }
    }
    return false;
#endif
}

bool literalAddress(const std::string& host) noexcept {
    std::array<unsigned char, 16> address{};
    return ::inet_pton(AF_INET, host.c_str(), address.data()) == 1 ||
           ::inet_pton(AF_INET6, host.c_str(), address.data()) == 1;
}

} // namespace

Tls::Tls(std::unique_ptr<State> state) noexcept : state_(std::move(state)) {
}

Tls::~Tls() = default;

result::Result<std::unique_ptr<Tls>> Tls::create(const std::filesystem::path& authorities) {
    auto state = std::make_unique<State>();
    state->context = SSL_CTX_new(TLS_client_method());
    state->method = BIO_meth_new(BIO_get_new_index() | BIO_TYPE_SOURCE_SINK, "rawframe socket");
    if (state->context == nullptr || state->method == nullptr) {
        return failed(HttpError::NoAuthorities, "TLS could not be set up");
    }
    BIO_meth_set_write_ex(state->method, bioWrite);
    BIO_meth_set_read_ex(state->method, bioRead);
    BIO_meth_set_ctrl(state->method, bioControl);
    SSL_CTX_set_min_proto_version(state->context, TLS1_2_VERSION);
    SSL_CTX_set_verify(state->context, SSL_VERIFY_PEER, nullptr);
    // A close without TLS's own goodbye ends a read like any close: whether
    // the response was whole is the response's to say, and what it carries
    // is verified by digest anyway.
    SSL_CTX_set_options(state->context, SSL_OP_IGNORE_UNEXPECTED_EOF);
    ERR_clear_error();
    const bool kLoaded =
        authorities.empty() ? loadSystemAuthorities(state->context)
                            : SSL_CTX_load_verify_locations(state->context, authorities.string().c_str(), nullptr) == 1;
    ERR_clear_error();
    if (!kLoaded) {
        return std::unexpected<result::Error>{
            failed(HttpError::NoAuthorities, "no trusted authorities could be read")
                .error()
                .withContext("authorities", authorities.empty() ? "the system's" : authorities.generic_string())};
    }
    return std::unique_ptr<Tls>{new Tls{std::move(state)}};
}

Connection::Connection(std::unique_ptr<State> state) noexcept : state_(std::move(state)) {
}

Connection::~Connection() = default;

result::Result<std::unique_ptr<Connection>>
Connection::open(const Url& url, Tls& tls, std::uint32_t timeoutMilliseconds) {
    auto state = std::make_unique<State>();
    state->secure = url.secure;
    state->host = url.host;
    state->port = url.port;
    state->timeout = timeoutMilliseconds;
    addrinfo hints{};
    hints.ai_family = AF_UNSPEC;
    hints.ai_socktype = SOCK_STREAM;
    addrinfo* found = nullptr;
    const std::string kPort = std::to_string(url.port);
    if (::getaddrinfo(url.host.c_str(), kPort.c_str(), &hints, &found) != 0 || found == nullptr) {
        return std::unexpected<result::Error>{
            failed(HttpError::Unreachable, "the host's name did not resolve").error().withContext("host", url.host)};
    }
    for (const addrinfo* each = found; each != nullptr && state->native == kNoSocket; each = each->ai_next) {
        const Native kNative = ::socket(each->ai_family, each->ai_socktype, each->ai_protocol);
        if (kNative == kNoSocket) {
            continue;
        }
        // Connect without blocking, so the wait is bounded, then block again:
        // every later wait is a poll's.
#if defined(_WIN32)
        u_long nonblocking = 1;
        ::ioctlsocket(kNative, FIONBIO, &nonblocking);
#else
        const int kFlags = ::fcntl(kNative, F_GETFL, 0);
        ::fcntl(kNative, F_SETFL, kFlags | O_NONBLOCK);
#if defined(SO_NOSIGPIPE)
        const int kOn = 1;
        ::setsockopt(kNative, SOL_SOCKET, SO_NOSIGPIPE, &kOn, sizeof(kOn));
#endif
#endif
        const int kStarted = ::connect(kNative, each->ai_addr, static_cast<int>(each->ai_addrlen));
        int problem = 0;
        if (kStarted != 0) {
            socklen_t length = sizeof(problem);
            const bool kAnswered =
                ready(kNative, true, timeoutMilliseconds) &&
                ::getsockopt(kNative, SOL_SOCKET, SO_ERROR, reinterpret_cast<char*>(&problem), &length) == 0;
            problem = kAnswered ? problem : 1;
        }
        if (problem != 0) {
            closeNative(kNative);
            continue;
        }
#if defined(_WIN32)
        nonblocking = 0;
        ::ioctlsocket(kNative, FIONBIO, &nonblocking);
#else
        ::fcntl(kNative, F_SETFL, kFlags);
#endif
        const int kNoDelay = 1;
        ::setsockopt(kNative, IPPROTO_TCP, TCP_NODELAY, reinterpret_cast<const char*>(&kNoDelay), sizeof(kNoDelay));
        state->native = kNative;
    }
    ::freeaddrinfo(found);
    if (state->native == kNoSocket) {
        return std::unexpected<result::Error>{failed(HttpError::Unreachable, "no address of the host answered")
                                                  .error()
                                                  .withContext("host", url.host)
                                                  .withContext("port", kPort)};
    }
    if (!url.secure) {
        return std::unique_ptr<Connection>{new Connection{std::move(state)}};
    }
    ERR_clear_error();
    state->tls = SSL_new(tls.state().context);
    BIO* bio = BIO_new(tls.state().method);
    if (state->tls == nullptr || bio == nullptr) {
        BIO_free(bio);
        return failed(HttpError::Untrusted, "TLS could not be set up");
    }
    BIO_set_data(bio, state.get());
    BIO_set_init(bio, 1);
    SSL_set_bio(state->tls, bio, bio);
    // The certificate must be the host's: its name, or its address.
    if (literalAddress(url.host)) {
        X509_VERIFY_PARAM_set1_ip_asc(SSL_get0_param(state->tls), url.host.c_str());
    } else {
        SSL_set_tlsext_host_name(state->tls, url.host.c_str());
        SSL_set1_host(state->tls, url.host.c_str());
    }
    if (SSL_connect(state->tls) != 1) {
        const long kVerdict = SSL_get_verify_result(state->tls);
        ERR_clear_error();
        if (state->timedOut) {
            return failed(HttpError::Interrupted, "the server went silent while TLS was agreed");
        }
        if (kVerdict != X509_V_OK) {
            return std::unexpected<result::Error>{
                failed(HttpError::Untrusted, "the server's certificate is not trusted for the host")
                    .error()
                    .withContext("host", url.host)
                    .withContext("why", X509_verify_cert_error_string(kVerdict))};
        }
        return std::unexpected<result::Error>{
            failed(HttpError::Untrusted, "TLS could not be agreed").error().withContext("host", url.host)};
    }
    return std::unique_ptr<Connection>{new Connection{std::move(state)}};
}

result::Status Connection::write(std::span<const std::byte> bytes) {
    State& state = *state_;
    while (!bytes.empty()) {
        long sent = 0;
        if (state.tls != nullptr) {
            std::size_t written = 0;
            ERR_clear_error();
            sent = SSL_write_ex(state.tls, bytes.data(), bytes.size(), &written) == 1 ? static_cast<long>(written) : -1;
        } else {
            sent = state.rawWrite(reinterpret_cast<const char*>(bytes.data()), bytes.size());
        }
        if (sent <= 0) {
            return failed(HttpError::Interrupted, "the request could not be sent");
        }
        bytes = bytes.subspan(static_cast<std::size_t>(sent));
    }
    return {};
}

result::Result<std::size_t> Connection::read(std::span<std::byte> room) {
    State& state = *state_;
    if (state.tls != nullptr) {
        std::size_t came = 0;
        ERR_clear_error();
        if (SSL_read_ex(state.tls, room.data(), room.size(), &came) == 1) {
            return came;
        }
        const int kWhy = SSL_get_error(state.tls, 0);
        ERR_clear_error();
        if (kWhy == SSL_ERROR_ZERO_RETURN || (kWhy == SSL_ERROR_SYSCALL && !state.timedOut)) {
            // Closed, cleanly or not: what the response needed decides.
            return std::size_t{0};
        }
        return failed(HttpError::Interrupted, "the connection went silent or failed");
    }
    const long kCame = state.rawRead(reinterpret_cast<char*>(room.data()), room.size());
    if (kCame < 0) {
        return failed(HttpError::Interrupted, "the connection went silent or failed");
    }
    return static_cast<std::size_t>(kCame);
}

bool Connection::to(const Url& url) const noexcept {
    return state_->secure == url.secure && state_->host == url.host && state_->port == url.port;
}

} // namespace rawframe::http
