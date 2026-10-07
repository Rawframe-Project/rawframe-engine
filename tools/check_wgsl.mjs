// Compiles every engine shader's WGSL (the entries WebGPU runs, D286),
// as each container holds it, compiled from Slang (D475), in headless
// Chrome's WebGPU, as the browser test's page would, and fails on any
// error or warning: the native tests never run them, a browser run may not
// reach every one, and WebGPU refuses what Vulkan takes (a sample in
// control flow that is not uniform). Puppeteer
// comes from RAWFRAME_NODE_MODULES and its browser from PUPPETEER_CACHE_DIR;
// without either it is skipped (77).
//
// usage: check_wgsl.mjs <repository>
import { readFileSync, readdirSync } from 'node:fs';
import { createServer } from 'node:http';
import { createRequire } from 'node:module';
import { dirname, join } from 'node:path';
import { argv, env } from 'node:process';
import { end } from '../hosts/web_client/tests/verdict.mjs';

const repository = argv[2];
let puppeteer;
try {
    puppeteer = createRequire(join(env.RAWFRAME_NODE_MODULES ?? '', 'x.js'))('puppeteer');
} catch {
    console.log('page: puppeteer not found through RAWFRAME_NODE_MODULES: skipped');
    process.exit(77);
}
const browserPath = await puppeteer.executablePath();
// A container (Maul RHI's mrhi_container.py): 64 bytes of head, then a
// table of sections, each its kind, an offset, and a length, 24 bytes;
// the WGSL is the section of kind nine.
function wgslOf(path) {
    const bytes = readFileSync(path);
    const count = bytes.readUInt32LE(48);
    for (let at = 0; at < count; ++at) {
        const record = 64 + at * 24;
        if (bytes.readUInt32LE(record) === 9) {
            const offset = Number(bytes.readBigUInt64LE(record + 8));
            const length = Number(bytes.readBigUInt64LE(record + 16));
            return bytes.subarray(offset, offset + length).toString('utf8');
        }
    }
    return null;
}

const shaders = [];
for (const module of readdirSync(join(repository, 'modules'))) {
    const directory = join(repository, 'modules', module, 'src', 'generated');
    let names = [];
    try {
        names = readdirSync(directory);
    } catch {
        continue;
    }
    // The Vulkan and WebGPU container of each; the Metal and Direct3D 12
    // ones hold the same WGSL.
    for (const name of names.filter((name) => /^[a-z]+\.mrsc$/.test(name)).sort()) {
        shaders.push(join('modules', module, 'src', 'generated', name));
    }
}
// WebGPU only in a secure context: a page of its own on the loopback.
const http = createServer((request, response) => {
    response.writeHead(200, { 'content-type': 'text/html' });
    response.end('<!doctype html><title>wgsl</title>');
});
await new Promise((resolve) => http.listen(0, '127.0.0.1', resolve));
const swiftShader = join(dirname(browserPath), 'vk_swiftshader_icd.json');
const browser = await puppeteer.launch({
    args: ['--no-sandbox', '--enable-unsafe-webgpu', '--enable-features=Vulkan', '--use-vulkan=swiftshader',
           '--use-angle=vulkan'],
    env: Object.assign({}, env, { VK_ICD_FILENAMES: swiftShader, VK_DRIVER_FILES: swiftShader }),
});
let failed = 0;
try {
    const tab = await browser.newPage();
    await tab.goto(`http://127.0.0.1:${http.address().port}/`);
    for (const shader of shaders) {
        const messages = await tab.evaluate(async (code) => {
            const adapter = await navigator.gpu.requestAdapter();
            const device = await adapter.requestDevice();
            const info = await device.createShaderModule({ code }).getCompilationInfo();
            return info.messages.map((m) => `${m.type} at ${m.lineNum}:${m.linePos}: ${m.message}`);
        }, wgslOf(join(repository, shader)) ?? '');
        console.log(`page: ${shader}: ${messages.length === 0 ? 'compiled' : messages.join('; ')}`);
        failed += messages.length === 0 ? 0 : 1;
    }
} finally {
    await browser.close();
    http.close();
}
console.log(`page: ${shaders.length} shaders, ${failed} with messages`);
end(shaders.length > 0 && failed === 0 ? 0 : 1);
