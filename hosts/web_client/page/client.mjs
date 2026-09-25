// A page's hold on one web client (D169, D173): the WebAssembly module
// instantiated with the browser's WASI and the page's transport, handed the
// files the page fetched, started, driven one animation frame at a time,
// and stopped. The same module runs in a browser and, for tests, in Node.
import { browserWasi } from './wasi.mjs';

export class WebClient {
    /**
     * Instantiates the client from its module's bytes, with `transport`'s
     * imports; `log` receives each line the client writes.
     */
    static async load(moduleBytes, { transport, log }) {
        let instance;
        const memory = () => instance.exports.memory;
        const { instance: made } = await WebAssembly.instantiate(moduleBytes, {
            wasi_snapshot_preview1: browserWasi(memory, log),
            rawframe_web_transport: transport.importsFor(memory),
        });
        instance = made;
        instance.exports._initialize();
        return new WebClient(instance.exports);
    }

    constructor(exports) {
        this.exports = exports;
        this.handle = exports.rawframe_client_create();
    }

    place(bytes) {
        const at = this.exports.rawframe_allocate(bytes.length);
        new Uint8Array(this.exports.memory.buffer, at, bytes.length).set(bytes);
        return at;
    }

    /** Hands over one fetched file by its path; false if refused. */
    hold(path, bytes) {
        const name = new TextEncoder().encode(path);
        const nameAt = this.place(name);
        const bytesAt = this.place(bytes);
        const held = this.exports.rawframe_client_hold(this.handle, nameAt, name.length, bytesAt, bytes.length);
        this.exports.rawframe_release(nameAt);
        this.exports.rawframe_release(bytesAt);
        return held === 0;
    }

    /** Starts with the configuration's text: 0 when it runs. */
    start(configuration) {
        const text = new TextEncoder().encode(configuration);
        const at = this.place(text);
        const started = this.exports.rawframe_client_start(this.handle, at, text.length);
        this.exports.rawframe_release(at);
        return started;
    }

    /** One animation frame: true while the run goes on. */
    frame() {
        return this.exports.rawframe_client_frame(this.handle) === 1;
    }

    /** Stops and lets go of the client: how the run ended. */
    stop() {
        const code = this.exports.rawframe_client_stop(this.handle);
        this.exports.rawframe_client_destroy(this.handle);
        return code;
    }
}
