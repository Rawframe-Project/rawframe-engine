#pragma once

// Compiler and architecture detection, and the debugger-break primitive
// (ADR-0075 class 4). A detection macro lands when a facility needs it.
//
// Everything here is a macro because the answers are used in `#if` and because
// a debugger break must expand at the call site to stop there.
// NOLINTBEGIN(cppcoreguidelines-macro-usage,cppcoreguidelines-macro-to-enum)

// Exactly one compiler macro is defined. An unrecognised compiler fails the
// translation unit rather than falling back to a guess.
#if defined(_MSC_VER) && !defined(__clang__)
#define RAWFRAME_COMPILER_MSVC 1
#elif defined(__clang__)
/// Clang, including clang-cl, Apple Clang, and Emscripten.
#define RAWFRAME_COMPILER_CLANG 1
#elif defined(__GNUC__)
#define RAWFRAME_COMPILER_GCC 1
#else
#error "Rawframe requires GCC, Clang, or MSVC; this compiler is unsupported."
#endif

// Exactly one architecture macro is defined: desktop and server x86-64, arm64
// for Apple silicon, Android, and Linux servers, and 32-bit WebAssembly for the
// browser client.
#if defined(__x86_64__) || defined(_M_X64)
#define RAWFRAME_ARCH_X86_64 1
#elif defined(__aarch64__) || defined(_M_ARM64)
#define RAWFRAME_ARCH_ARM64 1
#elif defined(__wasm32__)
#define RAWFRAME_ARCH_WASM32 1
#else
#error "Rawframe supports x86-64, arm64, and wasm32; this target is unsupported."
#endif

/// Traps to an attached debugger. Not a termination primitive: callers only use
/// it when a debugger is known to be attached.
#if defined(RAWFRAME_COMPILER_MSVC)
extern "C" void __debugbreak(void);
#define RAWFRAME_DEBUG_BREAK() __debugbreak()
#elif defined(RAWFRAME_ARCH_WASM32)
// The browser's debugger stops on its own breakpoints; there is no trap to raise.
#define RAWFRAME_DEBUG_BREAK() static_cast<void>(0)
#elif defined(RAWFRAME_COMPILER_CLANG)
#define RAWFRAME_DEBUG_BREAK() __builtin_debugtrap()
#elif defined(RAWFRAME_ARCH_X86_64)
#define RAWFRAME_DEBUG_BREAK() __asm__ volatile("int3")
#else
#define RAWFRAME_DEBUG_BREAK() __asm__ volatile("brk #0xf000")
#endif

// NOLINTEND(cppcoreguidelines-macro-usage,cppcoreguidelines-macro-to-enum)
