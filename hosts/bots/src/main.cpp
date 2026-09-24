// Headless bots playing a Kest game on a server in another process, over
// QUIC: the load for measuring a dedicated server the way players reach it.
// The game is compiled for its replication plan only (`kest.plan_only`); the
// World here holds nothing but each bot's mirror. Not a product: a Tool-role
// host for runs and budgets.
//
//   rawframe-bots [--config <file>]

#include "rawframe/host/main.h"

#include "rawframe/composition/registrar.h"
#include "rawframe/network_quic/registrar.h"
#include "rawframe/world_kest/registrar.h"
#include "rawframe/world_replication/registrar.h"
#include "rawframe/world_runtime/registrar.h"

#include <array>

namespace {

constexpr std::array<rawframe::composition::RegistrarEntry, 4> kRegistrars = {
    rawframe::composition::RegistrarEntry{
        "network_quic", &rawframe::network_quic::registerParticipants, rawframe::network_quic::kScopes},
    rawframe::composition::RegistrarEntry{
        "world_kest", &rawframe::world_kest::registerParticipants, rawframe::world_kest::kScopes},
    rawframe::composition::RegistrarEntry{
        "world_replication", &rawframe::world_replication::registerParticipants, rawframe::world_replication::kScopes},
    rawframe::composition::RegistrarEntry{
        "world_runtime", &rawframe::world_runtime::registerParticipants, rawframe::world_runtime::kScopes},
};

} // namespace

int main(int argc, char** argv) {
    return rawframe::host::hostMain(
        argc,
        argv,
        {.name = "rawframe-bots", .role = rawframe::composition::TargetRole::Tool, .registrars = kRegistrars});
}
