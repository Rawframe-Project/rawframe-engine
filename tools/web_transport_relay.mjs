// What a page gives the engine's web transport (modules/network_web/.../web.h),
// with a relay standing in for WebTransport: providers in this JavaScript
// realm, in one WebAssembly instance or several, reach each other's
// listeners by name, and every byte is copied. A browser page implements the
// same imports over real WebTransport sessions.
//
//   const relay = new WebTransportRelay();
//   imports.rawframe_web_transport = relay.importsFor(() => instance.exports.memory);

// network::EventKind, network::CloseReason, and network::NetworkError.
const Kind = { Connected: 0, Accepted: 1, StreamBytes: 2, Datagram: 3, Closed: 4 };
const Reason = { Closed: 0, PeerGone: 1, QueueExhausted: 2 };
const Code = { Unreachable: 9, StaleConnection: 10, Exhausted: 12, WrongStream: 13 };
const HEADER = 24;

export class WebTransportRelay {
    constructor() {
        this.providers = new Map();
        this.listeners = new Map();
        this.links = new Map();
        this.nextProvider = 1;
        this.nextLink = 1;
    }

    /** The imports for one instance, whose memory `memory()` answers. */
    importsFor(memory) {
        const text = (at, length) => new TextDecoder().decode(new Uint8Array(memory().buffer, at, length));
        const copy = (at, length) => new Uint8Array(memory().buffer, at, length).slice();
        return {
            open: (events, bytes) => this.open(events >>> 0, bytes >>> 0),
            listen: (provider, at, length) => this.listen(provider, text(at, length)),
            connect: (provider, at, length) => this.connect(provider, text(at, length)),
            open_stream: (provider, link, unidirectional) => this.openStream(provider, link, unidirectional !== 0),
            send: (provider, link, stream, at, length) => this.send(provider, link, stream, copy(at, length >>> 0)),
            send_datagram: (provider, link, at, length) => this.sendDatagram(provider, link, copy(at, length >>> 0)),
            close: (provider, link, reason) => this.close(provider, link, reason),
            poll: (provider, at, capacity) => this.poll(provider, memory(), at, capacity >>> 0),
            release: (provider) => this.release(provider),
        };
    }

    open(maximumEvents, maximumBytes) {
        const id = this.nextProvider++;
        this.providers.set(id, { maximumEvents, maximumBytes, queue: [], held: new Map() });
        return id;
    }

    listen(provider, name) {
        if (this.listeners.has(name)) {
            return Code.Unreachable;
        }
        this.listeners.set(name, provider);
        return 0;
    }

    connect(provider, name) {
        const listener = this.listeners.get(name);
        if (listener === undefined || listener === provider) {
            return 0;
        }
        const id = this.nextLink++;
        // The connector's end first, as QUIC numbers streams: its streams
        // have the low bit clear.
        this.links.set(id, { ends: [provider, listener], opened: [0, 0], open: true });
        this.deliver(listener, id, { kind: Kind.Accepted, stream: 0n, bytes: new Uint8Array() });
        this.deliver(provider, id, { kind: Kind.Connected, stream: 0n, bytes: new Uint8Array() });
        return id;
    }

    /** The link and which end `provider` is, or nothing. */
    endOf(provider, id) {
        const link = this.links.get(id);
        if (link === undefined || !link.open) {
            return undefined;
        }
        const side = link.ends.indexOf(provider);
        return side < 0 ? undefined : { link, side };
    }

    openStream(provider, id, unidirectional) {
        const end = this.endOf(provider, id);
        if (end === undefined) {
            return 0n;
        }
        const stream = BigInt(end.link.opened[end.side]++ * 4 + end.side + (unidirectional ? 2 : 0));
        return stream + 1n;
    }

    send(provider, id, stream, bytes) {
        const end = this.endOf(provider, id);
        if (end === undefined) {
            return Code.StaleConnection;
        }
        const openedBy = Number(stream & 1n);
        if ((stream & 2n) !== 0n && openedBy !== end.side) {
            return Code.WrongStream;
        }
        const peer = end.link.ends[1 - end.side];
        if (!this.deliver(peer, id, { kind: Kind.StreamBytes, stream, bytes })) {
            // A reliable stream cannot drop bytes: the connection ends.
            this.close(provider, id, Reason.QueueExhausted);
            return Code.Exhausted;
        }
        return 0;
    }

    sendDatagram(provider, id, bytes) {
        const end = this.endOf(provider, id);
        if (end === undefined) {
            return Code.StaleConnection;
        }
        this.deliver(end.link.ends[1 - end.side], id, { kind: Kind.Datagram, stream: 0n, bytes });
        return 0;
    }

    close(provider, id, reason) {
        const end = this.endOf(provider, id);
        if (end === undefined) {
            return;
        }
        end.link.open = false;
        for (const each of end.link.ends) {
            this.deliver(each, id, { kind: Kind.Closed, stream: 0n, bytes: new Uint8Array(), reason }, true);
        }
    }

    /**
     * Queues an event within the provider's bounds per connection; false
     * when it did not fit. A close always fits.
     */
    deliver(provider, id, event, always = false) {
        const at = this.providers.get(provider);
        if (at === undefined) {
            return false;
        }
        const held = at.held.get(id) ?? { events: 0, bytes: 0 };
        if (!always && (held.events + 1 > at.maximumEvents || held.bytes + event.bytes.length > at.maximumBytes)) {
            return false;
        }
        held.events += 1;
        held.bytes += event.bytes.length;
        at.held.set(id, held);
        at.queue.push({ reason: Reason.Closed, ...event, link: id });
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
        const held = at.held.get(event.link);
        held.events -= 1;
        held.bytes -= event.bytes.length;
        const view = new DataView(memory.buffer, into, size);
        view.setUint8(0, event.kind);
        view.setUint8(1, event.reason);
        view.setUint16(2, 0, true);
        view.setUint32(4, event.link, true);
        view.setBigUint64(8, event.stream, true);
        view.setUint32(16, event.bytes.length, true);
        view.setUint32(20, 0, true);
        new Uint8Array(memory.buffer, into + HEADER, event.bytes.length).set(event.bytes);
        return size;
    }

    release(provider) {
        for (const [id, link] of this.links) {
            if (link.open && link.ends.includes(provider)) {
                link.open = false;
                const peer = link.ends[1 - link.ends.indexOf(provider)];
                this.deliver(peer, id, { kind: Kind.Closed, stream: 0n, bytes: new Uint8Array(), reason: Reason.PeerGone }, true);
            }
        }
        for (const [name, listener] of this.listeners) {
            if (listener === provider) {
                this.listeners.delete(name);
            }
        }
        this.providers.delete(provider);
    }
}
