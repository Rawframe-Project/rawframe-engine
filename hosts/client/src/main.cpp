// A desktop client (D249): one window, and a Host playing a Kest game on a
// server over QUIC as the process's own player, whose input comes from the
// window. Where Maul RHI is built, its canvas is drawn on the one device and
// shown in the window (D280); elsewhere it is heard, not yet seen.
//
//   rawframe-client [--config <file>]
//
// The window system owns the loop (SPEC-0025, D248), so the Host is driven
// from its frames (`window_host`, D250): each frame hands the window's raw
// input to the player's devices, runs every Host iteration due, then waits
// for the next one. The configuration plays the player with
// `bots.player = true`; closing the window ends the run the way a stop
// request does.

#include "rawframe/host/main.h"

#include "rawframe/composition/registrar.h"
#include "rawframe/game_content/registrar.h"
#include "rawframe/input_kest/registrar.h"
#include "rawframe/network_quic/registrar.h"
#include "rawframe/render_canvas/registrar.h"
#include "rawframe/render_scene/registrar.h"
#include "rawframe/view/preview.h"
#include "rawframe/window/windows.h"
#include "rawframe/window_host/window_host.h"
#include "rawframe/world_animation/registrar.h"
#include "rawframe/world_audio/registrar.h"
#include "rawframe/world_kest/registrar.h"
#include "rawframe/world_localization/registrar.h"
#include "rawframe/world_replication/registrar.h"
#include "rawframe/world_runtime/registrar.h"
#include "rawframe/world_tooling/preview.h"
#include "rawframe/world_tooling/registrar.h"
#include "rawframe/world_ui/registrar.h"

#if RAWFRAME_CLIENT_DRAWS
#include "rawframe/render/registrar.h"
#include "rawframe/render_canvas_gpu/registrar.h"
#include "rawframe/render_scene_gpu/registrar.h"
#endif
#if RAWFRAME_CLIENT_HEADSET
#include "rawframe/xr/registrar.h"
#endif

#include <array>
#include <cstdio>
#include <numbers>
#include <optional>

