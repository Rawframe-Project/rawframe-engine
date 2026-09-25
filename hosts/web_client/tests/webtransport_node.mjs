// A stand-in for the browser's WebTransport class under Node, which has none
// (D173): the W3C API's shape (ready, closed, datagrams, streams both ways)
// over webtransport_bridge.py, where aioquic does the HTTP/3. Only the page's
// tests use it; a browser has the real one.
import { spawn } from 'node:child_process';
import { createInterface } from 'node:readline';
import { fileURLToPath } from 'node:url';

const BRIDGE = fileURLToPath(new URL('./webtransport_bridge.py', import.meta.url));

function hexOf(bytes) {
    return Array.from(bytes, (byte) => byte.toString(16).padStart(2, '0')).join('');
}

export class WebTransport {
    constructor(url, options = {}) {
        const hashes = (options.serverCertificateHashes ?? []).map((hash) => hexOf(hash.value));
        this.bridge = spawn('python3', [BRIDGE, url, hashes.join(',')], { stdio: ['pipe', 'pipe', 'inherit'] });
        this.nextId = 1;
        this.ours = new Map();
        this.theirs = new Map();
        let ready;
        let failed;
        let closed;
        this.ready = new Promise((resolve, reject) => {
            ready = resolve;
            failed = reject;
        });
        this.closed = new Promise((resolve) => {
            closed = resolve;
        });
        this.ready.catch(() => {});
        const controller = {};
        const incoming = (name) => new ReadableStream({ start: (made) => { controller[name] = made; } });
        this.datagrams = {
            readable: incoming('datagrams'),
            writable: new WritableStream({ write: (bytes) => this.command({ op: 'datagram', data: Buffer.from(bytes).toString('base64') }) }),
        };
        this.incomingBidirectionalStreams = incoming('bidirectional');
        this.incomingUnidirectionalStreams = incoming('unidirectional');
        this.controller = controller;
        createInterface({ input: this.bridge.stdout }).on('line', (line) => {
            const event = JSON.parse(line);
            if (event.ev === 'ready') {
                ready();
            } else if (event.ev === 'closed') {
                failed(new Error('the session did not open'));
                closed({ closeCode: 0, reason: '' });
                for (const each of [controller.datagrams, controller.bidirectional, controller.unidirectional]) {
                    try { each.close(); } catch { /* already closed */ }
                }
            } else if (event.ev === 'datagram') {
                controller.datagrams.enqueue(new Uint8Array(Buffer.from(event.data, 'base64')));
            } else if (event.ev === 'incoming') {
                const readable = new ReadableStream({ start: (made) => { this.theirs.set(event.peer, made); } });
                if (event.uni) {
                    controller.unidirectional.enqueue(readable);
                } else {
                    // The server's two-way streams are only read here.
                    controller.bidirectional.enqueue({ readable, writable: new WritableStream() });
                }
            } else if (event.ev === 'data') {
                const target = event.id !== undefined ? this.ours.get(event.id) : this.theirs.get(event.peer);
                target?.enqueue(new Uint8Array(Buffer.from(event.data, 'base64')));
            }
        });
        this.bridge.on('exit', () => {
            failed(new Error('the bridge ended'));
            closed({ closeCode: 0, reason: '' });
        });
    }

    command(command) {
        if (this.bridge.stdin.writable) {
            this.bridge.stdin.write(JSON.stringify(command) + '\n');
        }
    }

    writableFor(id) {
        return new WritableStream({ write: (bytes) => this.command({ op: 'write', id, data: Buffer.from(bytes).toString('base64') }) });
    }

    async createBidirectionalStream() {
        const id = this.nextId++;
        this.command({ op: 'bidi', id });
        const readable = new ReadableStream({ start: (made) => { this.ours.set(id, made); } });
        return { readable, writable: this.writableFor(id) };
    }

    async createUnidirectionalStream() {
        const id = this.nextId++;
        this.command({ op: 'uni', id });
        return this.writableFor(id);
    }

    close() {
        this.command({ op: 'close' });
    }
}
