// A browser plays runners from a canvas: headless Chrome loads the
// page's own modules, the web client, and Maul Window's page side; the
// client plays (`play`), so a canvas is its window and the window's frames
// drive it; it joins a dedicated server over the browser's own
// WebTransport, trusting the server by its certificate's hash; a key held
// in the canvas runs the player, another makes it jump, a jump the player
// feels (though the page has no gamepad to feel it on) and hears
// (the page takes the client's sound and plays it), seen through the
// camera the page's own present system places, its HUD's score in
// words drawn, and a stop
// asked of the page ends the run in order. The game is as a web game ships:
// exported for the web, so cooked, packed into a signed
// Build, installed in the site's library, and named by a Composition, which
// the exported server reads from the site and the page holds, so the client
// reads its game, scenes, and textures from the Build; the site is served
// as it was written, and its server run as it was written, its certificate's
// fingerprint read by the page from beside itself.
// Native players share the server with the page: a bots process of
// two runners joins from the same Composition over QUIC, pinning the
// server's certificate, and plays beside the browser throughout. The game
// is drawn in the canvas through the browser's WebGPU with Maul RHI's page
// side: frames are shown, WebGPU reports no error, and a screenshot
// of the canvas shows what was drawn.
// Runners' HUD is read by a screen reader: the elements Maul UI's
// ARIA adapter keeps over the canvas wait behind its hidden button, which a
// screen reader's user presses; then Chrome's accessibility tree, what a
// screen reader reads, holds the HUD's frame, its picture, and its score.
// Stalls plays when named too, read and typed into by a screen reader alone:
// its HUD's purse and its sign's field, named by its label's words,
// are read once the button is pressed; the sign is typed into the page's own
// field, which hands each edit to the game as typing, and Enter gives it to
// the game, which sends it to the server as the owner's sign.
// The sample 3D game plays the same way when named: plaza, its
// walker walked by W and made to jump, its scene drawn in the canvas (its
// models seen through the player's camera and drawn), no sprites asked of
// it, and the sound the export asks of every game played as silence, since
// the plaza declares no mixer. Gale, the Keelstorm-class sample,
// plays as the plaza does, its ship sailed by W and a broadside
// fired by Q, its sea's program material drawn by WebGPU, its HUD's words
// drawn, and the broadside heard. Quarry, the VoxelTech-class sample,
// plays its land of seven thousand blocks, each a model drawn by
// WebGPU, its tool aimed by W and digging by Q; it predicts nothing, has
// no HUD, and is silent.
// Puppeteer comes from RAWFRAME_NODE_MODULES, and its browser from where
// Puppeteer looks (PUPPETEER_CACHE_DIR); without either the test is
// skipped (77).
//
// usage: browser_play.mjs <rawframe-export> <rawframe-cook> <rawframe-build> <rawframe-server> <rawframe-bots>
//                         <rawframe-web-client.wasm> <maul-window.mjs> <maul-rhi.mjs> <maul-ui.mjs> <repository>
//                         [game]
import { execFileSync, spawn } from 'node:child_process';
import { createSocket } from 'node:dgram';
import { existsSync } from 'node:fs';
import { appendFile, mkdtemp, readFile, rm, writeFile } from 'node:fs/promises';
import { createServer } from 'node:http';
import { createRequire } from 'node:module';
import { tmpdir } from 'node:os';
import { dirname, extname, join, normalize } from 'node:path';
import { argv, env } from 'node:process';
import { inflateSync } from 'node:zlib';
import { end } from './verdict.mjs';

const [exportPath, cookPath, buildPath, serverPath, botsPath, wasmPath, windowPath, devicePath, uiPath, repository,
       named] = argv.slice(2);
