#pragma once

// The runtime's end of the tooling protocol (ADR-0032 section 4, D408): the
// one protocol through which an authoring client, a test driver, or an
// agent speaks to a running Runtime. It runs on a provider of its own whose
// application protocol is `rawframe-tooling-v1`, never on a game's lanes.
//
// A client opens one bidirectional stream and sends records on it, one a
// line, each the compact form of a strict JSON object, as an authoring
// session does (D407); the server replies on the same stream, a line for
// each. The first record is the hello, naming the protocol version and the
// endpoint's token:
//
//   {"kind":"tooling.hello","id":1,"protocolVersion":1,"token":"..."}
//   {"kind":"tooling.reply","id":1,"answer":{"kind":"tooling.welcome","protocolVersion":1,"grants":["inspect"]}}
//
// A wrong token or version, or anything else first, is answered with an
// error record and the connection closed; so is a connection silent past
// the hello's deadline. Every client the token admits holds the endpoint's
// grants: `inspect`, which reads and never changes, and `view` (D432), which
// moves a preview's camera and nothing else.
//
//   {"kind":"tooling.status","id":2}
//     answer: {"kind":"tooling.status","tick":..,"entities":..,
//              "components":[{"name":"..","entities":..},..]}
//   {"kind":"tooling.entities","id":3,"after":"4:1","limit":64,"component":"runners.tile"}
//     answer: {"kind":"tooling.entities","entities":[{"entity":"5:1","components":[".."]},..],
//              "more":true}
//   {"kind":"tooling.read_entity","id":4,"entity":"5:1"}
//     answer: {"kind":"tooling.entity","entity":"5:1",
//              "components":[{"name":"..","fields":{"x":1.5,"kind":"open","owner":"2:1"}},..]}
//   {"kind":"tooling.look","id":5,"view":{"eye":[0,8,10],"target":[0,0,0],"fieldOfView":60}}
//     answer: {"kind":"tooling.looking","previewing":true}; a `view` of null
//     gives the player's camera back. Refused where the Runtime has no
//     preview camera (a dedicated server, a client lending none).
//   {"kind":"tooling.end","id":6}
//     answer: {"kind":"tooling.ended"}, and the server closes.
//
// An entity is named `slot:generation` (D409). `entities` lists the living
// in slot order, at most `limit` (1 to 256, 64 by default) after the one
// `after` names, holding the component named if one is; `more` says whether
// another page follows. `read_entity` shows each component's fields by name
// where the composition's ComponentFields knows them (an integer as a JSON
// integer, or as text past 2^53; a real; a truth; an entity by its name or
// null; an enum by its case's name), and a component it does not know by
// its name alone.
//
// Every reply is `tooling.reply` with the client's `id` and an `answer` or
// an `error` ({code, message}).

#include "rawframe/execution/time.h"
#include "rawframe/network/provider.h"
#include "rawframe/result/result.h"
#include "rawframe/world/time.h"
#include "rawframe/world/world.h"
#include "rawframe/world_runtime/component_fields.h"
#include "rawframe/world_runtime/picking.h"
#include "rawframe/world_tooling/preview.h"

#include <cstddef>
#include <cstdint>
#include <memory>
#include <string>

namespace rawframe::world_tooling {

inline constexpr std::uint32_t kToolingProtocolVersion = 1;

/// What a client the token admits may do.
struct ToolingGrants {
    /// Read the World and the Runtime; change nothing.
    bool inspect = false;
    /// Move a preview's camera (D432).
    bool view = false;
};

struct ToolingSettings {
    /// The secret a hello must name, at least 32 bytes.
    std::string token;
    ToolingGrants grants;
    /// A record's bytes, either way (a limit point of SPEC-0040's kind).
    std::size_t maximumRecord = std::size_t{1} << 20U;
    /// How long a connection may go without a hello that admits it.
    execution::MonotonicDuration helloWithin = execution::MonotonicDuration::fromSeconds(5);
    /// The World's components field by field, if the composition has them;
    /// it outlives the server.
    const world_runtime::ComponentFields* fields = nullptr;
    /// Where `tooling.look` goes, where the Runtime shows a preview; it
    /// outlives the server.
    Previewer* previewer = nullptr;
    /// What a ray meets, for `tooling.pick` (D456), if the composition can
    /// say; it outlives the server.
    const world_runtime::Picking* picking = nullptr;
};

/// The provider bounds a tooling endpoint asks for, `clients` at once.
[[nodiscard]] network::ProviderProfile toolingProfile(std::size_t clients) noexcept;

class ToolingServer {
public:
    /// A server on `provider`, which listens already or will; refuses
    /// (`Configuration`) a token under 32 bytes.
    [[nodiscard]] static result::Result<std::unique_ptr<ToolingServer>> create(network::Provider& provider,
                                                                               ToolingSettings settings);

    ToolingServer(const ToolingServer&) = delete;
    ToolingServer& operator=(const ToolingServer&) = delete;
    ~ToolingServer();

    /// Takes what arrived, answers every whole record from `world` (none
    /// while there is no World) as it is between ticks, `tick` the next to
    /// run, and closes connections past their hello's deadline.
    void serve(const world::World* world, world::TickIndex tick, execution::MonotonicInstant now);

    struct Statistics {
        std::uint64_t accepted = 0;
        std::uint64_t admitted = 0;
        /// Connections closed for a bad hello, a record out of form or past
        /// the limit, or silence past the deadline.
        std::uint64_t refused = 0;
        std::uint64_t records = 0;
    };
    [[nodiscard]] Statistics statistics() const noexcept;

    struct State;

private:
    explicit ToolingServer(std::unique_ptr<State> state) noexcept;
    std::unique_ptr<State> state_;
};

} // namespace rawframe::world_tooling
