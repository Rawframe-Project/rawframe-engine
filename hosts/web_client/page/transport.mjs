// The engine's web transport (modules/network_web/.../web.h) over the
// browser's WebTransport (D173). A page cannot listen; it connects, each
// connection one WebTransport session, and numbers streams as QUIC does
// from the connecting side: its own two-way streams 0, 4, 8, one-way 2, 6,
// 10; the server's two-way 1, 5, 9, one-way 3, 7, 11. Every event waits,
// within the bounds `open` was given, until the engine polls.
//
//   const transport = new PageTransport({ certificateHashes: ['<sha-256 hex>'] });
//   imports.rawframe_web_transport = transport.importsFor(() => exports.memory);

// network::EventKind, network::CloseReason, and network::NetworkError.
const Kind = { Connected: 0, Accepted: 1, StreamBytes: 2, Datagram: 3, Closed: 4 };
const Reason = { Closed: 0, PeerGone: 1, QueueExhausted: 2 };
const Code = { Unreachable: 9, StaleConnection: 10, Exhausted: 12, WrongStream: 13 };
const HEADER = 24;

function bytesOfHex(text) {
    const bytes = new Uint8Array(text.length / 2);
    for (let at = 0; at < bytes.length; at += 1) {
        bytes[at] = parseInt(text.slice(at * 2, at * 2 + 2), 16);
    }
    return bytes;
}

export class PageTransport {
    /**
     * `certificateHashes`: the SHA-256 of each self-hosted server
     * certificate this page trusts, in hex, for `serverCertificateHashes`.
     * Without any, the browser's own certificate checks apply.
     */
    constructor({ certificateHashes = [], WebTransport = globalThis.WebTransport } = {}) {
        this.options = certificateHashes.length === 0 ? {} : {
            serverCertificateHashes: certificateHashes.map((hash) => ({ algorithm: 'sha-256', value: bytesOfHex(hash) })),
        };
        this.WebTransport = WebTransport;
        this.providers = new Map();
        this.nextProvider = 1;
        this.nextConnection = 1;
    }

    importsFor(memory) {
        const text = (at, length) => new TextDecoder().decode(new Uint8Array(memory().buffer, at, length));
        const copy = (at, length) => new Uint8Array(memory().buffer, at, length).slice();
        return {
            open: (events, bytes) => this.open(events >>> 0, bytes >>> 0),
            // A page cannot listen.
            listen: () => Code.Unreachable,
            connect: (provider, at, length) => this.connect(provider, text(at, length)),
            open_stream: (provider, id, unidirectional) => this.openStream(provider, id, unidirectional !== 0),
            send: (provider, id, stream, at, length) => this.send(provider, id, stream, copy(at, length >>> 0)),
            send_datagram: (provider, id, at, length) => this.sendDatagram(provider, id, copy(at, length >>> 0)),
            close: (provider, id, reason) => this.close(provider, id, reason),
            poll: (provider, at, capacity) => this.poll(provider, memory(), at, capacity >>> 0),
            release: (provider) => this.release(provider),
        };
    }

    open(maximumEvents, maximumBytes) {
        const id = this.nextProvider++;
        this.providers.set(id, { maximumEvents, maximumBytes, queue: [], held: new Map(), connections: new Map() });
        return id;
    }

    connect(provider, url) {
        const at = this.providers.get(provider);
        let session;
        try {
            session = new this.WebTransport(url, this.options);
        } catch {
            return 0;
        }
        const id = this.nextConnection++;
        const connection = {
            session, ready: false, open: true, streams: new Map(), datagrams: null,
            opened: { two: 0, one: 0 }, peer: { two: 0, one: 0 }, sending: 0,
        };
        at.connections.set(id, connection);
        session.ready.then(() => {
            if (!connection.open) {
                return;
            }
            connection.ready = true;
            connection.datagrams = session.datagrams.writable.getWriter();
            this.deliver(at, id, { kind: Kind.Connected, stream: 0n, bytes: new Uint8Array() });
            this.readDatagrams(at, id, connection);
            this.acceptStreams(at, id, connection, session.incomingBidirectionalStreams, false);
            this.acceptStreams(at, id, connection, session.incomingUnidirectionalStreams, true);
        }, () => {});
        session.closed.then(
            () => this.ended(at, id, connection, Reason.Closed),
            () => this.ended(at, id, connection, Reason.PeerGone));
        return id;
    }

    /** The session ended, or was ended: the engine hears it once. */
    ended(at, id, connection, reason) {
        if (!connection.open) {
            return;
        }
        connection.open = false;
        this.deliver(at, id, { kind: Kind.Closed, stream: 0n, bytes: new Uint8Array(), reason }, true);
    }

    async readDatagrams(at, id, connection) {
        const reader = connection.session.datagrams.readable.getReader();
        try {
            for (;;) {
                const { value, done } = await reader.read();
                if (done || !connection.open) {
                    return;
                }
                // Unreliable: one that does not fit is lost.
                this.deliver(at, id, { kind: Kind.Datagram, stream: 0n, bytes: new Uint8Array(value) });
            }
        } catch {
            // The session's end says why.
        }
    }