// runners, plaza, tycoon (Stalls), gale, quarry, or lighting.
const name = named ?? 'runners';
const plaza = name === 'plaza';
const stalls = name === 'tycoon';
const gale = name === 'gale';
const quarry = name === 'quarry';
const lighting = name === 'lighting';
// The games drawn in 3D.
const scened = plaza || gale || quarry || lighting;
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
                          '--maul-window', windowPath, '--maul-rhi', devicePath, '--maul-ui', uiPath, '--page',
                          join(repository, 'hosts/web_client/page')],
             { stdio: ['ignore', 'inherit', 'inherit'] });
const setup = JSON.parse(await readFile(join(site, 'play.json'), 'utf8'));
// The folder says how to host it: the server's command and its port.
const readme = await readFile(join(exported, 'README.txt'), 'utf8');
// Quarry's server ticks a land of seven thousand entities: behind on a
// loaded machine, it is not let degrade and refuse the page as unavailable,
// nor stop itself.
if (quarry) {
    await appendFile(join(exported, 'server', 'server.conf'), '\nworld.degraded_ms = 600000\nworld.overload_ms = 0\n');
}
// The export draws on whatever adapter the browser gives, a software one
// here; a 3D game's shadow map is small, and its view drawn at half
// scale, for the software rasterizer.
const drawsAsExported = /^render\.device = any$/m.test(setup.configuration);
setup.configuration += [
    ...(scened ? ['scene.shadow_side = 256', 'scene.shadow_filter = hardware', 'scene.render_scale_percent = 50'] : []),
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

// The pixels of a PNG screenshot that are not black, and those red or
// green over the others, as the Lighting Test's lamps make them:
// Chrome's are 8-bit RGB or RGBA, not interlaced.
const pixelCounts = (png) => {
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
    let red = 0;
    let green = 0;
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
            const [r, g, b] = [row[x], row[x + 1], row[x + 2]];
            lit += r || g || b ? 1 : 0;
            red += r > 60 && r > 1.3 * g && r > 1.3 * b ? 1 : 0;
            green += g > 80 && g > 1.1 * r && g > 1.1 * b ? 1 : 0;
        }
        previous = row;
    }
    return { lit, red, green };
};
// What Chrome's accessibility tree holds under an element, a node a line,
// each its role and its name, as a screen reader reads them: ignored nodes,
// unnamed generic ones, and text repeating its parent's name are passed
// through, as Maul UI's own test reads them.
const readUnder = async (tab, selector) => {
    const session = await tab.createCDPSession();
    try {
        const { root } = await session.send('DOM.getDocument', { depth: 0 });
        const { nodeId } = await session.send('DOM.querySelector', { nodeId: root.nodeId, selector });
        if (!nodeId) {
            return [];
        }
        const { node } = await session.send('DOM.describeNode', { nodeId });
        const { nodes } = await session.send('Accessibility.getFullAXTree');
        const byId = new Map(nodes.map((n) => [n.nodeId, n]));
        const lines = [];
        const visit = (ax, parentName, top) => {
            const role = ax.role?.value ?? '';
            const name = ax.name?.value ?? '';
            const through = ax.ignored || (role === 'generic' && name === '') || role === 'InlineTextBox' ||
                            (role === 'StaticText' && name === parentName);
            if (!through && !top) {
                lines.push(`${role} "${name}"`);
            }
            for (const id of ax.childIds ?? []) {
                const child = byId.get(id);
                if (child) {
                    visit(child, through ? parentName : name, false);
                }
            }
        };
        const top = nodes.find((n) => n.backendDOMNodeId === node.backendNodeId);
        if (top) {
            visit(top, '', true);
        }
        return lines;
    } finally {
        await session.detach();
    }
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
    // A 3D game's scene is drawn by a software rasterizer here: its canvas
    // is made a quarter of the size, which the client follows as a window
    // resized, so the page's drawing leaves its game time to play.
    if (scened) {
        await tab.evaluate(() => {
            const canvas = document.querySelector('canvas');
            canvas.style.width = '640px';
            canvas.style.height = '360px';
        });
    }
    await until(/"code":"bots_admitted"/, 30000);
    // Runners' HUD, or Stalls', read by a screen reader: nothing but the
    // hidden button until it is pressed, then the HUD.
    const host = 'canvas + [id$="-accessibility"]';
    let unread = [];
    let read = [];
    if (!scened) {
        // Stalls' HUD is whole once its purse and its sign's field are read.
        const wanted = stalls ? [/^button "[\d,]+ of 1,000 coins"$/, /^textbox "/] : [/ points"$/];
        const ready = () => wanted.every((pattern) => read.some((line) => pattern.test(line)));
        unread = await readUnder(tab, host);
        await tab.evaluate((selector) => document.querySelector(`${selector} button`)?.click(), host);
        for (let waited = 0; waited < 5000 && !ready(); waited += 100) {
            await sleep(100);
            read = await readUnder(tab, host);
        }
        console.log(`page: a screen reader read ${JSON.stringify(unread)}, then ${JSON.stringify(read)}`);
    }
    // Stalls' sign typed into the page's field, then given by Enter.
    if (stalls) {
        await tab.focus(`${host} input`);
        await tab.keyboard.type('ann', { delay: 50 });
        await tab.keyboard.press('Enter');
        await until(/"code":"ui_submitted"/, 10000);
        await sleep(1000);
    }
    // The canvas takes the focus, then D (W in the plaza) is held: the
    // player runs.
    // The plaza's page draws about eight frames a second on the software
    // rasterizer, and confirms fewer of its ticks for it: it plays five
    // seconds longer, so its confirmations clear the bar by more than chance
    // (two more once its fountain and traces cost it a frame in
    // twelve; and two more once a machine shared with other work
    // left it 97).
    // Its first frames make the rasterizer compile what each new way of
    // drawing needs (an effect's pipeline, a texture kind first sampled),
    // and a player starting during them confirms a fifth fewer ticks: it
    // starts once they are drawn.
    // Gale's ship fires a broadside in the run where the others jump.
    const forward = scened ? 'KeyW' : 'KeyD';
    const act = gale || quarry ? 'KeyQ' : 'Space';
    if (scened) {
        await sleep(1500);
    }
    if (!stalls) {
        await tab.click('canvas');
        await tab.keyboard.down(forward);
        await sleep(scened ? 6000 : 1000);
        await tab.keyboard.down(act);
        await sleep(200);
        await tab.keyboard.up(act);
        await sleep(800);
        await tab.keyboard.up(forward);
        await sleep(500);
    }
    // What the canvas shows before the stop.
    const pixels = pixelCounts(await (await tab.$('canvas')).screenshot({ type: 'png' }));
    const shown = pixels.lit;
    await tab.evaluate(() => window.rawframeStop());
    await until(/page: ended/, 30000);
    const ended = Number(/page: ended (\d+)/.exec(clientLog)[1]);
    const gpuErrors = await tab.evaluate(() => window.rawframeGpuErrors());
    const summary = /"bots":\d+,"admitted":\d+[^}]*/.exec(clientLog);
    console.log(summary ? `page: bots ${summary[0]}` : 'page: no bots summary');
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
    // drawn past its sheet's first cell was animated by the page,
    // through its own camera, which the page's present system placed.
    console.log(`page: the canvas drew ${drawn ? drawn[1] : 'no'} sprites, ${drawn ? drawn[2] : 'no'} animated, ` +
                `with ${drawn ? drawn[3] : 'no'} textures, ${viewed ? viewed[1] : 'no'} frames through the ` +
                `player's camera`);
    // The HUD's words, in the game's font, read from its string tables and
    // drawn from the glyph atlas.
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
    if (scened) {
        console.log(`page: the scene drew ${drawn3d ? drawn3d[1] : 'no'} models, ${seen3d ? seen3d[1] : 'no'} frames ` +
                    `through the player's camera`);
    }
    // Each condition by name, so a failure says which.
    const checks = {
        ended: ended === 0,
        admitted: field('admitted') === 1,
        handed: field('handed') === 1,
        notStalled: field('stalled') === 0,
        framesShown: drawing !== null && Number(drawing[1]) > 0,
        lit: shown > 1000,
        noGpuErrors: gpuErrors.length === 0,
        drawsAsExported,
        hostable: readme.includes('--config server/server.conf') && readme.includes(`UDP port ${port}`),
    };
    if (!stalls && !quarry && !lighting) {
        // Stalls, Quarry, and the Lighting Test predict nothing, so
        // confirm nothing.
        checks.confirmed = field('confirmed') > 100;
    }
    if (stalls) {
        const typing = /"code":"ui_summary"[^\n]*"focused":(\d+),"typed":(\d+),"submitted":(\d+)/.exec(clientLog);
        console.log(`page: the field took the keyboard ${typing?.[1] ?? 'no'} times, ${typing?.[2] ?? 'no'} edits, ` +
                    `${typing?.[3] ?? 'no'} given; the client sent ${field('commandsSent')} commands`);
        Object.assign(checks, {
            unread: unread.length === 1 && unread[0] === 'button "Enable accessibility"',
            read: read.some((line) => /^button "[\d,]+ of 1,000 coins"$/.test(line)) && read.includes('textbox "Write your sign"'),
            typed: typing !== null && Number(typing[2]) >= 3 && Number(typing[3]) === 1,
            sent: field('commandsSent') === 1,
        });
    } else if (scened) {
        Object.assign(checks, {
            seen3d: seen3d !== null && Number(seen3d[1]) > 0 && Number(seen3d[2]) > 0,
            drawn3d: drawn3d !== null && Number(drawn3d[1]) > 0,
        });
        if (plaza) {
            checks.silent = /"code":"game_silent"/.test(clientLog);
        } else if (gale) {
            Object.assign(checks, {
                worded: worded !== null && Number(worded[1]) > 0 && Number(worded[3]) === 0,
                heard: heard.frames > 48000 && heard.peak > 0.05,
            });
        } else if (quarry) {
            // The whole land, past the 4,096 a connection holds by
            // default.
            checks.landWhole = field('mirroredEntities') > 6000;
        } else {
            // The red lamp's light and the green's, through WebGPU.
            console.log(`page: the lamps lit ${pixels.red} red and ${pixels.green} green pixels`);
            checks.lamps = pixels.red > 200 && pixels.green > 50;
        }
    } else {
        Object.assign(checks, {
            felt: felt !== null && Number(felt[1]) > 0,
            sprites: drawn !== null && Number(drawn[1]) > 0 && Number(drawn[2]) > 0 && Number(drawn[3]) === 3,
            viewed: viewed !== null && Number(viewed[1]) > 0,
            heard: heard.frames > 48000 && heard.peak > 0.05,
            worded: worded !== null && Number(worded[1]) > 0 && Number(worded[3]) === 0,
            unread: unread.length === 1 && unread[0] === 'button "Enable accessibility"',
            read: read.some((line) => /^image "/.test(line)) && read.some((line) => /^StaticText "\d+ points"$/.test(line)),
        });
    }
    const unmet = Object.keys(checks).filter((name) => !checks[name]);
    if (unmet.length > 0) {
        console.log(`page: unmet: ${unmet.join(', ')}`);
    }
    verdict = unmet.length === 0 ? 0 : 1;
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
if (nativeField('admitted') !== 2 || (!stalls && !quarry && !lighting && nativeField('confirmed') <= 100) ||
    nativeField('stalled') !== 0 || most < 3) {
    console.log('page: unmet: the native bots or the players held at once');
    verdict = 1;
}
// On failure, the client's last lines, every summary it gave, and its first
// warnings and errors, wherever they fell.
if (verdict !== 0) {
    process.stdout.write(clientLog.slice(-6000));
    process.stdout.write((clientLog.match(/^.*"code":"[a-z_]*summary".*$/gm) ?? []).join('\n') + '\n');
    process.stdout.write((clientLog.match(/^.*"severity":"(warning|error)".*$/gm) ?? []).slice(0, 20).join('\n') + '\n');
    process.stdout.write(nativeLog.slice(-3000));
}
end(verdict);
