// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// Maul UI's HarfBuzz configuration (record mui-0006), read after HB_LEAN
// and HB_MINI chose what to leave out and before the choices that follow
// from them: variable fonts stay in; and on WASI errno too, which
// wasi-libc declares thread-local, so HB_NO_ERRNO's renaming macro would
// collide with its declaration when the C++ library includes it.
#undef HB_NO_VAR
#if defined(__wasi__)
#undef HB_NO_ERRNO
#endif
