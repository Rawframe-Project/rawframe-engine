// Plays the page a web client runs in: fetches a game's files (here, read
// from the repository), hands them to the client under `game/`, starts it
// with a configuration naming the game, drives it once a frame the way a
// browser's animation frames would, and stops it. The client is lent no
// file system: everything it reads was handed over.
//
// usage: page.mjs <rawframe-web-client.wasm> <game directory>
import { readFile, readdir } from 'node:fs/promises';
import { join, relative } from 'node:path';
import { WASI } from 'node:wasi';
import { argv, exit } from 'node:process';

const [wasmPath, gameDirectory] = argv.slice(2);
const wasi = new WASI({ args: [], env: {}, preopens: {}, returnOnExit: true });
const module = await WebAssembly.compile(await readFile(wasmPath));
const instance = await WebAssembly.instantiate(module, { wasi_snapshot_preview1: wasi.wasiImport });
wasi.initialize(instance);
const client = instance.exports;

/** Copies bytes into the client's memory; the caller releases them. */
function place(bytes) {
    const at = client.rawframe_allocate(bytes.length);
    new Uint8Array(client.memory.buffer, at, bytes.length).set(bytes);
    return at;
}

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

const handle = client.rawframe_client_create();
const encoder = new TextEncoder();
for (const path of await filesUnder(gameDirectory)) {
    const name = encoder.encode('game/' + relative(gameDirectory, path).split('\\').join('/'));
    const bytes = await readFile(path);
    const nameAt = place(name);
    const bytesAt = place(bytes);
    if (client.rawframe_client_hold(handle, nameAt, name.length, bytesAt, bytes.length) !== 0) {
        console.log('page: a file was refused');
        exit(1);
    }
    client.rawframe_release(nameAt);
    client.rawframe_release(bytesAt);
}

const configuration = encoder.encode(
    'kest.game = game/movers.game\nworld.tick_rate = 60\nhost.iteration_rate = 120\nhost.maximum_iterations = 30\n');
const configurationAt = place(configuration);
const started = client.rawframe_client_start(handle, configurationAt, configuration.length);
client.rawframe_release(configurationAt);
if (started !== 0) {
    console.log(`page: start refused, exit ${started}`);
    exit(1);
}

// Animation frames at about 60 a second, until the run ends.
let frames = 0;
while (client.rawframe_client_frame(handle) === 1) {
    frames += 1;
    await new Promise((resolve) => setTimeout(resolve, 16));
}
const code = client.rawframe_client_stop(handle);
client.rawframe_client_destroy(handle);
console.log(`page: ${frames} frames, exit ${code}`);
exit(code);
