/* Included before every OpenSSL source in the web build
   (tools/build_openssl_wasm.sh): wasi-libc has no chmod, which OpenSSL's
   random seed file calls to narrow a file's permissions. There are no
   permissions to narrow there, so it succeeds doing nothing. */
#include <sys/types.h>

static inline int chmod(const char* path, mode_t mode) {
    (void)path;
    (void)mode;
    return 0;
}
