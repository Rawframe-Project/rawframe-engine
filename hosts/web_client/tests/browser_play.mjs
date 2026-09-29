// A browser plays runners from a canvas (D250): headless Chrome loads the
// page's own modules, the web client, and Maul Window's page side; the
// client plays (`play`), so a canvas is its window and the window's frames
// drive it; it joins a dedicated server over the browser's own
// WebTransport, trusting the server by its certificate's hash; a key held
// in the canvas runs the player, and a stop asked of the page ends the run
// in order. Puppeteer comes from RAWFRAME_NODE_MODULES, and its browser
// from where Puppeteer looks (PUPPETEER_CACHE_DIR); without either the test
// is skipped (77).
//
// usage: browser_play.mjs <rawframe-server> <rawframe-web-client.wasm> <maul-window.mjs> <repository>
import { spawn } from 'node:child_process';
import { createSocket } from 'node:dgram';
import { existsSync } from 'node:fs';
import { mkdtemp, readFile, readdir, rm, writeFile } from 'node:fs/promises';
import { createServer } from 'node:http';
import { createRequire } from 'node:module';
import { tmpdir } from 'node:os';
import { extname, join, normalize, relative } from 'node:path';
import { argv, env } from 'node:process';
import { end } from './verdict.mjs';

const [serverPath, wasmPath, windowPath, repository] = argv.slice(2);
let puppeteer;
try {
    puppeteer = createRequire(join(env.RAWFRAME_NODE_MODULES ?? '', 'x.js'))('puppeteer');
} catch {
    console.log('page: puppeteer not found through RAWFRAME_NODE_MODULES: skipped');
    process.exit(77);
}
const browserPath = await puppeteer.executablePath();
if (!existsSync(browserPath)) {
    console.log(`page: puppeteer has no browser at ${browserPath}: skipped`);
    process.exit(77);
}
const sleep = (milliseconds) => new Promise((resolve) => setTimeout(resolve, milliseconds));
const work = await mkdtemp(join(tmpdir(), 'rawframe-play-'));
const game = join(repository, 'games/runners');

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
    `kest.game = ${join(game, 'runners.game')}`,
    'network.quic.self_signed = true',
    'network.quic.webtransport = true',
    `network.quic.fingerprint_file = ${join(work, 'fingerprint')}`,
    `replication.endpoint = 127.0.0.1:${port}`,
    '',
].join('\n'));
const server = spawn(serverPath, ['--config', join(work, 'server.conf')], { stdio: ['ignore', 'pipe', 'inherit'], cwd: repository });
process.on('exit', () => server.kill('SIGKILL'));
let serverLog = '';
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
    end(1);
}

