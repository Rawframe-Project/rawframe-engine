// Plays the page a web client runs in: hands a game's files to the client
// under `game/`, starts it with a configuration naming the game, drives it
// once a frame the way a browser's animation frames would, and stops it.
//
// usage: page.mjs <rawframe-web-client.wasm> <game directory>
import { argv, exit } from 'node:process';
import { WebTransportRelay } from '../../../tools/web_transport_relay.mjs';
import { PageClient, nextFrame } from './page_client.mjs';

const [wasmPath, gameDirectory] = argv.slice(2);
const client = await PageClient.load(wasmPath, new WebTransportRelay());
await client.holdDirectory(gameDirectory, 'game/');
const started = client.start(
    'kest.game = game/movers.game\nworld.tick_rate = 60\nhost.iteration_rate = 120\nhost.maximum_iterations = 30\n');
if (started !== 0) {
    console.log(`page: start refused, exit ${started}`);
    exit(1);
}
let frames = 0;
while (client.frame()) {
    frames += 1;
    await nextFrame();
}
const code = client.stop();
console.log(`page: ${frames} frames, exit ${code}`);
exit(code);
