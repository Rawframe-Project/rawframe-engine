// A server and headless bots in one process over the loopback network, for
// measuring a networked Kest game without a second machine or a real
// transport. Not a product: a Tool-role host for scenarios and budgets.
// Where Maul RHI is built, it can also draw a client's canvas offscreen on
// the one device (D279).
//
//   rawframe-arena [--config <file>]

#include "rawframe/host/main.h"

#include "rawframe/composition/registrar.h"
#include "rawframe/game_content/registrar.h"
#include "rawframe/input_kest/registrar.h"
#include "rawframe/network_loopback/registrar.h"
#include "rawframe/physics2d/registrar.h"
#include "rawframe/physics3d/registrar.h"
#include "rawframe/render_canvas/registrar.h"
#include "rawframe/render_scene/registrar.h"
#include "rawframe/world_animation/registrar.h"
#include "rawframe/world_audio/registrar.h"
#include "rawframe/world_kest/registrar.h"
#include "rawframe/world_localization/registrar.h"
#include "rawframe/world_replication/registrar.h"
#include "rawframe/world_runtime/registrar.h"

#if RAWFRAME_ARENA_DRAWS
#include "rawframe/render/registrar.h"
#include "rawframe/render_canvas_gpu/registrar.h"
#include "rawframe/render_scene_gpu/registrar.h"
#endif

#include <array>

namespace {

#if RAWFRAME_ARENA_DRAWS
constexpr std::size_t kDrawing = 3;
#else
constexpr std::size_t kDrawing = 0;
#endif

constexpr std::array<rawframe::composition::RegistrarEntry, 14 + kDrawing> kRegistrars = {
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
#if RAWFRAME_ARENA_DRAWS
    rawframe::composition::RegistrarEntry{"render", &rawframe::render::registerParticipants, rawframe::render::kScopes},
    rawframe::composition::RegistrarEntry{
        "render_canvas_gpu", &rawframe::render_canvas_gpu::registerParticipants, rawframe::render_canvas_gpu::kScopes},
    rawframe::composition::RegistrarEntry{
        "render_scene_gpu", &rawframe::render_scene_gpu::registerParticipants, rawframe::render_scene_gpu::kScopes},
#endif
};

} // namespace

int main(int argc, char** argv) {
    return rawframe::host::hostMain(
        argc,
        argv,
        {.name = "rawframe-arena", .role = rawframe::composition::TargetRole::Tool, .registrars = kRegistrars});
}