    async acceptStreams(at, id, connection, incoming, oneWay) {
        const reader = incoming.getReader();
        try {
            for (;;) {
                const { value, done } = await reader.read();
                if (done || !connection.open) {
                    return;
                }
                const number = oneWay ? connection.peer.one++ * 4 + 3 : connection.peer.two++ * 4 + 1;
                const stream = BigInt(number);
                if (oneWay) {
                    connection.streams.set(stream, { writer: null, chain: Promise.resolve() });
                    this.readStream(at, id, connection, stream, value);
                } else {
                    connection.streams.set(stream, { writer: value.writable.getWriter(), chain: Promise.resolve() });
                    this.readStream(at, id, connection, stream, value.readable);
                }
            }
        } catch {
            // The session's end says why.
        }
    }

    async readStream(at, id, connection, stream, readable) {
        const reader = readable.getReader();
        try {
            for (;;) {
                const { value, done } = await reader.read();
                if (done || !connection.open) {
                    return;
                }
                if (!this.deliver(at, id, { kind: Kind.StreamBytes, stream, bytes: new Uint8Array(value) })) {
                    // A reliable stream cannot drop bytes: the session ends.
                    this.close(this.providerOf(at), id, Reason.QueueExhausted);
                    return;
                }
            }
        } catch {
            // The session's end says why.
        }
    }

    providerOf(at) {
        for (const [provider, each] of this.providers) {
            if (each === at) {
                return provider;
            }
        }
        return 0;
    }

    connectionOf(provider, id) {
        const connection = this.providers.get(provider)?.connections.get(id);
        return connection !== undefined && connection.open && connection.ready ? connection : undefined;
    }

    openStream(provider, id, oneWay) {
        const connection = this.connectionOf(provider, id);
        if (connection === undefined) {
            return 0n;
        }
        const stream = BigInt(oneWay ? connection.opened.one++ * 4 + 2 : connection.opened.two++ * 4);
        const entry = { writer: null, chain: null };
        const at = this.providers.get(provider);
        entry.chain = (oneWay
            ? connection.session.createUnidirectionalStream()
            : connection.session.createBidirectionalStream()
        ).then((created) => {
            entry.writer = (oneWay ? created : created.writable).getWriter();
            if (!oneWay) {
                this.readStream(at, id, connection, stream, created.readable);
            }
        });
        connection.streams.set(stream, entry);
        return stream + 1n;
    }

    send(provider, id, stream, bytes) {
        const connection = this.connectionOf(provider, id);
        if (connection === undefined) {
            return Code.StaleConnection;
        }
        const entry = connection.streams.get(stream);
        const theirs = (stream & 1n) === 1n;
        if (entry === undefined || (theirs && (stream & 2n) !== 0n)) {
            return Code.WrongStream;
        }
        const at = this.providers.get(provider);
        if (connection.sending + bytes.length > at.maximumBytes) {
            // More waits to be written than the bounds allow: the session
            // cannot keep its promise, so it ends.
            this.close(provider, id, Reason.QueueExhausted);
            return Code.Exhausted;
        }
        connection.sending += bytes.length;
        entry.chain = entry.chain
            .then(() => entry.writer.write(bytes))
            .catch(() => {})
            .finally(() => {
                connection.sending -= bytes.length;
            });
        return 0;
    }

    sendDatagram(provider, id, bytes) {
        const connection = this.connectionOf(provider, id);
        if (connection === undefined) {
            return Code.StaleConnection;
        }
        // Unreliable: a write that fails is a datagram lost.
        connection.datagrams.write(bytes).catch(() => {});
        return 0;
    }

    close(provider, id, reason) {
        const at = this.providers.get(provider);
        const connection = at?.connections.get(id);
        if (connection === undefined || !connection.open) {
            return;
        }
        this.ended(at, id, connection, reason);
        try {
            connection.session.close({ closeCode: 0, reason: '' });
        } catch {
            // Already closing.
        }
    }

    /** Queues an event within the bounds per connection; a close always fits. */
    deliver(at, id, event, always = false) {
        const held = at.held.get(id) ?? { events: 0, bytes: 0 };
        if (!always && (held.events + 1 > at.maximumEvents || held.bytes + event.bytes.length > at.maximumBytes)) {
            return false;
        }
        held.events += 1;
        held.bytes += event.bytes.length;
        at.held.set(id, held);
        at.queue.push({ reason: Reason.Closed, ...event, connection: id });
        return true;
    }

    poll(provider, memory, into, capacity) {
        const at = this.providers.get(provider);
        const event = at?.queue[0];
        if (event === undefined) {
            return 0;
        }
        const size = HEADER + event.bytes.length;
        if (size > capacity) {
            return -size;
        }
        at.queue.shift();
        const held = at.held.get(event.connection);
        held.events -= 1;
        held.bytes -= event.bytes.length;
        const view = new DataView(memory.buffer, into, size);
        view.setUint8(0, event.kind);
        view.setUint8(1, event.reason);
        view.setUint16(2, 0, true);
        view.setUint32(4, event.connection, true);
        view.setBigUint64(8, event.stream, true);
        view.setUint32(16, event.bytes.length, true);
        view.setUint32(20, 0, true);
        new Uint8Array(memory.buffer, into + HEADER, event.bytes.length).set(event.bytes);
        return size;
    }

    release(provider) {
        const at = this.providers.get(provider);
        if (at === undefined) {
            return;
        }
        for (const id of at.connections.keys()) {
            this.close(provider, id, Reason.Closed);
        }
        this.providers.delete(provider);
    }
}
