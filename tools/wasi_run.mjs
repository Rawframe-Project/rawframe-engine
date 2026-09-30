// Runs a wasm32-wasi program under Node's WASI, the web build's test
// runner (cmake/wasm32-wasi.cmake): its arguments after the module's path,
// the host's environment, and the whole file system, as a native test has;
// and, for a program that uses the web transport, a relay standing in for
// the page's WebTransport (tools/web_transport_relay.mjs). A program that
// holds the device gets Maul RHI's page side from the maul-rhi.mjs its
// build wrote (RAWFRAME_MAUL_RHI_GLUE, D282): Node has no WebGPU, so it
// finds no adapter, and a GPU is never required of it here; drawing on the
// web is checked in a browser (browser_play.mjs).
import { noWindowPage } from './no_window_page.mjs';
import { readFile } from 'node:fs/promises';
import { WASI } from 'node:wasi';
import { argv, env, exit } from 'node:process';
import { pathToFileURL } from 'node:url';
import { WebTransportRelay } from './web_transport_relay.mjs';

const { RAWFRAME_REQUIRE_GPU: _, ...programEnv } = env;
// Preview 1 is the ABI wasi-libc targets; Node 20 and later require it named.
const wasi = new WASI({ version: 'preview1', args: argv.slice(2), env: programEnv, preopens: { '/': '/' },
                        returnOnExit: true });
const module = await WebAssembly.compile(await readFile(argv[2]));
let instance;
const relay = new WebTransportRelay();
let device = {};
if (env.RAWFRAME_MAUL_RHI_GLUE) {
    const { maulRhiImports } = await import(pathToFileURL(env.RAWFRAME_MAUL_RHI_GLUE).href);
    device = maulRhiImports(() => instance.exports);
}
instance = await WebAssembly.instantiate(module, {
    wasi_snapshot_preview1: wasi.wasiImport,
    rawframe_web_transport: relay.importsFor(() => instance.exports.memory),
    env: new Proxy(device, { get: (target, name) => target[name] ?? noWindowPage[name] }),
});
exit(wasi.start(instance));
