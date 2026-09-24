// A server and headless bots in one process over the loopback network, for
// measuring a networked Kest game without a second machine or a real
// transport. Not a product: a Tool-role host for scenarios and budgets.
//
//   rawframe-arena [--config <file>]

#include "rawframe/host/main.h"

#include "rawframe/composition/registrar.h"
#include "rawframe/network_loopback/registrar.h"
#include "rawframe/world_kest/registrar.h"
#include "rawframe/world_replication/registrar.h"
#include "rawframe/world_runtime/registrar.h"

#include <array>

namespace {

constexpr std::array<rawframe::composition::RegistrarEntry, 4> kRegistrars = {
    rawframe::composition::RegistrarEntry{
        "network_loopback", &rawframe::network_loopback::registerParticipants, rawframe::network_loopback::kScopes},
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
        {.name = "rawframe-arena", .role = rawframe::composition::TargetRole::Tool, .registrars = kRegistrars});
}
