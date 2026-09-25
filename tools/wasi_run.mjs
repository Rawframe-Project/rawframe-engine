// Runs a wasm32-wasi program under Node's WASI, the web build's test
// runner (cmake/wasm32-wasi.cmake): its arguments after the module's path,
// the host's environment, and the whole file system, as a native test has.
import { readFile } from 'node:fs/promises';
import { WASI } from 'node:wasi';
import { argv, env, exit } from 'node:process';

const wasi = new WASI({ args: argv.slice(2), env, preopens: { '/': '/' }, returnOnExit: true });
const module = await WebAssembly.compile(await readFile(argv[2]));
const instance = await WebAssembly.instantiate(module, { wasi_snapshot_preview1: wasi.wasiImport });
exit(wasi.start(instance));
