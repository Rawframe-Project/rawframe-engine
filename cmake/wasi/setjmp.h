#pragma once

// setjmp and longjmp for wasm32-wasi (D383), which wasi-libc as packaged
// lacks: Clang's -wasm-enable-sjlj lowers each call through WebAssembly's
// exception handling into the runtime in setjmp.c. The buffer is the shape
// that runtime and the lowering agree on. Only code built with the lowering
// (FreeType, third_party/maul_ui.cmake) may call these.

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    void* functionInvocationId;
    unsigned label;
    struct {
        void* env;
        int value;
    } arg;
} jmp_buf[1];

int setjmp(jmp_buf env) __attribute__((returns_twice));
_Noreturn void longjmp(jmp_buf env, int value);

#ifdef __cplusplus
}
#endif
