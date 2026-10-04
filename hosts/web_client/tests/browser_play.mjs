// A browser plays runners from a canvas (D250): headless Chrome loads the
// page's own modules, the web client, and Maul Window's page side; the
// client plays (`play`), so a canvas is its window and the window's frames
// drive it; it joins a dedicated server over the browser's own
// WebTransport, trusting the server by its certificate's hash; a key held
// in the canvas runs the player, another makes it jump, a jump the player
// feels (D251, though the page has no gamepad to feel it on) and hears
// (D259: the page takes the client's sound and plays it), seen through the
// camera the page's own present system places (D261), its HUD's score in
// words drawn (D398), and a stop
// asked of the page ends the run in order. The game is as a web game ships
// (D256, D397): exported for the web, so cooked, packed into a signed
// Build, installed in the site's library, and named by a Composition, which
// the exported server reads from the site and the page holds, so the client
// reads its game, scenes, and textures from the Build; the site is served
// as it was written, and its server run as it was written, its certificate's
// fingerprint read by the page from beside itself.
// Native players share the server with the page (D263): a bots process of
// two runners joins from the same Composition over QUIC, pinning the
// server's certificate, and plays beside the browser throughout. The game
// is drawn in the canvas through the browser's WebGPU with Maul RHI's page
// side (D282): frames are shown, WebGPU reports no error, and a screenshot
// of the canvas shows what was drawn.
// The sample 3D game plays the same way when named (D287): plaza, its
// walker walked by W and made to jump, its scene drawn in the canvas (its
// models seen through the player's camera and drawn), no sprites asked of
// it, and the sound the export asks of every game played as silence, since
// the plaza declares no mixer (D397).
// Puppeteer comes from RAWFRAME_NODE_MODULES, and its browser from where
// Puppeteer looks (PUPPETEER_CACHE_DIR); without either the test is
// skipped (77).
//
// usage: browser_play.mjs <rawframe-export> <rawframe-cook> <rawframe-build> <rawframe-server> <rawframe-bots>
//                         <rawframe-web-client.wasm> <maul-window.mjs> <maul-rhi.mjs> <repository> [game]
import { execFileSync, spawn } from 'node:child_process';
import { createSocket } from 'node:dgram';
import { existsSync } from 'node:fs';
import { mkdtemp, readFile, rm, writeFile } from 'node:fs/promises';
import { createServer } from 'node:http';
import { createRequire } from 'node:module';
import { tmpdir } from 'node:os';
import { dirname, extname, join, normalize } from 'node:path';
import { argv, env } from 'node:process';
import { inflateSync } from 'node:zlib';
import { end } from './verdict.mjs';

const [exportPath, cookPath, buildPath, serverPath, botsPath, wasmPath, windowPath, devicePath, repository, named] =
    argv.slice(2);
// runners, or plaza.
const name = named ?? 'runners';
const plaza = name === 'plaza';
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

/** A UDP port nothing holds right now. */
const port = await new Promise((resolve) => {
    const socket = createSocket('udp4');
    socket.bind(0, '127.0.0.1', () => {
        const { port: found } = socket.address();
        socket.close(() => resolve(found));
    });
});

// The game exported for the web: the site and its server.
const exported = join(work, 'exported');
const site = join(exported, 'web');
execFileSync(exportPath, [join(repository, 'games', name), exported, '--target', 'web', '--port', String(port),
                          '--cook', cookPath, '--build', buildPath, '--server', serverPath, '--web-client', wasmPath,
                          '--maul-window', windowPath, '--maul-rhi', devicePath, '--page',
                          join(repository, 'hosts/web_client/page')],
             { stdio: ['ignore', 'inherit', 'inherit'] });
const setup = JSON.parse(await readFile(join(site, 'play.json'), 'utf8'));
// Here a software adapter draws, which a player's machine is not asked to
// allow; the plaza's shadow map is small, and its view drawn at half scale
// (D373, D379), for the software rasterizer.
setup.configuration += [
    'render.device = any',
    ...(plaza ? ['scene.shadow_side = 256', 'scene.shadow_filter = hardware', 'scene.render_scale_percent = 50'] : []),
    '',
].join('\n');
await writeFile(join(site, 'play.json'), JSON.stringify(setup));
const gameResource = /kest\.game_resource = ([0-9a-f]+)/.exec(setup.configuration)[1];

