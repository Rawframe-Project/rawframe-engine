// How a test page ends: its verdict on a line of its own, then Node's exit.
// Node 24 has been seen to hang in its own teardown after a page had passed
// (D238), so tools/node_page.sh reads the verdict rather than waiting on
// the exit.
import { exit } from 'node:process';

/** Ends the page: 0 passes, anything else fails. */
export function end(code) {
    console.log(code === 0 ? 'page: passed' : 'page: failed');
    exit(code);
}
