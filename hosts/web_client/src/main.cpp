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
//   rawframe_client_start(client, configuration, length) -> 0 or an exit code
//   rawframe_client_frame(client) -> 1 while running, 0 once ended
//   rawframe_client_stop(client) -> an exit code (host::exitCode)
//   rawframe_client_destroy(client)
//   rawframe_allocate(size) -> memory the page writes into; rawframe_release
//
// Or the page lets a canvas run the client (D250): `play` instead of
// `start` makes a window of a canvas and plays the player from its input,
// and the window system's frames, not the page's, drive the Host; with
// `render.device`, the game is drawn in the canvas through the browser's
// WebGPU (D282):
//
//   rawframe_client_play(client, configuration, length) -> 0 or an exit code
//   rawframe_client_ended(client) -> -1 while it plays, then its exit code
//   rawframe_client_stop(client) -> -1: asks it to stop; `ended` says when
//
// A client that plays is destroyed once it has ended.

#include "rawframe/composition/held_files.h"
#include "rawframe/composition/registrar.h"
#include "rawframe/execution/time.h"
#include "rawframe/game_content/registrar.h"
#include "rawframe/host/host.h"
#include "rawframe/input_kest/registrar.h"
#include "rawframe/network_web/registrar.h"
#include "rawframe/physics2d/registrar.h"
#include "rawframe/physics3d/registrar.h"
#include "rawframe/render/registrar.h"
#include "rawframe/render_canvas/registrar.h"
#include "rawframe/render_canvas_gpu/registrar.h"
#include "rawframe/window/windows.h"
#include "rawframe/window_host/window_host.h"
#include "rawframe/world_animation/registrar.h"
#include "rawframe/world_audio/frame_sink.h"
#include "rawframe/world_audio/registrar.h"
#include "rawframe/world_kest/registrar.h"
#include "rawframe/world_replication/registrar.h"
#include "rawframe/world_runtime/registrar.h"

#include <array>
#include <atomic>
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
// Kest game, 2D and 3D physics, the simulation's animation, replication
// over the page's WebTransport with the game's input sources, and its sound,
// which the page takes and plays (D259).
constexpr std::array<composition::RegistrarEntry, 13> kRegistrars = {
    composition::RegistrarEntry{"game_content", &game_content::registerParticipants, game_content::kScopes},
    composition::RegistrarEntry{"input_kest", &input_kest::registerParticipants, input_kest::kScopes},
    composition::RegistrarEntry{"network_web", &network_web::registerParticipants, network_web::kScopes},
    composition::RegistrarEntry{"physics2d", &physics2d::registerParticipants, physics2d::kScopes},
    composition::RegistrarEntry{"physics3d", &physics3d::registerParticipants, physics3d::kScopes},
    composition::RegistrarEntry{"render", &render::registerParticipants, render::kScopes},
    composition::RegistrarEntry{"render_canvas", &render_canvas::registerParticipants, render_canvas::kScopes},
    composition::RegistrarEntry{
        "render_canvas_gpu", &render_canvas_gpu::registerParticipants, render_canvas_gpu::kScopes},
    composition::RegistrarEntry{"world_animation", &world_animation::registerParticipants, world_animation::kScopes},
    composition::RegistrarEntry{"world_audio", &world_audio::registerParticipants, world_audio::kScopes},
    composition::RegistrarEntry{"world_kest", &world_kest::registerParticipants, world_kest::kScopes},
    composition::RegistrarEntry{
        "world_replication", &world_replication::registerParticipants, world_replication::kScopes},
    composition::RegistrarEntry{"world_runtime", &world_runtime::registerParticipants, world_runtime::kScopes},
};

// The most iterations one frame runs to catch up: a page hidden for a while
// is not replayed in one frame.
constexpr int kMostIterationsPerFrame = 4;

/// The page's sound (D259): the frames the world audio player renders each
/// frame, held until the page takes them for the browser's own audio
/// thread. Half a second of room: a page that stops taking loses sound, not
/// memory.
class PageSound final : public world_audio::FrameSink {
public:
    static constexpr std::uint32_t kRate = 48'000;
    static constexpr std::size_t kRoomFrames = kRate / 2;

    [[nodiscard]] std::uint32_t rate() const noexcept override {
        return kRate;
    }
    [[nodiscard]] std::size_t room() const noexcept override {
        return kRoomFrames - (held_.size() / 2);
    }
    void write(std::span<const float> frames) noexcept override {
        held_.insert(held_.end(),
                     frames.begin(),
                     frames.begin() + static_cast<std::ptrdiff_t>(std::min(frames.size(), room() * 2)));
    }
    /// Moves up to `frames` of the oldest into `taken`: how many it moved.
    std::size_t take(std::size_t frames) {
        const std::size_t kMoved = std::min(frames, held_.size() / 2);
        taken.assign(held_.begin(), held_.begin() + static_cast<std::ptrdiff_t>(kMoved * 2));
        held_.erase(held_.begin(), held_.begin() + static_cast<std::ptrdiff_t>(kMoved * 2));
        return kMoved;
    }

