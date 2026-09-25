// Two web clients on one page and one relay standing in for WebTransport:
// the first serves a game at `arena`, the second's bots join it over the web
// transport, play, and stop. A browser cannot listen; the relay lets one
// page stand in for the server so the client's whole path runs here.
//
// usage: replicated.mjs <rawframe-web-client.wasm> <game directory>
import { argv, exit } from 'node:process';
import { WebTransportRelay } from '../../../tools/web_transport_relay.mjs';
import { PageClient, nextFrame } from './page_client.mjs';

const [wasmPath, gameDirectory] = argv.slice(2);
const relay = new WebTransportRelay();
const server = await PageClient.load(wasmPath, relay);
const player = await PageClient.load(wasmPath, relay);
await server.holdDirectory(gameDirectory, 'game/');
await player.holdDirectory(gameDirectory, 'game/');

const common = 'host.iteration_rate = 120\nworld.tick_rate = 60\nkest.game = game/arena.game\n';
if (server.start(common + 'replication.endpoint = arena\n') !== 0) {
    console.log('page: the server did not start');
    exit(1);
}
if (player.start(common + 'kest.plan_only = true\nbots.count = 2\nbots.endpoint = arena\nhost.maximum_iterations = 240\n') !== 0) {
    console.log('page: the player did not start');
    exit(1);
}
// Both in each frame, the server first, until the player's run ends.
while (true) {
    const serving = server.frame();
    if (!player.frame() || !serving) {
        break;
    }
    await nextFrame();
}
const played = player.stop();
const served = server.stop();
console.log(played === 0 && served === 0 ? 'page: both exit 0' : `page: exit ${played} and ${served}`);
exit(played === 0 && served === 0 ? 0 : 1);
