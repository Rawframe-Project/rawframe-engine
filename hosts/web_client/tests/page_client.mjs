// A page's hold on one web client: instantiated with the page's WebTransport
// imports (here the relay, tools/web_transport_relay.mjs), handed fetched
// files, started, driven a frame at a time, and stopped. What a browser page
// does, without the browser; the client is lent no file system.
import { readFile, readdir } from 'node:fs/promises';
import { join, relative } from 'node:path';
import { WASI } from 'node:wasi';

export class PageClient {
    /** Instantiates `wasmPath` with `relay` as its transport. */
    static async load(wasmPath, relay) {
        const wasi = new WASI({ args: [], env: {}, preopens: {}, returnOnExit: true });
        const module = await WebAssembly.compile(await readFile(wasmPath));
        let instance;
        instance = await WebAssembly.instantiate(module, {
            wasi_snapshot_preview1: wasi.wasiImport,
            rawframe_web_transport: relay.importsFor(() => instance.exports.memory),
        });
        wasi.initialize(instance);
        return new PageClient(instance.exports);
    }

    constructor(exports) {
        this.client = exports;
        this.handle = exports.rawframe_client_create();
    }

    /** Copies bytes into the client's memory; the caller releases them. */
    place(bytes) {
        const at = this.client.rawframe_allocate(bytes.length);
        new Uint8Array(this.client.memory.buffer, at, bytes.length).set(bytes);
        return at;
    }

    /** Hands over every file under `directory`, each as `prefix` + its path. */
    async holdDirectory(directory, prefix) {
        const encoder = new TextEncoder();
        for (const path of await filesUnder(directory)) {
            const name = encoder.encode(prefix + relative(directory, path).split('\\').join('/'));
            const bytes = await readFile(path);
            const nameAt = this.place(name);
            const bytesAt = this.place(bytes);
            const held = this.client.rawframe_client_hold(this.handle, nameAt, name.length, bytesAt, bytes.length);
            this.client.rawframe_release(nameAt);
            this.client.rawframe_release(bytesAt);
            if (held !== 0) {
                throw new Error(`a file was refused: ${path}`);
            }
        }
    }

    /** Starts with the configuration's text: 0 when it runs. */
    start(configuration) {
        const text = new TextEncoder().encode(configuration);
        const at = this.place(text);
        const started = this.client.rawframe_client_start(this.handle, at, text.length);
        this.client.rawframe_release(at);
        return started;
    }

    /** One animation frame: true while the run goes on. */
    frame() {
        return this.client.rawframe_client_frame(this.handle) === 1;
    }

    /** Stops and lets go of the client: how the run ended. */
    stop() {
        const code = this.client.rawframe_client_stop(this.handle);
        this.client.rawframe_client_destroy(this.handle);
        return code;
    }
}

/** About one animation frame. */
export const nextFrame = () => new Promise((resolve) => setTimeout(resolve, 16));

async function filesUnder(directory) {
    const found = [];
    for (const entry of await readdir(directory, { withFileTypes: true })) {
        const path = join(directory, entry.name);
        if (entry.isDirectory()) {
            found.push(...(await filesUnder(path)));
        } else if (entry.isFile()) {
            found.push(path);
        }
    }
    return found;
}