namespace {

using namespace rawframe;

#if RAWFRAME_CLIENT_DRAWS
constexpr std::size_t kDrawing = 3;
#else
constexpr std::size_t kDrawing = 0;
#endif
// A headset, where OpenXR is built (D593).
#if RAWFRAME_CLIENT_HEADSET
constexpr std::size_t kHeadset = 1;
#else
constexpr std::size_t kHeadset = 0;
#endif

constexpr std::array<composition::RegistrarEntry, 13 + kDrawing + kHeadset> kRegistrars = {
    composition::RegistrarEntry{"game_content", &game_content::registerParticipants, game_content::kScopes},
    composition::RegistrarEntry{"network_quic", &network_quic::registerParticipants, network_quic::kScopes},
    composition::RegistrarEntry{"input_kest", &input_kest::registerParticipants, input_kest::kScopes},
    composition::RegistrarEntry{"render_canvas", &render_canvas::registerParticipants, render_canvas::kScopes},
    composition::RegistrarEntry{"world_ui", &world_ui::registerParticipants, world_ui::kScopes},
    composition::RegistrarEntry{"world_tooling", &world_tooling::registerParticipants, world_tooling::kScopes},
    composition::RegistrarEntry{"render_scene", &render_scene::registerParticipants, render_scene::kScopes},
    composition::RegistrarEntry{"world_animation", &world_animation::registerParticipants, world_animation::kScopes},
    composition::RegistrarEntry{"world_audio", &world_audio::registerParticipants, world_audio::kScopes},
    composition::RegistrarEntry{"world_kest", &world_kest::registerParticipants, world_kest::kScopes},
    composition::RegistrarEntry{
        "world_localization", &world_localization::registerParticipants, world_localization::kScopes},
    composition::RegistrarEntry{
        "world_replication", &world_replication::registerParticipants, world_replication::kScopes},
    composition::RegistrarEntry{"world_runtime", &world_runtime::registerParticipants, world_runtime::kScopes},
#if RAWFRAME_CLIENT_DRAWS
    composition::RegistrarEntry{"render", &render::registerParticipants, render::kScopes},
    composition::RegistrarEntry{
        "render_canvas_gpu", &render_canvas_gpu::registerParticipants, render_canvas_gpu::kScopes},
    composition::RegistrarEntry{"render_scene_gpu", &render_scene_gpu::registerParticipants, render_scene_gpu::kScopes},
#endif
#if RAWFRAME_CLIENT_HEADSET
    composition::RegistrarEntry{"xr", &xr::registerParticipants, xr::kScopes},
#endif
};

/// A tooling client's look onto the preview's camera (D432): the client's
/// own tooling endpoint hands it what an author looks from, and the scene
/// renderer looks through the camera.
class Previewer final : public world_tooling::Previewer {
public:
    bool look(const std::optional<world_tooling::Look>& look) override {
        if (!look.has_value()) {
            camera.look(std::nullopt);
            return true;
        }
        const std::optional<view::Perspective> kView =
            view::lookingAt(look->eye, look->target, static_cast<float>(look->fieldOfView * std::numbers::pi / 180));
        if (!kView.has_value()) {
            return false;
        }
        camera.look(kView);
        return true;
    }
    bool mark(const std::optional<std::array<double, 3>>& at, std::uint8_t lit) override {
        camera.mark(at,
                    lit <= static_cast<std::uint8_t>(view::MarkPart::Ring) ? static_cast<view::MarkPart>(lit)
                                                                           : view::MarkPart::None);
        return true;
    }
    std::optional<world_tooling::Clicked> clicked() const override {
        const std::optional<view::Ray>& kRay = camera.lastClick();
        const std::optional<view::Ray>& kPointing = camera.pointing();
        if (!kRay.has_value() && !kPointing.has_value() && camera.wheel() == 0 && camera.fly() == 0 &&
            camera.orbit() == std::array<double, 2>{} && camera.look() == std::array<double, 2>{}) {
            return std::nullopt;
        }
        // A thousand metres along the press's direction: past anything a
        // scene an author edits holds.
        constexpr double kReach = 1000;
        world_tooling::Clicked made{.count = camera.clicks(),
                                    .modifiers = camera.clickModifiers(),
                                    .wheel = camera.wheel(),
                                    .orbit = camera.orbit(),
                                    .look = camera.look(),
                                    .fly = camera.fly()};
        if (kRay.has_value()) {
            made.origin = kRay->origin;
            made.toward = {kRay->direction[0] * kReach, kRay->direction[1] * kReach, kRay->direction[2] * kReach};
        }
        if (kPointing.has_value()) {
            made.pointing = true;
            made.pointOrigin = kPointing->origin;
            made.pointToward = {
                kPointing->direction[0] * kReach, kPointing->direction[1] * kReach, kPointing->direction[2] * kReach};
        }
        if (const std::optional<view::Ray>& kRelease = camera.lastRelease(); kRelease.has_value()) {
            made.released = camera.releases();
            made.releaseOrigin = kRelease->origin;
            made.releaseToward = {
                kRelease->direction[0] * kReach, kRelease->direction[1] * kReach, kRelease->direction[2] * kReach};
        }
        return made;
    }

    view::PreviewCamera camera;
};

host::HostExit play(const host::HostRequest& request) noexcept {
    Previewer previewer;
    window_host::WindowHost client{
        request,
        window_host::WindowHostSettings{
            .lent = {composition::LentCapability{view::kPreviewCamera.name, composition::provideAs(previewer.camera)},
                     composition::LentCapability{world_tooling::kPreviewer.name,
                                                 composition::provideAs<world_tooling::Previewer>(previewer)}},
            .preview = &previewer.camera}};
    const result::Status kRan = window::run(client, window::RunSettings{});
    if (client.exit().has_value()) {
        return *client.exit();
    }
    // No Host ran: the window system could not be reached, or refused.
    if (!kRan.has_value()) {
        std::fprintf(stderr,
                     "rawframe-client: %.*s\n",
                     static_cast<int>(kRan.error().description().size()),
                     kRan.error().description().data());
    }
    return host::HostExit::UnsupportedConfiguration;
}

} // namespace

int main(int argc, char** argv) {
    return host::hostMain(argc,
                          argv,
                          {.name = "rawframe-client",
                           .role = composition::TargetRole::Client,
                           .registrars = kRegistrars,
                           .drive = &play,
                           // Its participants' stop budgets came to the
                           // default's whole 5 s before a preview's tooling
                           // endpoint joined them (D432).
                           .defaultShutdownBudgetMs = 5500});
}