    /// What the last `take` moved, interleaved stereo, for the page to copy.
    std::vector<float> taken;

private:
    std::vector<float> held_;
};

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
    /// A client that plays: the window's program, which outlives its run.
    std::unique_ptr<window_host::WindowHost> player;
    PageSound sound;
    std::array<composition::LentCapability, 1> lent{composition::LentCapability{
        world_audio::kFrameSink.name, composition::provideAs<world_audio::FrameSink>(sound)}};
    std::atomic<bool> stopRequested{false};

    /// Takes the held files and the configuration: false when either is
    /// refused.
    bool prepare(std::string_view text) {
        auto held = composition::HeldFiles::of(std::move(fetched));
        auto parsed = composition::Configuration::parse(text);
        if (!held.has_value() || !parsed.has_value()) {
            return false;
        }
        files = std::move(*held);
        configuration = std::move(*parsed);
        return true;
    }

    [[nodiscard]] host::HostRequest request() {
        return host::HostRequest{.role = composition::TargetRole::Client,
                                 .platform = composition::Platform::Web,
                                 .registrars = kRegistrars,
                                 .configuration = &*configuration,
                                 .log = {.write = &writeStandardOutput},
                                 .stopRequested = &stopRequested,
                                 .files = &files,
                                 .lent = lent};
    }

    [[nodiscard]] bool used() const noexcept {
        return host != nullptr || player != nullptr;
    }
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
    if (client == nullptr || client->used()) {
        return 1;
    }
    client->fetched.emplace_back(std::string{path, pathLength}, std::vector<std::byte>{bytes, bytes + length});
    return 0;
}

/// Starts the Host over the held files and `configuration`'s text: 0 when
/// it runs, otherwise how it ended.
__attribute__((export_name("rawframe_client_start"))) int
rawframeClientStart(Client* client, const char* configuration, std::size_t length) {
    if (client == nullptr || client->used()) {
        return host::exitCode(host::HostExit::InvalidInvocation);
    }
    if (!client->prepare(std::string_view{configuration, length})) {
        return host::exitCode(host::HostExit::InvalidLaunchDescriptor);
    }
    client->host = std::make_unique<host::Host>(client->request());
    // A refused start has ended already; its first iteration says so.
    client->running = client->host->iterate();
    return client->running ? 0 : host::exitCode(client->host->stop());
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

/// Plays from a canvas with the configuration's text: 0 when it plays, or
/// how it ended; ResourceUnavailable where there is no page to play in.
__attribute__((export_name("rawframe_client_play"))) int
rawframeClientPlay(Client* client, const char* configuration, std::size_t length) {
    if (client == nullptr || client->used()) {
        return host::exitCode(host::HostExit::InvalidInvocation);
    }
    if (!client->prepare(std::string_view{configuration, length})) {
        return host::exitCode(host::HostExit::InvalidLaunchDescriptor);
    }
    client->player = std::make_unique<window_host::WindowHost>(
        client->request(), window_host::WindowHostSettings{.lent = {client->lent.begin(), client->lent.end()}});
    if (!window::run(*client->player, window::RunSettings{}).has_value() && !client->player->exit().has_value()) {
        return host::exitCode(host::HostExit::ResourceUnavailable);
    }
    return 0;
}

/// How a client that plays ended, or -1 while it plays.
__attribute__((export_name("rawframe_client_ended"))) int rawframeClientEnded(Client* client) {
    if (client == nullptr || client->player == nullptr) {
        return host::exitCode(host::HostExit::InvalidInvocation);
    }
    const auto kExit = client->player->exit();
    return kExit.has_value() ? host::exitCode(*kExit) : -1;
}

__attribute__((export_name("rawframe_client_stop"))) int rawframeClientStop(Client* client) {
    if (client != nullptr && client->player != nullptr) {
        // The Host drains and ends at a frame of the window's.
        client->stopRequested.store(true);
        return -1;
    }
    if (client == nullptr || client->host == nullptr) {
        return host::exitCode(host::HostExit::InvalidInvocation);
    }
    client->running = false;
    return host::exitCode(client->host->stop());
}

/// The rate the page plays the client's sound at, in frames a second.
__attribute__((export_name("rawframe_client_sound_rate"))) std::uint32_t rawframeClientSoundRate(Client* /*client*/) {
    return PageSound::kRate;
}

/// Takes up to `frames` of the client's sound, oldest first, for
/// `rawframe_client_sound_frames` to point at: how many it took.
__attribute__((export_name("rawframe_client_sound_take"))) std::size_t rawframeClientSoundTake(Client* client,
                                                                                               std::size_t frames) {
    return client == nullptr ? 0 : client->sound.take(frames);
}

/// Where the frames the last take took are, interleaved stereo floats.
__attribute__((export_name("rawframe_client_sound_frames"))) const float* rawframeClientSoundFrames(Client* client) {
    return client == nullptr ? nullptr : client->sound.taken.data();
}

__attribute__((export_name("rawframe_client_destroy"))) void rawframeClientDestroy(Client* client) {
    // A client still playing is the window system's until it ends.
    if (client != nullptr && client->player != nullptr && !client->player->exit().has_value()) {
        return;
    }
    delete client;
}

} // extern "C"