const server = spawn(join(exported, 'server', 'rawframe-server'), ['--config', join(exported, 'server', 'server.conf')],
                     { stdio: ['ignore', 'pipe', 'inherit'], cwd: work });
process.on('exit', () => server.kill('SIGKILL'));
let serverLog = '';
server.stdout.on('data', (chunk) => {
    serverLog += chunk;
});
let fingerprint;
for (let tries = 0; tries < 500 && fingerprint === undefined; tries += 1) {
    await sleep(10);
    fingerprint = await readFile(join(site, 'fingerprint'), 'utf8').then((text) => text.trim(), () => undefined);
}
if (fingerprint === undefined) {
    console.log('page: the server wrote no fingerprint');
    end(1);
}

// Two native players from the same Composition, until asked to stop.
await writeFile(join(work, 'bots.conf'), [
    'host.iteration_rate = 120',
    `kest.game_resource = ${gameResource}`,
    `content.composition = ${join(site, setup.composition)}`,
    `content.library = ${join(site, 'library')}`,
    'kest.plan_only = true',
    `network.quic.pin_file = ${join(site, 'fingerprint')}`,
    'bots.count = 2',
    `bots.endpoint = 127.0.0.1:${port}`,
    '',
].join('\n'));
const natives = spawn(botsPath, ['--config', join(work, 'bots.conf')], { stdio: ['ignore', 'pipe', 'inherit'], cwd: repository });
process.on('exit', () => natives.kill('SIGKILL'));
let nativeLog = '';
natives.stdout.on('data', (chunk) => {
    nativeLog += chunk;
});

// The site, served as it was written, as any static file server would.
const types = { '.html': 'text/html', '.mjs': 'text/javascript', '.wasm': 'application/wasm',
                '.json': 'application/json' };
const http = createServer(async (request, response) => {
    const url = decodeURIComponent(new URL(request.url, 'http://page').pathname);
    const path = normalize(join(site, url === '/' ? 'index.html' : url));
    try {
        if (!path.startsWith(site + '/')) {
            throw new Error('outside the site');
        }
        const bytes = await readFile(path);
        response.writeHead(200, { 'Content-Type': types[extname(path)] ?? 'application/octet-stream' });
        response.end(bytes);
    } catch {
        response.writeHead(404);
        response.end();
    }
});
await new Promise((resolve) => http.listen(0, '127.0.0.1', resolve));

// WebGPU in headless Chrome: its Vulkan path over the SwiftShader Chrome
// ships, which presents to a canvas, as Maul RHI's web runner does.
const swiftShader = join(dirname(browserPath), 'vk_swiftshader_icd.json');
const browser = await puppeteer.launch({
    args: ['--no-sandbox', '--autoplay-policy=no-user-gesture-required', '--enable-unsafe-webgpu',
           '--enable-features=Vulkan', '--use-vulkan=swiftshader', '--use-angle=vulkan'],
    env: Object.assign({}, env, { VK_ICD_FILENAMES: swiftShader, VK_DRIVER_FILES: swiftShader }),
});

