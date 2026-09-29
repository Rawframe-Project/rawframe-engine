// A page's hold on one web client (D169, D173): the WebAssembly module
// instantiated with the browser's WASI and the page's transport, handed the
// files the page fetched, started, driven one animation frame at a time,
// and stopped. The same module runs in a browser and, for tests, in Node.
//
// Or it plays (D250): the window's side on the page, Maul Window's
// maul-window.mjs, makes a window of a canvas, and the window's frames
// drive the client until it ends. Its sound is the page's to take and play
// (sound.mjs, D259).
import { browserWasi } from './wasi.mjs';

/** The window's imports where the page gives none: there is no page. */
const noPage = new Proxy({ mwinWebHasDocument: () => 0 }, {
    get: (target, name) => target[name] ?? (() => {
        throw new Error(`${String(name)} needs the window's page side`);
    }),
});

export class WebClient {
    /**
     * Instantiates the client from its module's bytes, or from the module
     * already compiled (as `WebAssembly.compileStreaming` gives it while it
     * downloads), with `transport`'s imports; `log` receives each line the
     * client writes. `windowImports`, the `maulWindowImports` of
     * maul-window.mjs, lets it play.
     */
    static async load(module, { transport, log, windowImports }) {
        let instance;
        const memory = () => instance.exports.memory;
        const made = await WebAssembly.instantiate(module, {
            wasi_snapshot_preview1: browserWasi(memory, log),
            rawframe_web_transport: transport.importsFor(memory),
            env: windowImports ? windowImports(() => instance.exports) : noPage,
        });
        instance = made instanceof WebAssembly.Instance ? made : made.instance;
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

    /**
     * Plays from a canvas with the configuration's text: 0 when it plays.
     * The window's frames drive it from then on.
     */
    play(configuration) {
        const text = new TextEncoder().encode(configuration);
        const at = this.place(text);
        const played = this.exports.rawframe_client_play(this.handle, at, text.length);
        this.exports.rawframe_release(at);
        return played;
    }

    /** How a client that plays ended, or null while it plays. */
    ended() {
        const code = this.exports.rawframe_client_ended(this.handle);
        return code < 0 ? null : code;
    }

    /** Asks a client that plays to stop; ended() says when it has. */
    requestStop() {
        this.exports.rawframe_client_stop(this.handle);
    }

    /** Frames a second of the client's sound. */
    soundRate() {
        return this.exports.rawframe_client_sound_rate(this.handle);
    }

    /**
     * Takes up to `frames` frames of the client's sound, oldest first, as
     * interleaved stereo samples copied out of its memory (D259).
     */
    takeSound(frames) {
        const taken = this.exports.rawframe_client_sound_take(this.handle, frames);
        if (taken === 0) {
            return new Float32Array(0);
        }
        const at = this.exports.rawframe_client_sound_frames(this.handle);
        return new Float32Array(this.exports.memory.buffer, at, taken * 2).slice();
    }

    /** Lets go of a client that played and ended. */
    destroy() {
        this.exports.rawframe_client_destroy(this.handle);
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
