// Bakes a game's reflection probes (D326): a server and one headless bot
// in one process over the loopback network, as the arena runs them, the
// bot's mirrored World drawn offscreen on the one device, and each probe
// the scene extracts drawn six ways and written as the Radiance picture its
// environment texture names. A Tool-role host: it reaches the importers,
// which no process that plays may.
//
//   rawframe-bake [--config <file>]

#include "rawframe/host/main.h"

#include "rawframe/composition/registrar.h"
#include "rawframe/game_content/registrar.h"
#include "rawframe/input_kest/registrar.h"
#include "rawframe/network_loopback/registrar.h"
#include "rawframe/physics2d/registrar.h"
#include "rawframe/physics3d/registrar.h"
#include "rawframe/render/registrar.h"
#include "rawframe/render_canvas/registrar.h"
#include "rawframe/render_canvas_gpu/registrar.h"
#include "rawframe/render_scene/registrar.h"
#include "rawframe/render_scene_gpu/registrar.h"
#include "rawframe/scene_bake/registrar.h"
#include "rawframe/world_animation/registrar.h"
#include "rawframe/world_audio/registrar.h"
#include "rawframe/world_kest/registrar.h"
#include "rawframe/world_localization/registrar.h"
#include "rawframe/world_replication/registrar.h"
#include "rawframe/world_runtime/registrar.h"
#include "rawframe/world_ui/registrar.h"

#include <array>

namespace {

constexpr std::array<rawframe::composition::RegistrarEntry, 19> kRegistrars = {
    rawframe::composition::RegistrarEntry{
        "game_content", &rawframe::game_content::registerParticipants, rawframe::game_content::kScopes},
    rawframe::composition::RegistrarEntry{
        "network_loopback", &rawframe::network_loopback::registerParticipants, rawframe::network_loopback::kScopes},
    rawframe::composition::RegistrarEntry{
        "physics2d", &rawframe::physics2d::registerParticipants, rawframe::physics2d::kScopes},
    rawframe::composition::RegistrarEntry{
        "physics3d", &rawframe::physics3d::registerParticipants, rawframe::physics3d::kScopes},
    rawframe::composition::RegistrarEntry{
        "input_kest", &rawframe::input_kest::registerParticipants, rawframe::input_kest::kScopes},
    rawframe::composition::RegistrarEntry{
        "render_canvas", &rawframe::render_canvas::registerParticipants, rawframe::render_canvas::kScopes},
    rawframe::composition::RegistrarEntry{
        "world_ui", &rawframe::world_ui::registerParticipants, rawframe::world_ui::kScopes},
    rawframe::composition::RegistrarEntry{
        "render_scene", &rawframe::render_scene::registerParticipants, rawframe::render_scene::kScopes},
    rawframe::composition::RegistrarEntry{
        "world_audio", &rawframe::world_audio::registerParticipants, rawframe::world_audio::kScopes},
    rawframe::composition::RegistrarEntry{
        "world_animation", &rawframe::world_animation::registerParticipants, rawframe::world_animation::kScopes},
    rawframe::composition::RegistrarEntry{
        "world_kest", &rawframe::world_kest::registerParticipants, rawframe::world_kest::kScopes},
    rawframe::composition::RegistrarEntry{"world_localization",
                                          &rawframe::world_localization::registerParticipants,
                                          rawframe::world_localization::kScopes},
    rawframe::composition::RegistrarEntry{
        "world_replication", &rawframe::world_replication::registerParticipants, rawframe::world_replication::kScopes},
    rawframe::composition::RegistrarEntry{
        "world_runtime", &rawframe::world_runtime::registerParticipants, rawframe::world_runtime::kScopes},
    rawframe::composition::RegistrarEntry{
        "world_runtime.saves", &rawframe::world_runtime::registerSaves, rawframe::world_runtime::kScopes},
    rawframe::composition::RegistrarEntry{"render", &rawframe::render::registerParticipants, rawframe::render::kScopes},
    rawframe::composition::RegistrarEntry{
        "render_canvas_gpu", &rawframe::render_canvas_gpu::registerParticipants, rawframe::render_canvas_gpu::kScopes},
    rawframe::composition::RegistrarEntry{
        "render_scene_gpu", &rawframe::render_scene_gpu::registerParticipants, rawframe::render_scene_gpu::kScopes},
    rawframe::composition::RegistrarEntry{
        "scene_bake", &rawframe::scene_bake::registerParticipants, rawframe::scene_bake::kScopes},
};

} // namespace

int main(int argc, char** argv) {
    return rawframe::host::hostMain(
        argc,
        argv,
        {.name = "rawframe-bake", .role = rawframe::composition::TargetRole::Tool, .registrars = kRegistrars});
}
