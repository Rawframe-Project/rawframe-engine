// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// The browser's side of the WebGPU driver is JavaScript in its C files,
// as EM_JS functions. Emscripten compiles it into the program. A build
// without Emscripten (wasm32-wasi) imports each of those functions under
// its own name from the module "env", as Emscripten's own are, and the
// page passes what tools/gen_web_glue.py wrote from the same files
// (mrhi-0016). This is the one file of the library that includes
// Emscripten's headers.

#ifndef MAUL_RHI_SRC_WEB_JS_H
#define MAUL_RHI_SRC_WEB_JS_H

#ifdef __EMSCRIPTEN__
#include <emscripten/em_js.h>
#else
#define EM_JS(ret, name, params, ...)                                                              \
    __attribute__((import_module("env"), import_name(#name))) ret name params
#define EM_JS_DEPS(tag, deps)
#endif

#endif // MAUL_RHI_SRC_WEB_JS_H
