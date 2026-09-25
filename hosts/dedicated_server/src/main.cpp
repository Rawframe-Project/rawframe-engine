// The dedicated server process entry (ADR-0017): one Host whose composition
// is the server closure. It owns no engine semantics.
//
//   rawframe-server [--config <file>]

#include "rawframe/host/main.h"

#include "rawframe/composition/registrar.h"
#include "rawframe/network_quic/registrar.h"
#include "rawframe/physics2d/registrar.h"
#include "rawframe/world_kest/registrar.h"
#include "rawframe/world_replication/registrar.h"
#include "rawframe/world_runtime/registrar.h"

#include <array>

namespace {

// The server closure: QUIC, the World, a Kest game, 2D physics, and
// replication.
constexpr std::array<rawframe::composition::RegistrarEntry, 5> kRegistrars = {
    rawframe::composition::RegistrarEntry{
        "network_quic", &rawframe::network_quic::registerParticipants, rawframe::network_quic::kScopes},
    rawframe::composition::RegistrarEntry{
        "physics2d", &rawframe::physics2d::registerParticipants, rawframe::physics2d::kScopes},
    rawframe::composition::RegistrarEntry{
        "world_kest", &rawframe::world_kest::registerParticipants, rawframe::world_kest::kScopes},
    rawframe::composition::RegistrarEntry{
        "world_replication", &rawframe::world_replication::registerParticipants, rawframe::world_replication::kScopes},
    rawframe::composition::RegistrarEntry{
        "world_runtime", &rawframe::world_runtime::registerParticipants, rawframe::world_runtime::kScopes},
};

} // namespace

int main(int argc, char** argv) {
    return rawframe::host::hostMain(argc,
                                    argv,
                                    {.name = "rawframe-server",
                                     .role = rawframe::composition::TargetRole::DedicatedServer,
                                     .registrars = kRegistrars});
}
