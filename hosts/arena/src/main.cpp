// A server and headless bots in one process over the loopback network, for
// measuring a networked Kest game without a second machine or a real
// transport. Not a product: a Tool-role host for scenarios and budgets.
//
//   rawframe-arena [--config <file>]

#include "rawframe/host/main.h"

#include "rawframe/composition/registrar.h"
#include "rawframe/game_content/registrar.h"
#include "rawframe/input_kest/registrar.h"
#include "rawframe/network_loopback/registrar.h"
#include "rawframe/physics2d/registrar.h"
#include "rawframe/physics3d/registrar.h"
#include "rawframe/world_animation/registrar.h"
#include "rawframe/world_audio/registrar.h"
#include "rawframe/world_kest/registrar.h"
#include "rawframe/world_localization/registrar.h"
#include "rawframe/world_replication/registrar.h"
#include "rawframe/world_runtime/registrar.h"

#include <array>

namespace {

constexpr std::array<rawframe::composition::RegistrarEntry, 12> kRegistrars = {
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
};

} // namespace

int main(int argc, char** argv) {
    return rawframe::host::hostMain(
        argc,
        argv,
        {.name = "rawframe-arena", .role = rawframe::composition::TargetRole::Tool, .registrars = kRegistrars});
}
