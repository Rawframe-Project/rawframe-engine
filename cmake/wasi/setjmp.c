// The runtime Clang's -wasm-enable-sjlj lowering calls (D383): setjmp
// records where it was called from and the label of the code after it;
// longjmp throws the C longjmp tag with the buffer and the value, which the
// lowered function catches, tests against its own invocation, and resumes at
// the label. Built with WebAssembly's exception handling.

#include <stddef.h>
#include <stdint.h>

struct Buffer {
    void* functionInvocationId;
    uint32_t label;
    struct Argument {
        void* env;
        int value;
    } argument;
};

void __wasm_setjmp(void* env, uint32_t label, void* functionInvocationId) {
    struct Buffer* buffer = env;
    if (label == 0 || functionInvocationId == NULL) {
        __builtin_trap();
    }
    buffer->label = label;
    buffer->functionInvocationId = functionInvocationId;
}

uint32_t __wasm_setjmp_test(void* env, void* functionInvocationId) {
    const struct Buffer* buffer = env;
    return buffer->functionInvocationId == functionInvocationId ? buffer->label : 0;
}

void __wasm_longjmp(void* env, int value) {
    struct Buffer* buffer = env;
    buffer->argument.env = env;
    buffer->argument.value = value != 0 ? value : 1;
    // Tag 1 is the C longjmp tag the linker gives the module.
    __builtin_wasm_throw(1, &buffer->argument);
}