// The pixels of a PNG screenshot that are not black: Chrome's are 8-bit
// RGB or RGBA, not interlaced.
const litPixels = (png) => {
    const width = png.readUInt32BE(16);
    const height = png.readUInt32BE(20);
    const channels = png[25] === 6 ? 4 : 3;
    const parts = [];
    for (let at = 8; at < png.length;) {
        const length = png.readUInt32BE(at);
        if (png.toString('latin1', at + 4, at + 8) === 'IDAT') {
            parts.push(png.subarray(at + 8, at + 8 + length));
        }
        at += 12 + length;
    }
    const raw = inflateSync(Buffer.concat(parts));
    const stride = width * channels;
    let previous = Buffer.alloc(stride);
    let lit = 0;
    for (let y = 0; y < height; y++) {
        const filter = raw[y * (stride + 1)];
        const row = Buffer.from(raw.subarray(y * (stride + 1) + 1, (y + 1) * (stride + 1)));
        for (let x = 0; x < stride; x++) {
            const left = x >= channels ? row[x - channels] : 0;
            const up = previous[x];
            const corner = x >= channels ? previous[x - channels] : 0;
            const guess = left + up - corner;
            const nearest = Math.abs(guess - left) <= Math.abs(guess - up) && Math.abs(guess - left) <= Math.abs(guess - corner)
                ? left
                : Math.abs(guess - up) <= Math.abs(guess - corner) ? up : corner;
            row[x] = (row[x] + [0, left, up, (left + up) >> 1, nearest][filter]) & 255;
        }
        for (let x = 0; x < stride; x += channels) {
            lit += row[x] || row[x + 1] || row[x + 2] ? 1 : 0;
        }
        previous = row;
    }
    return lit;
};
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
    // The plaza's scene is drawn by a software rasterizer here: its canvas
    // is made a quarter of the size, which the client follows as a window
    // resized (D281), so the page's drawing leaves its game time to play.
    if (plaza) {
        await tab.evaluate(() => {
            const canvas = document.querySelector('canvas');
            canvas.style.width = '640px';
            canvas.style.height = '360px';
        });
    }
    await until(/"code":"bots_admitted"/, 30000);
    // The canvas takes the focus, then D (W in the plaza) is held: the
    // player runs.
    // The plaza's page draws about eight frames a second on the software
    // rasterizer, and confirms fewer of its ticks for it: it plays five
    // seconds longer, so its confirmations clear the bar by more than chance
    // (D333; two more once its fountain and traces cost it a frame in
    // twelve, D354; and two more once a machine shared with other work
    // left it 97, D355).
    // Its first frames make the rasterizer compile what each new way of
    // drawing needs (an effect's pipeline, a texture kind first sampled),
    // and a player starting during them confirms a fifth fewer ticks: it
    // starts once they are drawn (D341).
    const forward = plaza ? 'KeyW' : 'KeyD';
    if (plaza) {
        await sleep(1500);
    }
    await tab.click('canvas');
    await tab.keyboard.down(forward);
    await sleep(plaza ? 6000 : 1000);
    await tab.keyboard.down('Space');
    await sleep(200);
    await tab.keyboard.up('Space');
    await sleep(800);
    await tab.keyboard.up(forward);
    await sleep(500);
    // What the canvas shows before the stop.
    const shown = litPixels(await (await tab.$('canvas')).screenshot({ type: 'png' }));
    await tab.evaluate(() => window.rawframeStop());
    await until(/page: ended/, 30000);
    const ended = Number(/page: ended (\d+)/.exec(clientLog)[1]);
    const gpuErrors = await tab.evaluate(() => window.rawframeGpuErrors());
    const summary = /"bots":\d+,"admitted":\d+[^}]*/.exec(clientLog);
    console.log(summary ? summary[0] : 'page: no bots summary');
    const field = (name) => Number(new RegExp(`"${name}":(\\d+)`).exec(summary?.[0] ?? '')?.[1] ?? -1);
    // What the page took of the client's sound and played.
    const heard = await tab.evaluate(() => window.rawframeSound());
    console.log(`page: the page played ${heard.frames} frames of sound, peak ${heard.peak.toFixed(3)} (${heard.state})`);
    const felt = /"code":"felt_summary"[^\n]*"effectsFelt":(\d+)/.exec(clientLog);
    console.log(`page: the player felt ${felt ? felt[1] : 'no'} effects`);
    const drawn = /"code":"canvas_summary"[^\n]*"spritesDrawn":(\d+),"spritesAnimated":(\d+)[^\n]*"unknownTextures":0,[^\n]*"texturesReady":(\d+)/.exec(
        clientLog);
    const viewed = /"code":"canvas_summary"[^\n]*"framesViewed":(\d+)/.exec(clientLog);
    const drawing = /"code":"frame_summary"[^\n]*"framesShown":(\d+)/.exec(clientLog);
    console.log(`page: the device showed ${drawing ? drawing[1] : 'no'} frames in the canvas, whose screenshot ` +
                `lit ${shown} pixels; WebGPU reported ${gpuErrors.length} errors`);
    for (const error of gpuErrors) {
        console.log(`page: WebGPU: ${error}`);
    }
    // The server is dedicated and plays no presentation animator: a runner
    // drawn past its sheet's first cell was animated by the page (D258),
    // through its own camera, which the page's present system placed (D261).
    console.log(`page: the canvas drew ${drawn ? drawn[1] : 'no'} sprites, ${drawn ? drawn[2] : 'no'} animated, ` +
                `with ${drawn ? drawn[3] : 'no'} textures, ${viewed ? viewed[1] : 'no'} frames through the ` +
                `player's camera`);
    // The HUD's words, in the game's font, read from its string tables and
    // drawn from the glyph atlas (D398).
    const worded = /"code":"ui_drawing_summary"[^\n]*"glyphRuns":(\d+),"glyphs":(\d+),"glyphRunsWaiting":(\d+)/.exec(
        clientLog);
    if (!plaza) {
        console.log(`page: the UI drew ${worded ? worded[1] : 'no'} runs of ${worded ? worded[2] : 'no'} glyphs, ` +
                    `${worded ? worded[3] : 'no'} waiting`);
    }
    // The plaza's walker, seen in 3D through the camera its present system
    // placed, its models drawn on the device.
    const seen3d = /"code":"scene_summary"[^\n]*"framesViewed":(\d+)[^\n]*"modelsDrawn":(\d+)/.exec(clientLog);
    const drawn3d = /"code":"scene_drawing_summary"[^\n]*"models":(\d+),[^\n]*"modelsLeftOut":0,/.exec(clientLog);
    if (plaza) {
        console.log(`page: the scene drew ${drawn3d ? drawn3d[1] : 'no'} models, ${seen3d ? seen3d[1] : 'no'} frames ` +
                    `through the player's camera`);
    }
    const played = ended === 0 && field('admitted') === 1 && field('handed') === 1 && field('stalled') === 0 &&
                   field('confirmed') > 100 && drawing !== null && Number(drawing[1]) > 0 && shown > 1000 &&
                   gpuErrors.length === 0;
    const runnersPlayed = felt !== null && Number(felt[1]) > 0 && drawn !== null && Number(drawn[1]) > 0 &&
                          Number(drawn[2]) > 0 && Number(drawn[3]) === 3 && viewed !== null && Number(viewed[1]) > 0 &&
                          heard.frames > 48000 && heard.peak > 0.05 && worded !== null && Number(worded[1]) > 0 &&
                          Number(worded[3]) === 0;
    const plazaPlayed = seen3d !== null && Number(seen3d[1]) > 0 && Number(seen3d[2]) > 0 && drawn3d !== null &&
                        Number(drawn3d[1]) > 0 && /"code":"game_silent"/.test(clientLog);
    verdict = played && (plaza ? plazaPlayed : runnersPlayed) ? 0 : 1;
} catch (error) {
    console.log(`page: ${error.message}`);
} finally {
    await browser.close();
    http.close();
}
natives.kill('SIGTERM');
await new Promise((resolve) => natives.on('exit', resolve));
server.kill('SIGTERM');
await new Promise((resolve) => server.on('exit', resolve));
await rm(work, { recursive: true, force: true });
// The server's side: what it sent of the player moving, and what it took.
const sent = /"recordsSent":(\d+)/.exec(serverLog);
const consumed = /"inputsConsumed":(\d+)/.exec(serverLog);
console.log(`page: the server sent ${sent ? sent[1] : 'no'} records and consumed ${consumed ? consumed[1] : 'no'} inputs`);
// The natives' side, and the players the server held at once: the page's
// and the natives' together.
const native = /"bots":\d+,"admitted":\d+[^}]*/.exec(nativeLog);
const nativeField = (name) => Number(new RegExp(`"${name}":(\\d+)`).exec(native?.[0] ?? '')?.[1] ?? -1);
const most = Number(/"mostConnections":(\d+)/.exec(serverLog)?.[1] ?? -1);
console.log(`page: native bots admitted ${nativeField('admitted')}, confirmed ${nativeField('confirmed')}; ` +
            `the server held ${most} players at once`);
if (nativeField('admitted') !== 2 || nativeField('confirmed') <= 100 || nativeField('stalled') !== 0 || most < 3) {
    verdict = 1;
}
if (verdict !== 0) {
    process.stdout.write(clientLog.slice(-6000));
    process.stdout.write(nativeLog.slice(-3000));
}
end(verdict);
