// The WASI a web client needs, in a browser (D173): no files, no
// arguments, no environment; the clock, randomness, and standard output
// and error as lines handed to `log`. Everything else answers that it is
// not there.
const ERRNO_SUCCESS = 0;
const ERRNO_BADF = 8;
const ERRNO_NOENT = 44;
const ERRNO_NOSYS = 52;

export class ProcessExit extends Error {
    constructor(code) {
        super(`the client exited with ${code}`);
        this.code = code;
    }
}

/** The `wasi_snapshot_preview1` imports over `memory()`. */
export function browserWasi(memory, log) {
    const view = () => new DataView(memory().buffer);
    const decoder = new TextDecoder();
    const partial = ['', '', ''];
    const write = (fd, iovs, count, written) => {
        if (fd !== 1 && fd !== 2) {
            return ERRNO_BADF;
        }
        let total = 0;
        for (let index = 0; index < count; index += 1) {
            const at = view().getUint32(iovs + index * 8, true);
            const length = view().getUint32(iovs + index * 8 + 4, true);
            partial[fd] += decoder.decode(new Uint8Array(memory().buffer, at, length));
            total += length;
        }
        const lines = partial[fd].split('\n');
        partial[fd] = lines.pop();
        for (const line of lines) {
            log(line, fd);
        }
        view().setUint32(written, total, true);
        return ERRNO_SUCCESS;
    };
    const none = () => ERRNO_NOSYS;
    return {
        args_get: () => ERRNO_SUCCESS,
        args_sizes_get: (count, size) => {
            view().setUint32(count, 0, true);
            view().setUint32(size, 0, true);
            return ERRNO_SUCCESS;
        },
        environ_get: () => ERRNO_SUCCESS,
        environ_sizes_get: (count, size) => {
            view().setUint32(count, 0, true);
            view().setUint32(size, 0, true);
            return ERRNO_SUCCESS;
        },
        clock_time_get: (id, precision, time) => {
            // 0 is the wall clock, the rest monotonic, in nanoseconds.
            const nanoseconds =
                id === 0 ? BigInt(Date.now()) * 1000000n : BigInt(Math.round(performance.now() * 1e6));
            view().setBigUint64(time, nanoseconds, true);
            return ERRNO_SUCCESS;
        },
        random_get: (buffer, length) => {
            // getRandomValues gives at most 65536 bytes a call.
            for (let at = 0; at < length; at += 65536) {
                crypto.getRandomValues(new Uint8Array(memory().buffer, buffer + at, Math.min(65536, length - at)));
            }
            return ERRNO_SUCCESS;
        },
        fd_write: write,
        fd_read: () => ERRNO_BADF,
        fd_close: () => ERRNO_SUCCESS,
        fd_seek: () => ERRNO_BADF,
        fd_fdstat_get: (fd, stat) => {
            if (fd > 2) {
                return ERRNO_BADF;
            }
            // A character device, as a terminal is, with no rights withheld.
            new Uint8Array(memory().buffer, stat, 24).fill(0);
            view().setUint8(stat, 2);
            view().setBigUint64(stat + 8, 0xffffffffffffffffn, true);
            view().setBigUint64(stat + 16, 0xffffffffffffffffn, true);
            return ERRNO_SUCCESS;
        },
        fd_fdstat_set_flags: () => ERRNO_SUCCESS,
        fd_filestat_get: () => ERRNO_BADF,
        // No directory is lent: the client reads what the page hands it.
        fd_prestat_get: () => ERRNO_BADF,
        fd_prestat_dir_name: () => ERRNO_BADF,
        fd_readdir: () => ERRNO_BADF,
        path_open: () => ERRNO_NOENT,
        path_filestat_get: () => ERRNO_NOENT,
        path_remove_directory: () => ERRNO_NOENT,
        path_rename: () => ERRNO_NOENT,
        path_unlink_file: () => ERRNO_NOENT,
        poll_oneoff: none,
        sched_yield: () => ERRNO_SUCCESS,
        proc_exit: (code) => {
            throw new ProcessExit(code);
        },
    };
}
