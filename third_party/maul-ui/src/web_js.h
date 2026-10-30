// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// The page's side of the ARIA adapter is JavaScript in its C files, as
// EM_JS functions. Emscripten compiles it into the program. A build
// without Emscripten (wasm32-wasi) imports each of those functions under
// its own name from the module "env", and the page passes what
// tools/gen_web_glue.py wrote from the same files, as Maul Window does.
// The functions the page calls back are the program's exports, under the
// names the JavaScript calls them by less Emscripten's leading underscore.
// This is the one file of the library that includes Emscripten's headers.

#ifndef MAUL_UI_SRC_WEB_JS_H
#define MAUL_UI_SRC_WEB_JS_H

#ifdef __EMSCRIPTEN__
#include <emscripten/em_js.h>
#include <emscripten/emscripten.h>
#define MUI_WEB_EXPORT(name) EMSCRIPTEN_KEEPALIVE
#else
#define MUI_WEB_IMPORT(name)          __attribute__((import_module("env"), import_name(#name)))
#define EM_JS(ret, name, params, ...) MUI_WEB_IMPORT(name) ret name params
#define MUI_WEB_EXPORT(name)          __attribute__((export_name(#name)))
#endif

#endif // MAUL_UI_SRC_WEB_JS_H
