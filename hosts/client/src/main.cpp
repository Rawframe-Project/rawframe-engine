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
#include <span>
#include <string>

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
    bool paths(const std::vector<std::vector<std::array<double, 3>>>& paths) override {
        camera.showPaths(paths);
        return true;
    }
    bool outlines(
        const std::vector<std::pair<std::array<float, 3>, std::vector<std::array<double, 6>>>>& outlines) override {
        std::vector<view::Outline> shown;
        for (const auto& [kColor, kBoxes] : outlines) {
            shown.push_back(view::Outline{.color = kColor, .boxes = kBoxes});
        }
        camera.showOutlines(std::move(shown));
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

    /// What an editor's viewport showing this preview handed it, acted on
    /// as the window's (D790).
    bool input(const std::vector<world_tooling::WindowInput>& events) override {
        injection.inject(events);
        return true;
    }
    /// The layers an editor hides, left out of what the scene draws
    /// (D791).
    bool hide(const std::vector<std::uint32_t>& layers) override {
        camera.hide(layers);
        return true;
    }
    /// The looks beside the preview's own (D797).
    bool split(const std::vector<world_tooling::Look>& others) override {
        std::vector<view::Perspective> made;
        for (const world_tooling::Look& kOther : others) {
            const std::optional<view::Perspective> kView = view::lookingAt(
                kOther.eye, kOther.target, static_cast<float>(kOther.fieldOfView * std::numbers::pi / 180));
            if (!kView.has_value()) {
                return false;
            }
            made.push_back(*kView);
        }
        camera.split(std::move(made));
        return true;
    }
    /// What is drawn over the scene, by the overlays' names (D798).
    bool overlay(const std::vector<std::string>& overlays) override {
        std::uint8_t bits = 0;
        for (const std::string& kName : overlays) {
            const std::optional<view::Overlay> kOverlay = view::overlayNamed(kName);
            if (!kOverlay.has_value()) {
                return false;
            }
            bits |= static_cast<std::uint8_t>(*kOverlay);
        }
        camera.overlay(bits);
        return true;
    }
    /// How the scene is drawn, by the view mode's name (D794).
    bool viewMode(std::string_view mode) override {
        const std::optional<view::ViewMode> kMode = view::viewModeNamed(mode);
        if (kMode.has_value()) {
            camera.show(*kMode);
        }
        return kMode.has_value();
    }
    /// What the frame the scene last drew from the preview holds (D842).
    std::optional<world_tooling::DrawnFrame> drawn(bool steps) const override {
        // Asked, the frames' passes are timed from now on (D894).
        camera.askTimes();
        // A frame with no scene in it (a 2D game's, D901) has its passes'
        // times and no figures.
        if (camera.descriptions() == 0 && camera.passTimings().empty()) {
            return std::nullopt;
        }
        world_tooling::DrawnFrame made{.frames = camera.descriptions()};
        for (const view::FrameFigure& kFigure : camera.figures()) {
            made.figures.emplace_back(std::string{kFigure.name}, kFigure.value);
        }
        for (const view::PassTiming& kPass : camera.passTimings()) {
            made.passes.emplace_back(kPass.pass, kPass.nanoseconds);
        }
        for (const view::FrameStep& kStep : steps ? camera.steps() : std::span<const view::FrameStep>{}) {
            made.steps.push_back(world_tooling::DrawnStep{.pass = std::string{kStep.pass},
                                                          .mesh = kStep.mesh,
                                                          .material = kStep.material,
                                                          .instances = kStep.instances});
        }
        return made;
    }

    view::PreviewCamera camera;
    world_tooling::Injection injection;
};

host::HostExit play(const host::HostRequest& request) noexcept {
    Previewer previewer;
    window_host::WindowHost client{
        request,
        window_host::WindowHostSettings{
            .lent = {composition::LentCapability{view::kPreviewCamera.name, composition::provideAs(previewer.camera)},
                     composition::LentCapability{world_tooling::kPreviewer.name,
                                                 composition::provideAs<world_tooling::Previewer>(previewer)}},
            .preview = &previewer.camera,
            .injection = &previewer.injection}};
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
