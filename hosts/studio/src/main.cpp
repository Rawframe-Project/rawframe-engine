// Studio (ADR-0032, D435): a window onto an authoring session, never part of
// a client or a server. It is a client of the session like any other: it
// holds one in its own process and speaks only the session's records, so
// nothing it does is beyond what `rawframe-author session` does.
//
//   rawframe-studio [--config <file>] [<game description>]
//
// The configuration names the game (`studio.game`) and, unless its
// directory, the scenes' root (`studio.root`); `render.device` chooses the
// device it draws on. Its window is driven as a client's is
// (`window_host`); closing it ends Studio as a stop request does.

#include "rawframe/host/main.h"

#include "rawframe/composition/registrar.h"
#include "rawframe/render/registrar.h"
#include "rawframe/render_canvas_gpu/registrar.h"
#include "rawframe/studio/registrar.h"
#include "rawframe/window/windows.h"
#include "rawframe/window_host/window_host.h"

#include <array>
#include <span>
#include <vector>
#include <cstdio>

namespace {

using namespace rawframe;

constexpr std::array<composition::RegistrarEntry, 3> kRegistrars = {
    composition::RegistrarEntry{"render", &render::registerParticipants, render::kScopes},
    composition::RegistrarEntry{
        "render_canvas_gpu", &render_canvas_gpu::registerParticipants, render_canvas_gpu::kScopes},
    composition::RegistrarEntry{"studio", &studio::registerParticipants, studio::kScopes},
};

host::HostExit play(const host::HostRequest& request) noexcept {
    // Its own icon in the title bar and the taskbar (D920).
    std::vector<window::IconImage> icon;
    std::span<const std::uint8_t> pixels = studio::appIconPixels();
    for (const std::uint32_t kSide : studio::kAppIconSides) {
        const std::size_t kBytes = static_cast<std::size_t>(kSide) * kSide * 4;
        icon.push_back(window::IconImage{.width = kSide, .height = kSide, .pixels = pixels.first(kBytes)});
        pixels = pixels.subspan(kBytes);
    }
    window_host::WindowHost shown{request,
                                  window_host::WindowHostSettings{.title = "Rawframe Studio", .icon = std::move(icon)}};
    const result::Status kRan = window::run(shown, window::RunSettings{});
    if (shown.exit().has_value()) {
        return *shown.exit();
    }
    // No Host ran: the window system could not be reached, or refused.
    if (!kRan.has_value()) {
        std::fprintf(stderr,
                     "rawframe-studio: %.*s\n",
                     static_cast<int>(kRan.error().description().size()),
                     kRan.error().description().data());
    }
    return host::HostExit::UnsupportedConfiguration;
}

} // namespace

int main(int argc, char** argv) {
    return host::hostMain(
        argc,
        argv,
        {.name = "rawframe-studio",
         .role = composition::TargetRole::Tool,
         .registrars = kRegistrars,
         .drive = &play,
         // Opened as an application is, with no file (D920): drawn on the
         // best device there is, a game given as its argument opened.
         .defaults = "render.device = any\nwindow.maximized = true\nwindow.width = 1280\nwindow.height = 760\n",
         .fileKey = "studio.game"});
}
