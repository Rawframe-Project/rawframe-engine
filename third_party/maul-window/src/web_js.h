// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// The page's side of the web backend is JavaScript in its C files, as
// EM_JS functions. Emscripten compiles it into the program. A build
// without Emscripten (wasm32-wasi) imports each of those functions under
// its own name from the module "env", as Emscripten's own are and as
// every file that only declares one expects, and the page passes what
// tools/gen_web_glue.py wrote from the same files. A host build, which
// only the fuzz target of the page's records makes
// (test/fuzz_web_records.c), declares each of them weak, and the target
// defines those its records reach. This is the one file that includes
// Emscripten's headers.

#ifndef MAUL_WINDOW_SRC_WEB_JS_H
#define MAUL_WINDOW_SRC_WEB_JS_H

#ifdef __EMSCRIPTEN__
#include <emscripten/em_js.h>
#include <emscripten/emscripten.h>
#define MWIN_WEB_IMPORT(name)
#elif defined(__wasm__)
// Marks a declaration of one of them in a header too, so every file that
// calls it imports it: the linker refuses a function only declared.
#define MWIN_WEB_IMPORT(name)         __attribute__((import_module("env"), import_name(#name)))
#define EM_JS(ret, name, params, ...) MWIN_WEB_IMPORT(name) ret name params
#define EM_JS_DEPS(tag, deps)
#else
#define MWIN_WEB_IMPORT(name)         __attribute__((weak))
#define EM_JS(ret, name, params, ...) MWIN_WEB_IMPORT(name) ret name params
#define EM_JS_DEPS(tag, deps)
#endif

// Milliseconds on the page's clock, performance.now().
MWIN_WEB_IMPORT(mwinWebNow) double mwinWebNow(void);

#endif // MAUL_WINDOW_SRC_WEB_JS_H