// What the page fetches: its modules, the client, the window's page side,
// and the game, which it hands the client under game/.
const files = [];
async function list(directory) {
    for (const entry of await readdir(directory, { withFileTypes: true })) {
        const path = join(directory, entry.name);
        if (entry.isDirectory()) {
            await list(path);
        } else {
            files.push(relative(game, path));
        }
    }
}
await list(game);
const setup = {
    fingerprint,
    files,
    configuration: [
        'host.iteration_rate = 120',
        'kest.game = game/runners.game',
        'kest.plan_only = true',
        'bots.player = true',
        `bots.endpoint = https://127.0.0.1:${port}/rawframe`,
        '',
    ].join('\n'),
};
const page = `<!doctype html><html><head><meta charset="utf-8"></head><body>
<script type="module">
import { WebClient } from '/page/client.mjs';
import { PageTransport } from '/page/transport.mjs';
import { maulWindowImports } from '/maul-window.mjs';
const setup = await (await fetch('/setup.json')).json();
const transport = new PageTransport({ certificateHashes: [setup.fingerprint] });
const client = await WebClient.load(await (await fetch('/client.wasm')).arrayBuffer(),
                                    { transport, log: (line) => console.log(line), windowImports: maulWindowImports });
for (const path of setup.files) {
    if (!client.hold('game/' + path, new Uint8Array(await (await fetch('/game/' + path)).arrayBuffer()))) {
        throw new Error('a file was refused: ' + path);
    }
}
console.log('page: play ' + client.play(setup.configuration));
window.rawframeStop = () => client.requestStop();
const watch = () => {
    const code = client.ended();
    if (code === null) {
        requestAnimationFrame(watch);
    } else {
        console.log('page: ended ' + code);
    }
};
requestAnimationFrame(watch);
</script></body></html>`;
const types = { '.mjs': 'text/javascript', '.wasm': 'application/wasm', '.json': 'application/json' };
const http = createServer(async (request, response) => {
    const url = decodeURIComponent(request.url);
    const serve = async (path, type) => {
        response.writeHead(200, { 'Content-Type': type ?? types[extname(path)] ?? 'application/octet-stream' });
        response.end(await readFile(path));
    };
    try {
        if (url === '/') {
            response.writeHead(200, { 'Content-Type': 'text/html' });
            response.end(page);
        } else if (url === '/setup.json') {
            response.writeHead(200, { 'Content-Type': 'application/json' });
            response.end(JSON.stringify(setup));
        } else if (url === '/client.wasm') {
            await serve(wasmPath);
        } else if (url === '/maul-window.mjs') {
            await serve(windowPath);
        } else if (url.startsWith('/page/')) {
            await serve(join(repository, 'hosts/web_client/page', normalize(url.slice(6))));
        } else if (url.startsWith('/game/')) {
            await serve(join(game, normalize(url.slice(6))));
        } else {
            response.writeHead(404);
            response.end();
        }
    } catch {
        response.writeHead(404);
        response.end();
    }
});
await new Promise((resolve) => http.listen(0, '127.0.0.1', resolve));

const browser = await puppeteer.launch({ args: ['--no-sandbox'] });
let clientLog = '';
let verdict = 1;
try {
    const tab = await browser.newPage();
    const seen = (pattern) => pattern.test(clientLog);
    const until = async (pattern, milliseconds) => {
        for (let waited = 0; !seen(pattern); waited += 20) {
            if (waited > milliseconds) {
                throw new Error(`no ${pattern} in ${milliseconds} ms`);
            }
            await sleep(20);
        }
    };
    tab.on('console', (message) => {
        clientLog += message.text() + '\n';
    });
    tab.on('pageerror', (error) => {
        clientLog += `page error: ${error.message}\n`;
    });
    await tab.goto(`http://127.0.0.1:${http.address().port}/`);
    await until(/page: play 0/, 30000);
    await until(/"code":"bots_admitted"/, 30000);
    // The canvas takes the focus, then D is held: the runner runs.
    await tab.click('canvas');
    await tab.keyboard.down('KeyD');
    await sleep(2000);
    await tab.keyboard.up('KeyD');
    await sleep(500);
    await tab.evaluate(() => window.rawframeStop());
    await until(/page: ended/, 30000);
    const ended = Number(/page: ended (\d+)/.exec(clientLog)[1]);
    const summary = /"bots":\d+,"admitted":\d+[^}]*/.exec(clientLog);
    console.log(summary ? summary[0] : 'page: no bots summary');
    const field = (name) => Number(new RegExp(`"${name}":(\\d+)`).exec(summary?.[0] ?? '')?.[1] ?? -1);
    verdict = ended === 0 && field('admitted') === 1 && field('handed') === 1 && field('stalled') === 0 &&
                      field('confirmed') > 100
                  ? 0
                  : 1;
} catch (error) {
    console.log(`page: ${error.message}`);
} finally {
    await browser.close();
    http.close();
}
server.kill('SIGTERM');
await new Promise((resolve) => server.on('exit', resolve));
await rm(work, { recursive: true, force: true });
// The server's side: what it sent of the player moving, and what it took.
const sent = /"recordsSent":(\d+)/.exec(serverLog);
const consumed = /"inputsConsumed":(\d+)/.exec(serverLog);
console.log(`page: the server sent ${sent ? sent[1] : 'no'} records and consumed ${consumed ? consumed[1] : 'no'} inputs`);
if (verdict !== 0) {
    process.stdout.write(clientLog.slice(-6000));
}
end(verdict);
