// The web client entry (ADR-0084, D169): a WebAssembly reactor whose
// exports a page's JavaScript calls. The page hands over the files it
// fetched and the configuration, starts one Host, drives it from its own
// event loop one frame at a time, and stops it. Nothing here blocks, sleeps,
// or reads a disk; diagnostics go to standard output as NDJSON, which the
// page's WASI shim carries.
//
// Every export takes the client its `create` gave, so the module holds no
// state of its own:
//
//   rawframe_client_create() -> client
//   rawframe_client_hold(client, path, path_length, bytes, length) -> 0 or 1
//   rawframe_client_start(client, configuration, length) -> 0 or a HostExit
//   rawframe_client_frame(client) -> 1 while running, 0 once ended
//   rawframe_client_stop(client) -> HostExit
//   rawframe_client_destroy(client)
//   rawframe_allocate(size) -> memory the page writes into; rawframe_release

#include "rawframe/composition/held_files.h"
#include "rawframe/composition/registrar.h"
#include "rawframe/execution/time.h"
#include "rawframe/game_content/registrar.h"
#include "rawframe/host/host.h"
#include "rawframe/input_kest/registrar.h"
#include "rawframe/network_web/registrar.h"
#include "rawframe/physics2d/registrar.h"
#include "rawframe/physics3d/registrar.h"
#include "rawframe/world_animation/registrar.h"
#include "rawframe/world_kest/registrar.h"
#include "rawframe/world_replication/registrar.h"
#include "rawframe/world_runtime/registrar.h"

#include <array>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <memory>
#include <optional>
#include <string>
#include <utility>
#include <vector>

namespace {

using namespace rawframe;

// A client playing a game from its content or held sources: the World, a
// Kest game, 2D and 3D physics, the simulation's animation, and replication
// over the page's WebTransport with the game's input sources.
constexpr std::array<composition::RegistrarEntry, 9> kRegistrars = {
    composition::RegistrarEntry{"game_content", &game_content::registerParticipants, game_content::kScopes},
    composition::RegistrarEntry{"input_kest", &input_kest::registerParticipants, input_kest::kScopes},
    composition::RegistrarEntry{"network_web", &network_web::registerParticipants, network_web::kScopes},
    composition::RegistrarEntry{"physics2d", &physics2d::registerParticipants, physics2d::kScopes},
    composition::RegistrarEntry{"physics3d", &physics3d::registerParticipants, physics3d::kScopes},
    composition::RegistrarEntry{"world_animation", &world_animation::registerParticipants, world_animation::kScopes},
    composition::RegistrarEntry{"world_kest", &world_kest::registerParticipants, world_kest::kScopes},
    composition::RegistrarEntry{
        "world_replication", &world_replication::registerParticipants, world_replication::kScopes},
    composition::RegistrarEntry{"world_runtime", &world_runtime::registerParticipants, world_runtime::kScopes},
};

// The most iterations one frame runs to catch up: a page hidden for a while
// is not replayed in one frame.
constexpr int kMostIterationsPerFrame = 4;

bool writeStandardOutput(void*, std::span<const char> bytes) noexcept {
    const bool kWritten = std::fwrite(bytes.data(), 1, bytes.size(), stdout) == bytes.size();
    return std::fflush(stdout) == 0 && kWritten;
}

struct Client {
    std::vector<composition::HeldFiles::File> fetched;
    composition::HeldFiles files;
    std::optional<composition::Configuration> configuration;
    std::unique_ptr<host::Host> host;
    execution::SteadyClock clock;
    bool running = false;
};

} // namespace

extern "C" {

__attribute__((export_name("rawframe_allocate"))) void* rawframeAllocate(std::size_t size) {
    return std::malloc(size == 0 ? 1 : size);
}

__attribute__((export_name("rawframe_release"))) void rawframeRelease(void* memory) {
    std::free(memory);
}

__attribute__((export_name("rawframe_client_create"))) Client* rawframeClientCreate() {
    return new (std::nothrow) Client{};
}

/// Holds one fetched file by its path; refused (1) once started.
__attribute__((export_name("rawframe_client_hold"))) int rawframeClientHold(
    Client* client, const char* path, std::size_t pathLength, const std::byte* bytes, std::size_t length) {
    if (client == nullptr || client->host != nullptr) {
        return 1;
    }
    client->fetched.emplace_back(std::string{path, pathLength}, std::vector<std::byte>{bytes, bytes + length});
    return 0;
}

/// Starts the Host over the held files and `configuration`'s text: 0 when
/// it runs, otherwise how it ended.
__attribute__((export_name("rawframe_client_start"))) int
rawframeClientStart(Client* client, const char* configuration, std::size_t length) {
    if (client == nullptr || client->host != nullptr) {
        return static_cast<int>(host::HostExit::StartupFailed);
    }
    auto held = composition::HeldFiles::of(std::move(client->fetched));
    auto parsed = composition::Configuration::parse(std::string_view{configuration, length});
    if (!held.has_value() || !parsed.has_value()) {
        return static_cast<int>(host::HostExit::StartupFailed);
    }
    client->files = std::move(*held);
    client->configuration = std::move(*parsed);
    client->host = std::make_unique<host::Host>(host::HostRequest{.role = composition::TargetRole::Client,
                                                                  .platform = composition::Platform::Web,
                                                                  .registrars = kRegistrars,
                                                                  .configuration = &*client->configuration,
                                                                  .log = {.write = &writeStandardOutput},
                                                                  .files = &client->files});
    // A refused start has ended already; its first iteration says so.
    client->running = client->host->iterate();
    return client->running ? 0 : static_cast<int>(client->host->stop());
}

/// One frame of the page: every iteration due, up to a few. 1 while the
/// run goes on, 0 once it has ended.
__attribute__((export_name("rawframe_client_frame"))) int rawframeClientFrame(Client* client) {
    if (client == nullptr || !client->running) {
        return 0;
    }
    for (int ran = 0; ran < kMostIterationsPerFrame && client->clock.now() >= client->host->due(); ++ran) {
        if (!client->host->iterate()) {
            client->running = false;
            return 0;
        }
    }
    return 1;
}

__attribute__((export_name("rawframe_client_stop"))) int rawframeClientStop(Client* client) {
    if (client == nullptr || client->host == nullptr) {
        return static_cast<int>(host::HostExit::StartupFailed);
    }
    client->running = false;
    return static_cast<int>(client->host->stop());
}

__attribute__((export_name("rawframe_client_destroy"))) void rawframeClientDestroy(Client* client) {
    delete client;
}

} // extern "C"
