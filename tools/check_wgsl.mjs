// Compiles every engine shader's WGSL (modules/*/shaders/*.wgsl, the
// entries WebGPU runs, D286) in headless Chrome's WebGPU, as the browser
// test's page would, and fails on any error or warning: the native tests
// never run them, and a browser run may not reach every one. Puppeteer
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
const shaders = [];
for (const module of readdirSync(join(repository, 'modules'))) {
    const directory = join(repository, 'modules', module, 'shaders');
    let names = [];
    try {
        names = readdirSync(directory);
    } catch {
        continue;
    }
    for (const name of names.filter((name) => name.endsWith('.wgsl')).sort()) {
        shaders.push(join('modules', module, 'shaders', name));
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
        }, readFileSync(join(repository, shader), 'utf8'));
        console.log(`page: ${shader}: ${messages.length === 0 ? 'compiled' : messages.join('; ')}`);
        failed += messages.length === 0 ? 0 : 1;
    }
} finally {
    await browser.close();
    http.close();
}
console.log(`page: ${shaders.length} shaders, ${failed} with messages`);
end(shaders.length > 0 && failed === 0 ? 0 : 1);
