// How long a web client takes to become a running game (D175), as a page
// would see it under V8: compiling the module, instantiating it, and
// starting the arena sample from files handed over, to its first
// iteration. Then play: two clients on one relay standing in for
// WebTransport, one serving runners to sixteen predicting bots in the
// other, for two seconds; the server World's tick durations are what its
// summary gives. The median of five runs, in milliseconds, one JSON line.
//
// usage: web_load.mjs <rawframe-web-client.wasm> <repository>
import { webcrypto } from 'node:crypto';
import { readFile, readdir } from 'node:fs/promises';
import { join, relative } from 'node:path';
import { argv } from 'node:process';
import { WebClient } from '../hosts/web_client/page/client.mjs';
import { PageTransport } from '../hosts/web_client/page/transport.mjs';
import { WebTransportRelay } from './web_transport_relay.mjs';

globalThis.crypto ??= webcrypto;
const [wasmPath, repository] = argv.slice(2);
const gameOf = async (name) => {
    const directory = join(repository, 'games', name);
    return Promise.all((await filesUnder(directory)).map(async (path) => [relative(directory, path), await readFile(path)]));
};

async function filesUnder(directory) {
    const found = [];
    for (const entry of await readdir(directory, { withFileTypes: true })) {
        const path = join(directory, entry.name);
        found.push(...(entry.isDirectory() ? await filesUnder(path) : [path]));
    }
    return found;
}
const arena = await gameOf('arena');
const runners = await gameOf('runners');
const bytes = await readFile(wasmPath);

async function once() {
    const began = performance.now();
    const module = await WebAssembly.compile(bytes);
    const compiled = performance.now();
    let summary;
    let loaded = false;
    const log = (line) => {
        loaded = loaded || line.includes('"code":"game_loaded"');
        // The last World summary is the server's.
        if (line.includes('"code":"tick_summary"')) {
            summary = JSON.parse(line).fields;
        }
    };
    const client = await WebClient.load(module, { transport: new PageTransport(), log });
    const instantiated = performance.now();
    for (const [path, content] of arena) {
        client.hold('game/' + path, content);
    }
    const started = client.start('kest.game = game/arena.game\nworld.tick_rate = 60\nhost.iteration_rate = 1000\nhost.maximum_iterations = 1000\n');
    if (started !== 0) {
        throw new Error(`the client did not start (${started})`);
    }
    const running = performance.now();
    client.stop();
    if (!loaded) {
        throw new Error('the game did not load');
    }

    // Two clients on one relay: a server and sixteen bots.
    const relay = new WebTransportRelay();
    const server = await WebClient.load(module, { transport: relay, log });
    const players = await WebClient.load(module, { transport: relay, log: () => {} });
    for (const [path, content] of runners) {
        server.hold('game/' + path, content);
        players.hold('game/' + path, content);
    }
    const common = 'kest.game = game/runners.game\nworld.tick_rate = 60\nhost.iteration_rate = 120\nhost.maximum_iterations = 240\n';
    if (server.start(common + 'replication.endpoint = runners\n') !== 0 ||
        players.start(common + 'kest.plan_only = true\nbots.count = 16\nbots.endpoint = runners\n') !== 0) {
        throw new Error('the play did not start');
    }
    for (;;) {
        const serving = server.frame();
        const playing = players.frame();
        if (!serving || !playing) {
            break;
        }
        await new Promise((resolve) => setImmediate(resolve));
    }
    players.stop();
    server.stop();
    if (summary === undefined) {
        throw new Error('the server gave no tick summary');
    }
    return {
        compile: compiled - began,
        instantiate: instantiated - compiled,
        start: running - instantiated,
        tickP50: summary.p50 / 1000,
        tickP99: summary.p99 / 1000,
    };
}

const runs = [];
for (let run = 0; run < 5; run += 1) {
    runs.push(await once());
}
const median = (key) => runs.map((each) => each[key]).sort((a, b) => a - b)[2];
const result = Object.fromEntries(['compile', 'instantiate', 'start', 'tickP50', 'tickP99'].map((key) => [key, Number(median(key).toFixed(3))]));
console.log(JSON.stringify(result));
