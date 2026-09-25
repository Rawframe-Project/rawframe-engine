// The browser's path end to end, without a browser (D173): the dedicated
// server accepts browsers over WebTransport with a fresh 13-day identity;
// the page's own modules (page/) load the web client with the browser's
// WASI and its transport over WebTransport, trust the server by its
// certificate's hash, hand over the arena sample, and drive the client's
// bots against the server until they stop. WebTransport is the stand-in
// over aioquic (webtransport_node.mjs); everything else is what a browser
// runs.
//
// usage: browser_page.mjs <rawframe-server> <rawframe-web-client.wasm> <repository>
import { spawn } from 'node:child_process';
import { webcrypto } from 'node:crypto';
import { mkdtemp, readFile, readdir, rm, writeFile } from 'node:fs/promises';
import { createSocket } from 'node:dgram';
import { tmpdir } from 'node:os';
import { join, relative } from 'node:path';
import { argv, exit } from 'node:process';
import { WebClient } from '../page/client.mjs';
import { PageTransport } from '../page/transport.mjs';
import { WebTransport } from './webtransport_node.mjs';

// What a browser has that Node 18 keeps elsewhere.
globalThis.crypto ??= webcrypto;

const [serverPath, wasmPath, repository] = argv.slice(2);
const work = await mkdtemp(join(tmpdir(), 'rawframe-page-'));
const sleep = (milliseconds) => new Promise((resolve) => setTimeout(resolve, milliseconds));

/** A UDP port nothing holds right now. */
const port = await new Promise((resolve) => {
    const socket = createSocket('udp4');
    socket.bind(0, '127.0.0.1', () => {
        const { port: found } = socket.address();
        socket.close(() => resolve(found));
    });
});

await writeFile(join(work, 'server.conf'), [
    'host.iteration_rate = 120',
    'world.tick_rate = 60',
    'kest.game = games/arena/arena.game',
    'network.quic.self_signed = true',
    'network.quic.webtransport = true',
    `network.quic.fingerprint_file = ${join(work, 'fingerprint')}`,
    `replication.endpoint = 127.0.0.1:${port}`,
    '',
].join('\n'));
const server = spawn(serverPath, ['--config', join(work, 'server.conf')], { stdio: ['ignore', 'pipe', 'inherit'], cwd: repository });
process.on('exit', () => server.kill('SIGKILL'));
let serverLog = '';
// However it goes, the server does not outlive the page.
const watchdog = setTimeout(() => {
    console.log('page: out of time');
    console.log(clientLog.slice(-4000));
    server.kill('SIGKILL');
    exit(1);
}, 60000);
let clientLog = '';
server.stdout.on('data', (chunk) => {
    serverLog += chunk;
});

let fingerprint;
for (let tries = 0; tries < 500 && fingerprint === undefined; tries += 1) {
    await sleep(10);
    fingerprint = await readFile(join(work, 'fingerprint'), 'utf8').then((text) => text.trim(), () => undefined);
}
if (fingerprint === undefined) {
    console.log('page: the server wrote no fingerprint');
    server.kill();
    exit(1);
}

const transport = new PageTransport({ certificateHashes: [fingerprint], WebTransport });
const client = await WebClient.load(await readFile(wasmPath), {
    transport,
    log: (line) => {
        clientLog += line + '\n';
    },
});
async function holdUnder(directory, prefix) {
    for (const entry of await readdir(directory, { withFileTypes: true })) {
        const path = join(directory, entry.name);
        if (entry.isDirectory()) {
            await holdUnder(path, prefix);
        } else if (!client.hold(prefix + relative(join(repository, 'games/arena'), path), await readFile(path))) {
            throw new Error(`a file was refused: ${path}`);
        }
    }
}
await holdUnder(join(repository, 'games/arena'), 'game/');
const started = client.start([
    'host.iteration_rate = 120',
    'host.maximum_iterations = 360',
    'kest.game = game/arena.game',
    'kest.plan_only = true',
    'bots.count = 2',
    `bots.endpoint = https://127.0.0.1:${port}/rawframe`,
    '',
].join('\n'));
if (started !== 0) {
    console.log(`page: the client did not start (${started})`);
    process.stdout.write(clientLog);
    server.kill();
    exit(1);
}
while (client.frame()) {
    await sleep(16);
}
const code = client.stop();
clearTimeout(watchdog);
server.kill('SIGTERM');
await new Promise((resolve) => server.on('exit', resolve));
await rm(work, { recursive: true, force: true });
const summary = clientLog.match(/"bots":\d+,"admitted":\d+[^}]*/);
console.log(summary ? summary[0] : 'page: no bots summary');
// The server's side of the same play: the inputs it took from the page.
const served = serverLog.match(/"inputsConsumed":(\d+)/);
console.log(`page: client exit ${code}, the server consumed ${served ? served[1] : 'no'} inputs`);
exit(code === 0 && summary !== null && /"admitted":2/.test(summary[0]) && served && Number(served[1]) > 0 ? 0 : 1);
