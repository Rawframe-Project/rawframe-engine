# Networking

Every game is played through an authoritative server, also alone on one machine. The modules are `network` (the transport contract, the wire, sessions and admission), its providers `network_quic` (MsQuic, and WebTransport for browsers), `network_web` (the browser's side) and `network_loopback` (in-process, for tests and the arena), and `world_replication` (state, input and events between Worlds).

## Contents

- [Layers](#layers)
- [Providers](#providers)
- [The wire](#the-wire)
- [Sessions and admission](#sessions-and-admission)
- [Lanes](#lanes)
- [Replication](#replication)
- [Interest](#interest)
- [Certificates](#certificates)

## Layers

1. A **provider** moves opaque bytes: listeners, connections, streams and datagrams. It knows no World or gameplay.
2. **Sessions** turn connections into admitted peers with a control stream and declared lanes.
3. **Replication** turns a server World's committed state into what each client mirrors, and a client's input into what the server consumes.

Local play bypasses bytes, never architecture: a game played alone still has a server, its replication and its admission, connected by the loopback provider.

## Providers

The `Provider` contract is listen, connect, QUIC-numbered streams, datagrams, close, and a bounded queue of owned events the Runtime drains each iteration. Every bound is a required value of a `ProviderProfile`; zero is refused and nothing grows without one. Streams never drop: a receiving queue that fills closes the connection. Datagrams are dropped and counted instead. A stream's pending bytes are those its peer has not acknowledged, which the event bounds below count: the QUIC provider turns MsQuic's send buffering off for this, since with it on MsQuic releases a send once copied. Only the shared authoring session's host turns it on; it reads no pending bytes and sends records of megabytes. A profile also names the application protocol (QUIC's ALPN), and a listener refuses a connector speaking another: the tooling protocol and a shared authoring session never share a game's lanes.

The loopback provider delivers on a clock, with seeded latency, jitter, loss and duplication, so tests and the arena exercise the same paths under bad networks deterministically (`network.loopback.*`).

## The wire

Generation 1 of the application wire uses QUIC variable-length integers, stream prefaces, frames taken whole or waited for, and datagram records that consume their datagram exactly. Every value has exactly one accepted spelling: a non-canonical varint, an oversized length or a trailing byte is refused. Readers never allocate and never read past their input. A change to the wire is a new generation with a new protocol fingerprint, never an edit to this one.

## Sessions and admission

A client connects, opens the control stream and says hello. The server compares exact compatibility (32-byte fingerprints of everything both sides must agree on, compared whole and never parsed, so there is no downgrade), runs its admission rule (the engine's, then the game's own `admission` function in Kest, under its own fuel and heap), and answers once: an accept with fresh nonzero epochs, the tick context and the smaller of both sides' limits, or a typed reject with the first incompatibility. A connection silent past its deadline is closed.

Nothing reaches the game before admission. Datagrams before it, or on another lane, are dropped and counted. A peer that breaks the protocol is closed, and malformed input counts as a strike: the eighth within ten seconds ends an admitted connection.

## Lanes

- **State** travels as unreliable datagrams, each self-sufficient, so a lost one is simply superseded.
- **Input** travels as redundant windows of the last commands in datagrams, so one lost datagram costs nothing.
- **Mappings** (which entity a `NetEntityId` names) and the connection's control travel on the control stream. A session gathers the frames its owner sends once admitted and hands each connection's to the transport as one send: before a datagram or event to that peer (so a mapping precedes the state that uses it), when replication flushes after each tick's publish and at the end of the client's pump, or at the session's next pump. A frame each its own send cost the server tens of thousands of sends a second. A publish declares what enters a connection's view under the next IDs in turn, so its declarations are one run of IDs and go as one frame of a few bytes: the epoch, the first ID, how many, and whether the first is the connection's own player. The client acknowledges the run whole. Retirements (an entity removed, or leaving the connection's interest) go as runs too, in ascending order, each written as the gap from the run before and its length: what was declared together, as a scene's entities are, retires as one run, and what a removal scatters costs a byte or two an ID. A retirement is not acknowledged: it travels on the ordered control stream, so a client has let a mapping go before it reads any later declaration, and IDs are never reused in an epoch.

  Two bounds hold a publish's mappings. **Bytes:** a connection is sent at most `replication.mapping_bytes_per_publish` of mapping frames a publish (16 KiB by default). The client's transport holds 256 KiB of what it has not read, and a full queue ends the connection, so the default is a sixteenth of it: a client that misses sixteen publishes, about a quarter of a second at 60 a second, still holds what they sent. Retirements come first, since the client holds a mapping against its own bound until its retirement arrives; a retirement past the budget waits, its mapping held as retiring: it is sent no state, names nothing, still counts against the bound, and its entity, should it come back meanwhile, is declared anew once the retirement went. Until its retirement arrives, a removed entity stands still on the client where it was last sent. **Count:** a declaration's frame does not grow with its count, so what bounds it is the client's work: it makes an entity for each in one pump. A connection is declared at most `replication.mappings_per_publish` entities a publish, its own player first (4,096 by default, 1 to 16,384). Measured on a Ryzen 9 5900X, a client takes about 0.75 to 0.95 µs a declaration (3.0 to 3.8 ms for 4,001 in one pump, a shipping build): the default is a few milliseconds there, and the most about one 60 Hz frame. A phone may take several times as long, so a game that targets phones may want a lower value. A join in view of the most a connection may hold (65,536) is then declared over sixteen publishes, about a quarter of a second, and removing them all at once is one run, gone in the next publish. State for an entity is sent only once its declaration is acknowledged, so spreading the declarations delays little else.
- **Guaranteed events** travel on event lanes, each a one-way reliable stream of its own, declared on both sides and authorized at admission: a game's `message` lines (server to one player, delivered after the tick commits), its `command` lines (a player to the server, read on the next tick), and the engine's termination notice. Each connection's queued events are bounded (1,024 records, 128 KiB, 2 s); past any bound the lane cannot keep its promise and the connection ends as exhausted, so no promised event is silently dropped.

Commands are untrusted: one of the wrong kind or size strikes its connection, at most eight a tick are sent, and past 32 a tick a connection's are dropped.

## Replication

The server gives each admitted connection a player entity with the game's `player` components. In `apply_inputs` it consumes each connection's input exactly once per input tick, holding the last command for a few ticks when the next has not arrived and then going neutral. In `replication` it reads committed state and sends each connection datagrams for the entities whose mappings that connection has acknowledged.

Each replicated component has a codec built from its Kest layout, which writes it field by field in network byte order, never as memory. Each publish encodes every entity's values once, finds which changed, and each connection looks only at those and at what it does not yet hold. `own` components go only to the connection whose player holds them.

The client mirrors mapped entities in its own World. It applies only whole, validated datagrams and never an older value over a newer one. `interpolate` components of other entities are shown between received states, a little in the past, rather than as each arrives.

## Interest

Without an `interest` line every entity is in every connection's interest. With one, an entity whose position component lies within the radius of the player enters, and leaves beyond a slightly larger radius, so one at the edge does not flicker. An entity without the component is in every connection's interest, and a connection's own player always is. An entity that leaves is retired from the connection as though it were gone; one that comes back is declared afresh under a new id. A district a scene spawns enters together and stays while any of its entities is in range.

## Certificates

QUIC needs a server certificate. A dedicated server either makes a self-signed one as it starts (`network.quic.self_signed`) and writes its fingerprint to a file (`network.quic.fingerprint_file`), which a client pins (`network.quic.pin_file`), or uses a certificate for its host name (`network.quic.certificate_file` and `network.quic.private_key_file`). A browser trusts a self-signed certificate for at most fourteen days, so a server renews its own a day before it ends (or after `network.quic.renew_ms`): connections from then on get the new one and the fingerprint file is rewritten, while those already made keep theirs.
